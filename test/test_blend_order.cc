// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Scheduler::sort_blended: within a priority the opaque calls first, in the order they were added, then the blended ones farthest
// first, so that what is seen through glass is drawn before the glass.  Off by default: the order is then the one the calls were added
// in, as it always was.

#include <memory>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <rove/obb.h>

#include "camera.h"
#include "draw_call.h"
#include "image.h"
#include "index.h"
#include "material.h"
#include "null_renderer.h"
#include "pixel.h"
#include "renderer.h"
#include "scheduler.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "texture.h"
#include "vertex.h"

using namespace edge;

namespace
{

CameraPtr view()
{
	CameraPtr camera = std::make_shared<Camera>();
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 10.0f);
	camera->look_at(rove::vec<3>(0, 0, 5), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

/// A square over the whole view at depth z, in `material`, its bounds where it is
DrawCallPtr square(RendererPtr const &renderer, MaterialPtr const &material, CameraPtr const &camera, float z, int priority = 0)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNT>(), sizeof(VERTEX_PTBNT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PTBNT *v = vb_mapping.as<VERTEX_PTBNT>().data();
	rove::vec<3> const corners[4] = {{-1, -1, z}, {1, -1, z}, {1, 1, z}, {-1, 1, z}};
	for (int k = 0; k < 4; ++k)
	{
		v[k].position = corners[k];
		v[k].tangent = rove::vec<3>(1, 0, 0);
		v[k].binormal = rove::vec<3>(0, 1, 0);
		v[k].normal = rove::vec<3>(0, 0, 1);
		v[k].texcoords = rove::vec<2>(0, 0);
	}
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *i = ib_mapping.as<INDEX_U16>().data();
	INDEX_U16 const order[6] = {0, 1, 2, 0, 2, 3};
	std::copy(order, order + 6, i);
	ib_mapping.unmap();
	DrawCallPtr dc = renderer->create_draw_call();
	dc->set_vertex_buffer(vb);
	dc->set_index_buffer(ib);
	dc->set_material(material);
	dc->set_triangles_count(2);
	dc->set_priority(priority);
	dc->set_obb(rove::obb<3>(rove::aabb<3>(rove::vec<3>(-1, -1, z - 0.01f), rove::vec<3>(1, 1, z + 0.01f))));
	dc->set_camera(camera);
	dc->commit();
	return dc;
}

MaterialPtr glass(RendererPtr const &renderer, rove::vec<4> const &colour)
{
	MaterialPtr m = renderer->create_material();
	m->set_cull_mode(Material::CullMode::NONE);
	m->set_base_color_factor(colour);
	m->set_alpha_blend_enabled(true);
	m->set_src_blend_mode(Material::BlendMode::SRC_ALPHA);
	m->set_dest_blend_mode(Material::BlendMode::INV_SRC_ALPHA);
	m->set_zwrite_enabled(false);
	return m;
}

MaterialPtr solid(RendererPtr const &renderer)
{
	MaterialPtr m = renderer->create_material();
	m->set_cull_mode(Material::CullMode::NONE);
	return m;
}

/// The null renderer, keeping the materials in the order they were drawn
class OrderRenderer: public NullRenderer
{
public:
	OrderRenderer(): NullRenderer(64, 64) {}
	std::vector<Material const *> drawn;

protected:
	void do_draw(DrawCall const &call) override { drawn.push_back(call.material().get()); }
};

}

BOOST_AUTO_TEST_SUITE(blend_order)

BOOST_AUTO_TEST_CASE_TEMPLATE(blended_squares_are_drawn_farthest_first_when_asked, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	CameraPtr const camera = view();
	MaterialPtr const red = glass(renderer, rove::vec<4>(1, 0, 0, 0.5f)), green = glass(renderer, rove::vec<4>(0, 1, 0, 0.5f));
	RenderTargetPtr const target = renderer->create_render_target(8, 8, PixelFormat::R32G32B32A32F);
	auto const draw = [&](bool sort)
	{
		auto const scheduler = std::make_shared<Scheduler>(renderer);
		scheduler->set_sort_blended(sort);
		// the near red square added first, then the far green one
		scheduler->add(square(renderer, red, camera, 1.0f));
		scheduler->add(square(renderer, green, camera, -1.0f));
		RenderPassClearState clear;
		clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
		renderer->begin_frame();
		renderer->begin_pass(target, clear);
		scheduler->flush();
		renderer->end_pass();
		renderer->finish_frame();
		return read_image<rove::rgba<float>>(*target->color_texture()).at(4, 4);
	};
	// sorted: green behind, red over it: red half, green a quarter
	rove::rgba<float> const sorted = draw(true);
	BOOST_CHECK_CLOSE(sorted.r, 0.5f, 1.0);
	BOOST_CHECK_CLOSE(sorted.g, 0.25f, 1.0);
	// as added (the default): green over red
	rove::rgba<float> const added = draw(false);
	BOOST_CHECK_CLOSE(added.r, 0.25f, 1.0);
	BOOST_CHECK_CLOSE(added.g, 0.5f, 1.0);
}

BOOST_AUTO_TEST_CASE(within_a_priority_the_opaque_go_first_in_their_order_then_the_blended_farthest_first)
{
	auto renderer = std::make_shared<OrderRenderer>();
	CameraPtr const camera = view();
	MaterialPtr const o1 = solid(renderer), o2 = solid(renderer), o3 = solid(renderer);
	MaterialPtr const near = glass(renderer, rove::vec<4>(1, 1, 1, 0.5f)), middle = glass(renderer, rove::vec<4>(1, 1, 1, 0.5f)),
		far = glass(renderer, rove::vec<4>(1, 1, 1, 0.5f)), later = glass(renderer, rove::vec<4>(1, 1, 1, 0.5f));
	auto const scheduler = std::make_shared<Scheduler>(renderer);
	scheduler->set_sort_blended(true);
	scheduler->add(square(renderer, middle, camera, 0.0f));
	scheduler->add(square(renderer, o1, camera, 2.0f));
	scheduler->add(square(renderer, near, camera, 3.0f));
	scheduler->add(square(renderer, later, camera, -4.0f, 1));        // a later priority: after all of priority 0, whatever its depth
	scheduler->add(square(renderer, o2, camera, -3.0f));
	scheduler->add(square(renderer, far, camera, -2.0f));
	scheduler->add(square(renderer, o3, camera, 0.5f, 1));
	flush_in_a_pass(scheduler);
	std::vector<Material const *> const expected{o1.get(), o2.get(), far.get(), middle.get(), near.get(), o3.get(), later.get()};
	BOOST_REQUIRE_EQUAL(renderer->drawn.size(), expected.size());
	for (size_t k = 0; k < expected.size(); ++k)
		BOOST_CHECK_MESSAGE(renderer->drawn[k] == expected[k], "draw " << k << " out of order");
	// off: exactly the order added, by priority
	renderer->drawn.clear();
	scheduler->set_sort_blended(false);
	scheduler->add(square(renderer, middle, camera, 0.0f));
	scheduler->add(square(renderer, o1, camera, 2.0f));
	scheduler->add(square(renderer, near, camera, 3.0f));
	flush_in_a_pass(scheduler);
	BOOST_REQUIRE_EQUAL(renderer->drawn.size(), 3u);
	BOOST_CHECK(renderer->drawn[0] == middle.get() && renderer->drawn[1] == o1.get() && renderer->drawn[2] == near.get());
}

BOOST_AUTO_TEST_SUITE_END()
