// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// The frame and its passes, the same on every renderer (the base Renderer keeps the order): what is out of order is refused, the
// renderer left as it was, and drawing goes on.

#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "test_backends.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

BOOST_AUTO_TEST_SUITE(renderer_contract)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_frame_and_its_passes_are_begun_and_ended_in_order, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetPtr const target = renderer->create_render_target(16, 8, PixelFormat::A8R8G8B8);

	BOOST_CHECK(!renderer->in_frame());
	BOOST_CHECK_THROW(renderer->finish_frame(), std::logic_error);
	BOOST_CHECK_THROW(renderer->submit_frame(), std::logic_error);
	BOOST_CHECK_THROW(renderer->begin_pass(target), std::logic_error);
	BOOST_CHECK_THROW(renderer->end_pass(), std::logic_error);

	renderer->begin_frame();
	BOOST_CHECK(renderer->in_frame() && !renderer->in_pass());
	BOOST_CHECK_THROW(renderer->begin_frame(), std::logic_error);
	BOOST_CHECK_THROW(renderer->end_pass(), std::logic_error);
	BOOST_CHECK_THROW(renderer->begin_pass(RenderTargetPtr()), std::invalid_argument);
	BOOST_CHECK(renderer->in_frame() && !renderer->in_pass());       // as it was

	renderer->begin_pass(target, RenderPassClearState(), 3);
	BOOST_CHECK(renderer->in_pass());
	BOOST_CHECK_EQUAL(renderer->pass_type(), 3u);
	BOOST_CHECK(renderer->pass_target() == target);
	BOOST_CHECK_THROW(renderer->begin_pass(target), std::logic_error);
	BOOST_CHECK_THROW(renderer->finish_frame(), std::logic_error);  // a pass left open is a mistake, not ended for it
	BOOST_CHECK_THROW(renderer->submit_frame(), std::logic_error);
	BOOST_CHECK(renderer->in_pass());
	renderer->end_pass();
	BOOST_CHECK(!renderer->in_pass());
	BOOST_CHECK(!renderer->pass_target());
	renderer->finish_frame();
	BOOST_CHECK(!renderer->in_frame());

	// and again, submitted
	renderer->begin_frame();
	renderer->begin_pass(target);
	renderer->end_pass();
	renderer->submit_frame();
	BOOST_CHECK(!renderer->in_frame());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(what_is_set_between_frames_is_refused_inside_one, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::vector<unsigned char> const constants(64, 1);
	TexturePtr const texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);

	renderer->set_frame_constants(constants.data(), constants.size());
	BOOST_CHECK_THROW(renderer->set_frame_constants(constants.data(), Renderer::FRAME_CONSTANTS_SIZE + 1), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->set_frame_constants(nullptr, 16), std::invalid_argument);
	renderer->set_global_texture(0, texture);
	BOOST_CHECK_THROW(renderer->set_global_texture(Renderer::GLOBAL_TEXTURES_COUNT, texture), std::out_of_range);
	renderer->batch_download_textures({texture});
	BOOST_CHECK_THROW(renderer->batch_download_textures({texture, TexturePtr()}), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->batch_upload_textures({texture, texture}), std::invalid_argument);

	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->set_frame_constants(constants.data(), constants.size()), std::logic_error);
	BOOST_CHECK_THROW(renderer->set_global_texture(0, texture), std::logic_error);
	BOOST_CHECK_THROW(renderer->batch_download_textures({texture}), std::logic_error);
	renderer->begin_pass(renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8));
	BOOST_CHECK_THROW(renderer->stream_textures({texture}), std::logic_error);
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_viewport_is_a_rectangle_of_the_pass_s_target, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, 4, 4), std::logic_error);
	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, 4, 4), std::logic_error);
	renderer->begin_pass(renderer->create_render_target(16, 8, PixelFormat::A8R8G8B8));
	renderer->set_viewport(0, 0, 16, 8);
	renderer->set_viewport(8, 4, 8, 4);
	BOOST_CHECK_THROW(renderer->set_viewport(9, 0, 8, 8), std::out_of_range);
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, 16, 9), std::out_of_range);
	BOOST_CHECK_THROW(renderer->set_viewport(-1, 0, 4, 4), std::out_of_range);
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, 0, 4), std::out_of_range);
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_draw_is_in_a_pass, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	VertexBufferPtr const vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);
	auto vb_mapping = vb->write(Contents::DISCARD);
	std::memset(vb_mapping.data(), 0, sizeof(VERTEX_PT) * 3);
	vb_mapping.unmap();
	DrawCallPtr const draw = renderer->create_draw_call();
	draw->set_vertex_buffer(vb);
	draw->set_material(renderer->create_material());
	draw->set_camera(std::make_shared<Camera>());
	draw->set_triangles_count(1);
	draw->set_always_visible(true);
	draw->commit();
	BOOST_CHECK_THROW(draw->execute(), std::logic_error);
	renderer->begin_frame();
	BOOST_CHECK_THROW(draw->execute(), std::logic_error);
	renderer->begin_pass(renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8));
	draw->execute();
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(what_a_frame_draws_with_lives_until_the_frame_is_submitted, F, AllBackends)
{
	// a target, a material and its texture, buffers whose last owner lets them go inside the frame: the frame still draws with them
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);
	auto vb_mapping = vb->write(Contents::DISCARD);
	std::memset(vb_mapping.data(), 0, sizeof(VERTEX_PT) * 3);
	vb_mapping.unmap();
	MaterialPtr material = renderer->create_material();
	TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	std::memset(texture_mapping.data(), 255, 4 * 4 * 4);
	texture_mapping.unmap();
	material->set_diffuse_texture(texture);
	DrawCallPtr draw = renderer->create_draw_call();
	draw->set_vertex_buffer(vb);
	draw->set_material(material);
	draw->set_camera(std::make_shared<Camera>());
	draw->set_triangles_count(1);
	draw->commit();

	renderer->begin_frame();
	renderer->begin_pass(renderer->create_render_target(16, 8, PixelFormat::A8R8G8B8));
	draw->execute();
	draw.reset();
	material.reset();
	texture.reset();
	vb.reset();
	renderer->end_pass();
	renderer->finish_frame();
	BOOST_CHECK(!renderer->in_frame());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_texture_without_its_cpu_copy_is_written_once, F, AllBackends)
{
	// TextureUsage::keep_cpu_copy false: the texels go to the GPU at the first unlock and the copy with them
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	TextureUsage usage;
	usage.keep_cpu_copy = false;
	TexturePtr const texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, usage);
	auto texture_mapping = texture->write(Contents::DISCARD);
	std::memset(texture_mapping.data(), 255, 4 * 4 * 4);
	texture_mapping.unmap();
	BOOST_CHECK_THROW((void)texture->write(Contents::DISCARD), std::logic_error);
	BOOST_CHECK_THROW((void)texture->read(), std::logic_error);
	BOOST_CHECK_THROW(texture->download(), std::logic_error);
	// and one that keeps it is written and read again
	TexturePtr const kept = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, TextureUsage());
	auto kept_mapping = kept->write(Contents::DISCARD);
	std::memset(kept_mapping.data(), 7, 4 * 4 * 4);
	kept_mapping.unmap();
	BOOST_CHECK_EQUAL(std::as_const(*kept).read().as<unsigned char const>()[5], 7);
}

BOOST_AUTO_TEST_CASE(a_shader_program_is_loaded_by_its_name_from_a_directory)
{
	// <dir>/<name>.vert.spv and <name>.frag.spv, as edge_add_shaders makes them: there, or an error naming what is not
	NullFixture fixture;
	std::filesystem::path const dir = std::filesystem::path(EDGE_TEST_SHADER_DIR);
	ShaderProgramPtr const program = fixture.renderer->load_shader_program(dir, "test_vertex_color");
	BOOST_CHECK_EQUAL(program->vertex_shader_filename(), (dir / "test_vertex_color.vert.spv").string());
	BOOST_CHECK_EQUAL(program->fragment_shader_filename(), (dir / "test_vertex_color.frag.spv").string());
	try
	{
		fixture.renderer->load_shader_program(dir, "test_inverse_color");      // a fragment shader and no vertex one
		BOOST_ERROR("a program of a vertex shader that is not there was loaded");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("test_inverse_color.vert.spv") != std::string::npos, e.what());
	}
	BOOST_CHECK_THROW(fixture.renderer->load_shader_program(dir / "nowhere", "test_vertex_color"), std::runtime_error);
}

/// A pass is into a target of this renderer: another renderer's (of the same backend or another, a layer of one, its default target) is
/// std::invalid_argument, and the renderer is left as it was, in its frame and out of a pass.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_pass_into_another_renderer_s_target_is_refused, F, AllBackends)
{
	F fixture, other;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_REQUIRE_MESSAGE(other.renderer, "renderer unavailable: " << other.error);
	RenderTargetFormat layered;
	layered.color_formats = {PixelFormat::A8R8G8B8};
	layered.layers = 2;
	std::vector<RenderTargetPtr> const theirs = {other.renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8),
		other.renderer->create_render_target(8, 8, layered)->layer(1), other.renderer->default_render_target(),
		std::make_shared<NullRenderer>(16, 16)->create_render_target(8, 8, PixelFormat::A8R8G8B8)};
	renderer->begin_frame();
	for (size_t i = 0; i < theirs.size(); ++i)
	{
		BOOST_CHECK_THROW(renderer->begin_pass(theirs[i]), std::invalid_argument);
		BOOST_CHECK_MESSAGE(renderer->in_frame() && !renderer->in_pass(), "target " << i);
	}
	renderer->begin_pass(renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8));         // its own still
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_SUITE_END()
