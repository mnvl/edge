// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Instanced draws: a draw call given instances is drawn once for each, each placing the draw's world (and its bounds) by its matrix,
// the instances out of view culled before anything reaches the GPU; a model drawn as instances is the model drawn at each place.

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <rove/matrix.h>
#include <rove/obb.h>

#include "animated_model.h"
#include "camera.h"
#include "draw_call.h"
#include "image.h"
#include "index.h"
#include "material.h"
#include "null_renderer.h"
#include "pixel.h"
#include "renderer.h"
#include "scheduler.h"
#include "static_model.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "texture.h"
#include "vertex.h"

using namespace edge;

namespace
{

rove::matrix<4, 4> moved(float x, float y, float z = 0)
{
	rove::matrix<4, 4> m;
	m.translation(x, y, z);
	return m;
}

rove::matrix<4, 4> scaled(float s)
{
	rove::matrix<4, 4> m;
	m.scaling(s, s, s);
	return m;
}

/// A square of half-width `extent` in the plane z = 0, seen by an orthographic camera from +z: the view is [-1, 1] on x and y, the
/// target `size` pixels square, +x to the right and +y up.
struct Scene
{
	VertexBufferPtr vertices;
	IndexBufferPtr indices;
	CameraPtr camera;
	RenderTargetPtr target;
	MaterialPtr material;
	SchedulerPtr scheduler;
	int size;

	Scene(RendererPtr const &renderer, float extent, int size_ = 64):
		size(size_)
	{
		vertices = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNT>(), sizeof(VERTEX_PTBNT) * 4);
		auto vertices_mapping = vertices->write(Contents::DISCARD);
		VERTEX_PTBNT *v = vertices_mapping.as<VERTEX_PTBNT>().data();
		rove::vec<3> const corners[4] = {{-extent, -extent, 0}, {extent, -extent, 0}, {extent, extent, 0}, {-extent, extent, 0}};
		for (int k = 0; k < 4; ++k)
		{
			v[k].position = corners[k];
			v[k].tangent = rove::vec<3>(1, 0, 0);
			v[k].binormal = rove::vec<3>(0, 1, 0);
			v[k].normal = rove::vec<3>(0, 0, 1);
			v[k].texcoords = rove::vec<2>(0, 0);
		}
		vertices_mapping.unmap();
		indices = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
		auto indices_mapping = indices->write(Contents::DISCARD);
		INDEX_U16 *i = indices_mapping.as<INDEX_U16>().data();
		INDEX_U16 const order[6] = {0, 1, 2, 0, 2, 3};
		std::copy(order, order + 6, i);
		indices_mapping.unmap();
		camera = std::make_shared<Camera>();
		camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
		camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
		target = renderer->create_render_target(size, size, PixelFormat::R32G32B32A32F);
		material = renderer->create_material();
		material->set_cull_mode(Material::CullMode::NONE);
		material->set_base_color_factor(rove::vec<4>(1, 0.5f, 0.25f, 1));
		scheduler = std::make_shared<Scheduler>(renderer);
	}

	/// The square's draw call at `world`, its bounds the square's, drawn once for each of `instances` if there are any
	DrawCallPtr call(RendererPtr const &renderer, rove::matrix<4, 4> const &world,
		std::vector<rove::matrix<4, 4>> const *instances = nullptr) const
	{
		DrawCallPtr dc = renderer->create_draw_call();
		dc->set_vertex_buffer(vertices);
		dc->set_index_buffer(indices);
		dc->set_material(material);
		dc->set_triangles_count(2);
		dc->set_world_matrix(world);
		rove::obb<3> bounds(rove::aabb<3>(rove::vec<3>(-0.1f, -0.1f, -0.01f), rove::vec<3>(0.1f, 0.1f, 0.01f)));
		bounds.transform(world);
		dc->set_obb(bounds);
		if (instances)
			dc->set_instances(*instances);
		dc->set_camera(camera);
		dc->commit();
		return dc;
	}

	Image<rove::rgba<float>> render(RendererPtr const &renderer, std::vector<DrawCallPtr> const &calls) const
	{
		for (DrawCallPtr const &dc : calls)
			scheduler->add(dc);
		RenderPassClearState clear;
		clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
		renderer->begin_frame();
		renderer->begin_pass(target, clear);
		scheduler->flush();
		renderer->end_pass();
		renderer->finish_frame();
		return read_image<rove::rgba<float>>(*target->color_texture());
	}

	/// The pixel under world point (x, y)
	rove::rgba<float> at(Image<rove::rgba<float>> const &image, float x, float y) const
	{
		int const column = std::min(size - 1, int((x + 1) / 2 * size));
		int const row = std::min(size - 1, int((1 - y) / 2 * size));
		return image.at(column, row);
	}

	static bool drawn(rove::rgba<float> const &p)
	{
		return std::abs(p.r - 1) < 1e-3f && std::abs(p.g - 0.5f) < 1e-3f && std::abs(p.b - 0.25f) < 1e-3f;
	}

	static bool empty(rove::rgba<float> const &p)
	{
		return p.r == 0 && p.g == 0 && p.b == 0;
	}
};

/// What a draw call gave the shader when it was drawn
struct Drawn
{
	rove::matrix<4, 4> world_view_projection, world;
	bool instanced;
};

/// The null renderer, keeping what each draw was given
class CapturingRenderer: public NullRenderer
{
public:
	CapturingRenderer(): NullRenderer(320, 200) {}
	std::vector<Drawn> draws;
	std::vector<std::vector<rove::matrix<4, 4>>> instances;

protected:
	void do_draw(DrawCall const &call) override
	{
		draws.push_back({call.shader_world_view_projection(), call.shader_world(), call.is_instanced()});
		instances.push_back(call.visible_instances());
	}
};

bool same(rove::matrix<4, 4> const &a, rove::matrix<4, 4> const &b)
{
	return rove::equal(a, b, 1e-6f);
}

}

BOOST_AUTO_TEST_SUITE(instancing)

BOOST_AUTO_TEST_CASE_TEMPLATE(an_instanced_draw_is_drawn_once_where_each_instance_places_it, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Scene scene(renderer, 0.1f);
	std::vector<rove::matrix<4, 4>> const instances{moved(-0.5f, 0), moved(0.5f, 0), moved(0, 0.5f)};
	// an instanced draw, and a plain one beside it in the same pass, which draws once at its world as before
	Image<rove::rgba<float>> const image = scene.render(renderer,
		{scene.call(renderer, rove::matrix<4, 4>(), &instances), scene.call(renderer, moved(0, -0.5f))});
	BOOST_CHECK(Scene::drawn(scene.at(image, -0.5f, 0)));
	BOOST_CHECK(Scene::drawn(scene.at(image, 0.5f, 0)));
	BOOST_CHECK(Scene::drawn(scene.at(image, 0, 0.5f)));
	BOOST_CHECK(Scene::drawn(scene.at(image, 0, -0.5f)));
	BOOST_CHECK(Scene::empty(scene.at(image, 0, 0)));             // the instanced square is not also drawn at its own world
	BOOST_CHECK(Scene::empty(scene.at(image, -0.5f, 0.5f)));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(an_instance_places_the_draws_world_not_the_other_way_round, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	// a square of half-width 0.2 at half size, put at x = +-0.5: instance * world, so 0.1 wide at +-0.5 (world * instance would halve
	// the offsets too, to +-0.25, and leave +-0.5 empty)
	Scene scene(renderer, 0.2f);
	std::vector<rove::matrix<4, 4>> const instances{moved(-0.5f, 0), moved(0.5f, 0)};
	Image<rove::rgba<float>> const image = scene.render(renderer, {scene.call(renderer, scaled(0.5f), &instances)});
	BOOST_CHECK(Scene::drawn(scene.at(image, 0.5f, 0)));
	BOOST_CHECK(Scene::drawn(scene.at(image, -0.5f, 0.05f)));
	BOOST_CHECK(Scene::empty(scene.at(image, 0.5f, 0.15f)));         // at half size the square reaches 0.1, not 0.2
	BOOST_CHECK(Scene::empty(scene.at(image, 0.25f, 0)));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(more_instances_than_a_batch_holds_are_all_drawn, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	Scene scene(renderer, 0.03f, 256);
	std::vector<rove::matrix<4, 4>> instances;
	for (int row = 0; row < 15; ++row)
		for (int column = 0; column < 20; ++column)
			instances.push_back(moved(-0.95f + 0.1f * column, -0.7f + 0.1f * row));
	BOOST_REQUIRE_GT(instances.size(), Renderer::MAX_INSTANCES);
	Image<rove::rgba<float>> const image = scene.render(renderer, {scene.call(renderer, rove::matrix<4, 4>(), &instances)});
	int drawn = 0;
	for (rove::matrix<4, 4> const &m : instances)
		drawn += Scene::drawn(scene.at(image, m.ij[3][0], m.ij[3][1]));
	BOOST_CHECK_EQUAL(drawn, int(instances.size()));
	BOOST_CHECK(Scene::empty(scene.at(image, -0.9f, -0.75f)));       // between them
}

BOOST_AUTO_TEST_CASE(the_renderer_gets_each_visible_instances_world_and_none_out_of_view)
{
	auto renderer = std::make_shared<CapturingRenderer>();
	Scene scene(renderer, 0.1f);
	rove::matrix<4, 4> const world = scaled(0.5f);
	std::vector<rove::matrix<4, 4>> const instances{moved(-0.5f, 0), moved(50, 0), moved(0, 0.5f)};
	DrawCallPtr const dc = scene.call(renderer, world, &instances);
	BOOST_CHECK(dc->is_visible());
	BOOST_CHECK_EQUAL(dc->instance_count(), 2u);
	scene.scheduler->add(dc);
	flush_in_a_pass(scene.scheduler);
	BOOST_REQUIRE_EQUAL(renderer->draws.size(), 1u);
	std::vector<rove::matrix<4, 4>> const &got = renderer->instances.at(0);
	BOOST_REQUIRE_EQUAL(got.size(), 2u);
	BOOST_CHECK(same(got[0], instances[0] * world));
	BOOST_CHECK(same(got[1], instances[2] * world));
	// the draw's own matrices are the camera's alone: the instances carry the world
	Drawn const &draw = renderer->draws.at(0);
	BOOST_CHECK(draw.instanced);
	BOOST_CHECK(same(draw.world, rove::matrix<4, 4>()));
	BOOST_CHECK(same(draw.world_view_projection, scene.camera->projection_matrix() * scene.camera->view_matrix()));
	// two triangles for each instance drawn, and each offered
	BOOST_CHECK_EQUAL(scene.scheduler->executed_triangles_count(), 4u);
	BOOST_CHECK_EQUAL(scene.scheduler->offered_triangles_count(), 6u);
}

BOOST_AUTO_TEST_CASE(a_draw_whose_instances_are_all_out_of_view_is_not_drawn_unless_always_visible)
{
	auto renderer = std::make_shared<CapturingRenderer>();
	Scene scene(renderer, 0.1f);
	std::vector<rove::matrix<4, 4>> const far_away{moved(50, 0), moved(0, -50)};
	DrawCallPtr const dc = scene.call(renderer, rove::matrix<4, 4>(), &far_away);
	BOOST_CHECK(!dc->is_visible());
	BOOST_CHECK_EQUAL(dc->instance_count(), 0u);
	scene.scheduler->add(dc);
	flush_in_a_pass(scene.scheduler);
	BOOST_CHECK(renderer->draws.empty());

	DrawCallPtr const all = renderer->create_draw_call();
	all->set_vertex_buffer(scene.vertices);
	all->set_index_buffer(scene.indices);
	all->set_material(scene.material);
	all->set_triangles_count(2);
	all->set_always_visible(true);
	all->set_instances(far_away);
	all->set_camera(scene.camera);
	all->commit();
	BOOST_CHECK_EQUAL(all->instance_count(), 2u);
	scene.scheduler->add(all);
	flush_in_a_pass(scene.scheduler);
	BOOST_REQUIRE_EQUAL(renderer->instances.size(), 1u);
	BOOST_CHECK_EQUAL(renderer->instances.at(0).size(), 2u);
}

BOOST_AUTO_TEST_CASE(no_instances_is_refused_and_a_plain_draw_has_none)
{
	auto renderer = std::make_shared<CapturingRenderer>();
	Scene scene(renderer, 0.1f);
	DrawCallPtr const dc = renderer->create_draw_call();
	BOOST_CHECK_THROW(dc->set_instances(std::vector<rove::matrix<4, 4>>()), std::invalid_argument);
	DrawCallPtr const plain = scene.call(renderer, moved(0.25f, 0));
	BOOST_CHECK_EQUAL(plain->instance_count(), 1u);
	scene.scheduler->add(plain);
	flush_in_a_pass(scene.scheduler);
	BOOST_REQUIRE_EQUAL(renderer->draws.size(), 1u);
	BOOST_CHECK(!renderer->draws.at(0).instanced);
	BOOST_CHECK(renderer->instances.at(0).empty());
	BOOST_CHECK(same(renderer->draws.at(0).world, moved(0.25f, 0)));
}

namespace
{

/// How many of the pixels differ by more than a little between two images of the same size
int differing(Image<rove::rgba<float>> const &a, Image<rove::rgba<float>> const &b)
{
	int count = 0;
	for (int y = 0; y < a.height(); ++y)
		for (int x = 0; x < a.width(); ++x)
		{
			rove::rgba<float> const p = a.at(x, y), q = b.at(x, y);
			if (std::abs(p.r - q.r) + std::abs(p.g - q.g) + std::abs(p.b - q.b) + std::abs(p.a - q.a) > 0.02f)
				++count;
		}
	return count;
}

int covered(Image<rove::rgba<float>> const &a)
{
	int count = 0;
	for (int y = 0; y < a.height(); ++y)
		for (int x = 0; x < a.width(); ++x)
			count += a.at(x, y).a > 0;
	return count;
}

template<class M>
Image<rove::rgba<float>> draw_model(RendererPtr const &renderer, M &model, CameraPtr const &camera,
	std::vector<rove::matrix<4, 4>> const &worlds, bool as_instances)
{
	if (as_instances)
	{
		model.draw_instances(camera, worlds);
	}
	else
	{
		for (rove::matrix<4, 4> const &world : worlds)
			model.draw(camera, world);
	}
	RenderTargetPtr const target = renderer->create_render_target(128, 128, PixelFormat::R32G32B32A32F);
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	model.scheduler()->flush();
	renderer->end_pass();
	renderer->finish_frame();
	return read_image<rove::rgba<float>>(*target->color_texture());
}

CameraPtr looking_at_the_origin()
{
	CameraPtr camera = std::make_shared<Camera>();
	camera->set_orthographic_projection(4.0f, 4.0f, 0.1f, 20.0f);
	camera->look_at(rove::vec<3>(0, 0, 5), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_static_model_drawn_as_instances_is_the_model_drawn_at_each_place, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	auto const scheduler = std::make_shared<Scheduler>(renderer);
	auto const model_ptr = std::make_shared<StaticModelPTBNT>(renderer, scheduler);
	StaticModelPTBNT &model = *model_ptr;
	model.load_from_gltf(TestFixture::graphic_sources_path() + "test/data/cone.glb");
	std::vector<rove::matrix<4, 4>> const worlds{moved(-1.2f, 0) * scaled(0.4f), moved(1.2f, 0.5f) * scaled(0.6f),
		moved(0, -1.2f) * scaled(0.3f)};
	Image<rove::rgba<float>> const one_by_one = draw_model(renderer, model, looking_at_the_origin(), worlds, false);
	Image<rove::rgba<float>> const instanced = draw_model(renderer, model, looking_at_the_origin(), worlds, true);
	int const area = covered(one_by_one);
	BOOST_REQUIRE_GT(area, 500);                          // the three cones are there
	BOOST_CHECK_EQUAL(covered(instanced), area);
	BOOST_CHECK_LE(differing(one_by_one, instanced), area / 100);       // the same but for roundings along edges
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_skinned_model_drawn_as_instances_is_the_model_in_its_pose_at_each_place, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	auto const scheduler = std::make_shared<Scheduler>(renderer);
	auto const model_ptr = std::make_shared<AnimatedModelPTBNJWT>(renderer, scheduler);
	AnimatedModelPTBNJWT &model = *model_ptr;
	model.load_from_gltf(TestFixture::graphic_sources_path() + "test/data/test_figure.glb");
	model.pose(0.3f);
	std::vector<rove::matrix<4, 4>> const worlds{moved(-1.0f, -1.5f) * scaled(1.2f), moved(1.0f, -1.0f)};
	Image<rove::rgba<float>> const one_by_one = draw_model(renderer, model, looking_at_the_origin(), worlds, false);
	Image<rove::rgba<float>> const instanced = draw_model(renderer, model, looking_at_the_origin(), worlds, true);
	int const area = covered(one_by_one);
	BOOST_REQUIRE_GT(area, 300);
	BOOST_CHECK_LE(differing(one_by_one, instanced), area / 100);
}

/// Skinned draws of one frame keep their own joint matrices (Vulkan packs them one draw after another, each taking what its palette
/// needs): two figures drawn in one frame are each the figure drawn alone.  The second is carried 2 m aside by its root joint, so every
/// one of its matrices differs from the first's, and any of them read for the first's would put some of it somewhere else.
BOOST_AUTO_TEST_CASE_TEMPLATE(skinned_draws_of_one_frame_keep_their_own_joint_matrices, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	auto const scheduler = std::make_shared<Scheduler>(renderer);
	auto const prototype = std::make_shared<AnimatedModelPTBNJWT>(renderer, scheduler);
	prototype->load_from_gltf(TestFixture::graphic_sources_path() + "test/data/test_figure.glb");
	auto posed = [&](float time, float carried)
	{
		auto model = std::make_shared<AnimatedModelPTBNJWT>(static_cast<AnimatedModelPTBNJWT const &>(*prototype), scheduler);
		model->bind_animations();
		model->pose(time);
		int roots = 0;
		for (JointPtr const &joint : model->joints())
			if (!joint->parent.lock() && !joint->transforms.empty())
			{
				joint->transforms[0].matrix = moved(carried, 0) * joint->transforms[0].matrix;
				++roots;
			}
		BOOST_REQUIRE_GT(roots, 0);
		model->rebuild_joints();
		return model;
	};
	auto const a = posed(0.3f, 0.0f), b = posed(0.9f, 2.0f);
	CameraPtr const camera = looking_at_the_origin();
	rove::matrix<4, 4> const left = moved(-1.0f, -1.5f);

	Image<rove::rgba<float>> const a_alone = draw_model(renderer, *a, camera, {left}, false);
	Image<rove::rgba<float>> const b_alone = draw_model(renderer, *b, camera, {left}, false);
	BOOST_REQUIRE_GT(covered(a_alone), 300);
	BOOST_REQUIRE_GT(covered(b_alone), 300);

	a->draw(camera, left);
	b->draw(camera, left);
	Image<rove::rgba<float>> const together = draw_model(renderer, *a, camera, {}, false);
	int wrong = 0;
	for (int y = 0; y < together.height(); ++y)
		for (int x = 0; x < together.width(); ++x)
		{
			BOOST_REQUIRE(!(a_alone.at(x, y).a > 0 && b_alone.at(x, y).a > 0));    // the figures apart
			rove::rgba<float> const want = a_alone.at(x, y).a > 0 ? a_alone.at(x, y) : b_alone.at(x, y);
			rove::rgba<float> const got = together.at(x, y);
			wrong += std::abs(got.r - want.r) + std::abs(got.g - want.g) + std::abs(got.b - want.b) + std::abs(got.a - want.a) > 0.02f;
		}
	BOOST_CHECK_EQUAL(wrong, 0);
}

BOOST_AUTO_TEST_SUITE_END()
