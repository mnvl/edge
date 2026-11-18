// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// A pass is drawn through its lists, the same on every renderer: begin_pass() makes them (one unless asked for more, as many as
// max_draw_lists() allows), the renderer's own draws and viewport are list 0's, and end_pass() takes them away.  What a list draws
// with is the frame's until the frame is submitted, as what the renderer draws with is.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "scheduler.h"
#include "shader_program.h"
#include "test_backends.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

/// A committed, visible call of one triangle of @p renderer's.
std::shared_ptr<DrawCall> triangle(std::shared_ptr<Renderer> const &renderer, std::shared_ptr<Material> material = nullptr)
{
	std::shared_ptr<VertexBuffer> const vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);
	auto mapping = vb->write(Contents::DISCARD);
	std::memset(mapping.data(), 0, sizeof(VERTEX_PT) * 3);
	mapping.unmap();
	std::shared_ptr<DrawCall> const call = renderer->create_draw_call();
	call->set_vertex_buffer(vb);
	call->set_material(material ? material : renderer->create_material());
	call->set_camera(std::make_shared<Camera>());
	call->set_triangles_count(1);
	call->set_always_visible(true);
	call->commit();
	return call;
}

/// What it is given, in order: "viewport x y w h" and "draw <the call's material's name>".
struct LoggingRenderer: NullRenderer
{
	std::vector<std::string> log;

	void do_set_viewport(int x, int y, int width, int height) override
	{
		log.push_back("viewport " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(width) + " " + std::to_string(height));
	}
	void do_draw(DrawCall const &call) override { log.push_back("draw " + call.material()->name()); }
};

std::shared_ptr<Material> named(std::shared_ptr<Renderer> const &renderer, std::string const &name)
{
	std::shared_ptr<Material> const material = renderer->create_material();
	material->set_name(name);
	return material;
}

}

BOOST_AUTO_TEST_SUITE(draw_lists)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_pass_has_one_list_unless_it_asks_for_more, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::shared_ptr<RenderTarget> const target = renderer->create_render_target(16, 8, PixelFormat::A8R8G8B8);
	BOOST_REQUIRE_GE(renderer->max_draw_lists(), 1u);

	BOOST_CHECK_EQUAL(renderer->draw_lists_count(), 0u);
	BOOST_CHECK_THROW(renderer->draw_list(), std::logic_error);
	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->draw_list(), std::logic_error);         // a frame, no pass
	BOOST_CHECK_THROW(renderer->begin_pass(target, RenderPassClearState(), 0, 0), std::invalid_argument);
	BOOST_CHECK(renderer->in_frame() && !renderer->in_pass());          // as it was
	if (renderer->max_draw_lists() < std::numeric_limits<size_t>::max())
	{
		BOOST_CHECK_THROW(renderer->begin_pass(target, RenderPassClearState(), 0, renderer->max_draw_lists() + 1), std::invalid_argument);
		BOOST_CHECK(!renderer->in_pass());
	}

	renderer->begin_pass(target);
	BOOST_CHECK_EQUAL(renderer->draw_lists_count(), 1u);
	DrawList &list = renderer->draw_list();
	BOOST_CHECK(&renderer->draw_list(0) == &list);
	BOOST_CHECK_THROW(renderer->draw_list(1), std::out_of_range);
	renderer->end_pass();
	BOOST_CHECK_EQUAL(renderer->draw_lists_count(), 0u);
	BOOST_CHECK_THROW(renderer->draw_list(), std::logic_error);

	size_t const lists = std::min<size_t>(renderer->max_draw_lists(), 3);
	renderer->begin_pass(target, RenderPassClearState(), 0, lists);
	BOOST_CHECK_EQUAL(renderer->draw_lists_count(), lists);
	for (size_t i = 0; i < lists; ++i)
		for (size_t j = 0; j < i; ++j)
			BOOST_CHECK(&renderer->draw_list(i) != &renderer->draw_list(j));
	BOOST_CHECK_THROW(renderer->draw_list(lists), std::out_of_range);
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_list_s_viewport_is_a_rectangle_of_the_pass_s_target, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	renderer->begin_frame();
	renderer->begin_pass(renderer->create_render_target(16, 8, PixelFormat::A8R8G8B8));
	DrawList &list = renderer->draw_list();
	list.set_viewport(0, 0, 16, 8);
	list.set_viewport(8, 4, 8, 4);
	BOOST_CHECK_THROW(list.set_viewport(9, 0, 8, 8), std::out_of_range);
	BOOST_CHECK_THROW(list.set_viewport(0, 0, 16, 9), std::out_of_range);
	BOOST_CHECK_THROW(list.set_viewport(-1, 0, 4, 4), std::out_of_range);
	BOOST_CHECK_THROW(list.set_viewport(0, 0, 0, 4), std::out_of_range);
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_draw_into_a_list_is_checked_as_a_draw_through_the_renderer, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::shared_ptr<Renderer> const other = std::make_shared<NullRenderer>(64, 64);
	std::shared_ptr<DrawCall> const call = triangle(renderer);
	std::shared_ptr<DrawCall> const others = triangle(other);
	std::shared_ptr<DrawCall> const uncommitted = renderer->create_draw_call();
	std::shared_ptr<DrawCall> const no_triangles = triangle(renderer);
	no_triangles->set_triangles_count(0);
	no_triangles->commit();

	renderer->begin_frame();
	renderer->begin_pass(renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8));
	DrawList &list = renderer->draw_list();
	BOOST_CHECK_THROW(uncommitted->execute(list), std::logic_error);
	BOOST_CHECK_THROW(others->execute(list), std::invalid_argument);      // a call of another renderer
	no_triangles->execute(list);                                          // nothing to draw: nothing
	call->execute(list);
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(what_a_list_draws_with_lives_until_the_frame_is_submitted, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::shared_ptr<Texture> texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	{
		auto mapping = texture->write(Contents::DISCARD);
		std::memset(mapping.data(), 255, 4 * 4 * 4);
	}
	std::shared_ptr<Material> material = renderer->create_material();
	material->set_diffuse_texture(texture);
	std::shared_ptr<DrawCall> call = triangle(renderer, material);
	std::weak_ptr<VertexBuffer> const vb = call->vertex_buffer();
	std::weak_ptr<Material> const held_material = material;
	std::weak_ptr<Texture> const held_texture = texture;

	for (bool submit : {false, true})
	{
		renderer->begin_frame();
		renderer->begin_pass(renderer->create_render_target(16, 8, PixelFormat::A8R8G8B8));
		call->execute(renderer->draw_list());
		if (submit)
		{
			// the last owners let go inside the pass: the list holds them, then the frame
			call.reset();
			material.reset();
			texture.reset();
		}
		renderer->end_pass();
		BOOST_CHECK(!vb.expired() && !held_material.expired() && !held_texture.expired());
		if (submit)
			renderer->submit_frame();
		else
			renderer->finish_frame();
	}
	BOOST_CHECK(vb.expired());
	BOOST_CHECK(held_material.expired());
	BOOST_CHECK(held_texture.expired());
}

BOOST_AUTO_TEST_CASE(the_renderer_s_viewport_and_draws_are_list_0_s)
{
	auto const renderer = std::make_shared<LoggingRenderer>();
	std::shared_ptr<DrawCall> const a = triangle(renderer, named(renderer, "a"));
	std::shared_ptr<DrawCall> const b = triangle(renderer, named(renderer, "b"));
	std::shared_ptr<DrawCall> const c = triangle(renderer, named(renderer, "c"));
	renderer->begin_frame();
	renderer->begin_pass(renderer->create_render_target(16, 8, PixelFormat::A8R8G8B8));
	renderer->set_viewport(1, 2, 3, 4);
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, 17, 8), std::out_of_range);      // refused before the backend sees it
	a->execute();
	renderer->draw_list().set_viewport(0, 0, 16, 8);
	b->execute(renderer->draw_list());
	{
		Scheduler scheduler(renderer);
		scheduler.add(c);
		scheduler.add(a);
		scheduler.flush(renderer->draw_list());
		BOOST_CHECK_EQUAL(scheduler.visible_calls_count(), 2u);
	}
	renderer->end_pass();
	renderer->finish_frame();
	std::vector<std::string> const expected{"viewport 1 2 3 4", "draw a", "viewport 0 0 16 8", "draw b", "draw c", "draw a"};
	BOOST_CHECK_EQUAL_COLLECTIONS(renderer->log.begin(), renderer->log.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(a_draw_outside_a_pass_reaches_no_list)
{
	auto const renderer = std::make_shared<LoggingRenderer>();
	std::shared_ptr<DrawCall> const call = triangle(renderer, named(renderer, "a"));
	BOOST_CHECK_THROW(call->execute(), std::logic_error);
	renderer->begin_frame();
	BOOST_CHECK_THROW(call->execute(), std::logic_error);
	Scheduler scheduler(renderer);
	scheduler.add(call);
	BOOST_CHECK_THROW(scheduler.flush(), std::logic_error);
	renderer->finish_frame();
	BOOST_CHECK(renderer->log.empty());
}

BOOST_AUTO_TEST_SUITE_END()

// Every change to a material is one a renderer that remembers what the material drew with (a Vulkan list: its pipeline and
// descriptor set) sees: the revision moves on.
BOOST_AUTO_TEST_SUITE(material_revision)

BOOST_AUTO_TEST_CASE(every_setter_moves_the_revision_on)
{
	auto const renderer = std::make_shared<NullRenderer>();
	std::shared_ptr<Material> const m = renderer->create_material();
	std::shared_ptr<Texture> const texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	std::shared_ptr<ShaderProgram> const program = renderer->create_shader_program("a.vert.spv", "a.frag.spv");
	unsigned char const bytes[16] = {};
	std::vector<std::pair<char const *, std::function<void()>>> const setters{
		{"set_shader_program(pass)", [&] { m->set_shader_program(1, program); }},
		{"clear_shader_program", [&] { m->clear_shader_program(1); }},
		{"set_shader_program", [&] { m->set_shader_program(program); }},
		{"set_diffuse_texture", [&] { m->set_diffuse_texture(texture); }},
		{"set_diffuse_texture(1)", [&] { m->set_diffuse_texture(texture, 1); }},
		{"set_texture_blending_weights_texture", [&] { m->set_texture_blending_weights_texture(texture); }},
		{"set_normal_map_texture", [&] { m->set_normal_map_texture(texture); }},
		{"set_metallic_roughness_texture", [&] { m->set_metallic_roughness_texture(texture); }},
		{"set_emissive_texture", [&] { m->set_emissive_texture(texture); }},
		{"set_metallic_factor", [&] { m->set_metallic_factor(0.5f); }},
		{"set_roughness_factor", [&] { m->set_roughness_factor(0.5f); }},
		{"set_emissive_factor", [&] { m->set_emissive_factor(rove::vec<3>(1, 0, 0)); }},
		{"set_alpha_cutoff", [&] { m->set_alpha_cutoff(0.5f); }},
		{"set_normal_scale", [&] { m->set_normal_scale(2.0f); }},
		{"set_occlusion", [&] { m->set_occlusion(true); }},
		{"set_occlusion_strength", [&] { m->set_occlusion_strength(0.5f); }},
		{"set_parameters", [&] { m->set_parameters(bytes, sizeof(bytes)); }},
		{"set_name", [&] { m->set_name("m"); }},
		{"set_cull_mode", [&] { m->set_cull_mode(Material::CullMode::NONE); }},
		{"set_zwrite_enabled", [&] { m->set_zwrite_enabled(false); }},
		{"set_stencil_write", [&] { m->set_stencil_write(uint8_t(1)); }},
		{"set_alpha_blend_enabled", [&] { m->set_alpha_blend_enabled(true); }},
		{"set_preserve_destination_alpha", [&] { m->set_preserve_destination_alpha(true); }},
		{"set_src_blend_mode", [&] { m->set_src_blend_mode(Material::BlendMode::SRC_ALPHA); }},
		{"set_dest_blend_mode", [&] { m->set_dest_blend_mode(Material::BlendMode::INV_SRC_ALPHA); }},
		{"set_extra_parameter", [&] { m->set_extra_parameter(0, rove::vec<4>(1, 2, 3, 4)); }},
		{"set_base_color_factor", [&] { m->set_base_color_factor(rove::vec<4>(1, 0, 0, 1)); }},
	};
	for (auto const &[name, set] : setters)
	{
		uint64_t const before = m->revision();
		set();
		BOOST_CHECK_MESSAGE(m->revision() != before, name << " left the revision at " << before);
	}
	// reading is not a change
	uint64_t const read = m->revision();
	(void)m->diffuse_texture();
	(void)m->parameters();
	BOOST_CHECK_EQUAL(m->revision(), read);
}

BOOST_AUTO_TEST_SUITE_END()
