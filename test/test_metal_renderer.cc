// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// What is the Metal renderer's own (the renderer contract, drawing, textures, targets and timing it shares with the other backends are
// held by the tests that run on every one of them, test_backends.h): that nothing the CPU writes reaches a frame already given to
// the GPU, that a frame's uniforms stay apart however many there are, that what a shader lacks reads as GL's defaults, what is refused
// (a shader without its library, another renderer's things, a window that is not there), and that the frames in flight end well.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "image.h"
#include "index.h"
#include "material.h"
#include "metal_renderer.h"
#include "pixel.h"
#include "shader_program.h"
#include "test_fixture.h"
#include "test_metal_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

using Rgba = rove::rgba<float>;

CameraPtr view()
{
	CameraPtr camera = std::make_shared<Camera>();
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

/// A quad over the whole target (or over a part of it: @p x0..x1, @p y0..y1 in clip space) with texture coordinates 0 to 1.
VertexBufferPtr quad_buffer(RendererPtr const &renderer, float x0 = -1, float y0 = -1, float x1 = 1, float y1 = 1)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	float const xy[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
	for (int k = 0; k < 4; ++k)
	{
		v[k].position = rove::vec<3>(xy[k][0], xy[k][1], 0);
		v[k].texcoords = rove::vec<2>(uv[k][0], uv[k][1]);
	}
	vb_mapping.unmap();
	return vb;
}

IndexBufferPtr quad_indices(RendererPtr const &renderer)
{
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *i = ib_mapping.as<INDEX_U16>().data();
	INDEX_U16 const order[6] = {0, 1, 2, 0, 2, 3};
	std::copy(order, order + 6, i);
	ib_mapping.unmap();
	return ib;
}

DrawCallPtr quad_draw(RendererPtr const &renderer, VertexBufferPtr const &vb, IndexBufferPtr const &ib, MaterialPtr const &material)
{
	DrawCallPtr draw = renderer->create_draw_call();
	draw->set_vertex_buffer(vb);
	draw->set_index_buffer(ib);
	draw->set_material(material);
	draw->set_camera(view());
	draw->set_triangles_count(2);
	draw->set_always_visible(true);
	draw->commit();
	return draw;
}

MaterialPtr material_of(RendererPtr const &renderer, char const *vertex, char const *fragment, bool blend = false)
{
	MaterialPtr m = renderer->create_material();
	m->set_cull_mode(Material::CullMode::NONE);
	m->set_shader_program(renderer->create_shader_program(test_shader(vertex), test_shader(fragment)));
	if (blend)
	{
		m->set_alpha_blend_enabled(true);
		m->set_src_blend_mode(Material::BlendMode::SRC_ALPHA);
		m->set_dest_blend_mode(Material::BlendMode::INV_SRC_ALPHA);
	}
	return m;
}

void fill_colored(VertexBufferPtr const &vb, rove::vec<4> const &color, float offset)
{
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PTBNTC *v = vb_mapping.as<VERTEX_PTBNTC>().data();
	float const xs[4] = {-1, 1, 1, -1}, ys[4] = {-1, -1, 1, 1};
	for (int k = 0; k < 4; ++k)
	{
		std::memset(&v[k], 0, sizeof(v[k]));
		v[k].position = rove::vec<3>(xs[k] + offset, ys[k], 0.0f);
		v[k].normal = rove::vec<3>(0, 0, 1);
		v[k].tangent = rove::vec<3>(1, 0, 0);
		v[k].binormal = rove::vec<3>(0, 1, 0);
		v[k].color.set(color.x, color.y, color.z, color.w);
	}
	vb_mapping.unmap();
}

/// A quad over the whole target (shifted along x by @p offset) of one color, vertex 10 of a draw by test_vertex_color.vert.
VertexBufferPtr colored_quad(RendererPtr const &renderer, rove::vec<4> const &color, float offset = 0.0f)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNTC>(), sizeof(VERTEX_PTBNTC) * 4);
	fill_colored(vb, color, offset);
	return vb;
}

TexturePtr colored_texture(RendererPtr const &renderer, uint32_t bgra)
{
	TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	uint32_t *texels = texture_mapping.as<uint32_t>().data();
	std::fill(texels, texels + 16, bgra);
	texture_mapping.unmap();
	return texture;
}

/// Passes over a big target that take the GPU a while (a few ms): the CPU is ahead of the GPU after it hands them over.
void keep_the_gpu_busy(RendererPtr const &renderer, RenderTargetPtr const &big, DrawCallPtr const &blended)
{
	renderer->begin_pass(big);
	for (int i = 0; i < 300; ++i)
		blended->execute();
	renderer->end_pass();
}

Rgba pixel_of(RenderTargetPtr const &target, int x, int y)
{
	Image<Rgba> const image = read_image<Rgba>(*target->color_texture());
	return image.at(x, y);
}

struct Bgra { uint8_t b, g, r, a; };

Bgra bgra_pixel_of(RenderTargetPtr const &target, int x, int y)
{
	TexturePtr color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	Bgra const *p = color_mapping.as<Bgra const>().data();
	Bgra pixel = p[y * color->width() + x];
	color_mapping.unmap();
	return pixel;
}

}

BOOST_AUTO_TEST_SUITE(metal_renderer)

BOOST_FIXTURE_TEST_CASE(a_vertex_buffer_rewritten_after_its_frame_was_submitted_does_not_change_that_frame, MetalFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << error);
	RenderTargetPtr const big = renderer->create_render_target(2048, 2048, PixelFormat::A8R8G8B8);
	RenderTargetPtr const small = renderer->create_render_target(16, 16, PixelFormat::A8R8G8B8);
	// 300 translucent squares over 2048 x 2048, then the small pass: it is drawn some milliseconds after the CPU has given it away
	MaterialPtr const blended = material_of(renderer, "test_vertex_color.vert", "test_vertex_color.frag", true);
	VertexBufferPtr const heavy_vb = colored_quad(renderer, rove::vec<4>(1, 1, 1, 0.01f));
	DrawCallPtr const heavy = quad_draw(renderer, heavy_vb, quad_indices(renderer), blended);

	MaterialPtr const red = material_of(renderer, "test_vertex_color.vert", "test_vertex_color.frag");
	VertexBufferPtr const vb = colored_quad(renderer, rove::vec<4>(1, 0, 0, 1));
	DrawCallPtr const small_draw = quad_draw(renderer, vb, quad_indices(renderer), red);

	renderer->begin_frame();
	keep_the_gpu_busy(renderer, big, heavy);
	renderer->begin_pass(small);
	small_draw->execute();
	renderer->end_pass();
	renderer->submit_frame();
	fill_colored(vb, rove::vec<4>(0, 1, 0, 1), 0.0f);        // the CPU is on to the next frame; the GPU has not drawn this one
	renderer->begin_frame();
	renderer->finish_frame();
	Bgra const p = bgra_pixel_of(small, 8, 8);
	BOOST_CHECK_MESSAGE(p.r == 255 && p.g == 0, "the pixel is (r" << int(p.r) << " g" << int(p.g) << " b" << int(p.b) << "): the later write got in");
}

BOOST_FIXTURE_TEST_CASE(a_texture_rewritten_after_its_frame_was_submitted_does_not_change_that_frame, MetalFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << error);
	RenderTargetPtr const big = renderer->create_render_target(2048, 2048, PixelFormat::A8R8G8B8);
	RenderTargetPtr const small = renderer->create_render_target(16, 16, PixelFormat::A8R8G8B8);
	MaterialPtr const blended = material_of(renderer, "test_vertex_color.vert", "test_vertex_color.frag", true);
	VertexBufferPtr const heavy_vb = colored_quad(renderer, rove::vec<4>(1, 1, 1, 0.01f));
	IndexBufferPtr const ib = quad_indices(renderer);
	DrawCallPtr const heavy = quad_draw(renderer, heavy_vb, ib, blended);

	TexturePtr const texture = colored_texture(renderer, 0xFFFF0000u);       // B, G, R, A: red
	MaterialPtr const sampled = material_of(renderer, "test_frame.vert", "test_sample.frag");
	sampled->set_diffuse_texture(texture);
	DrawCallPtr const small_draw = quad_draw(renderer, quad_buffer(renderer), ib, sampled);

	renderer->begin_frame();
	keep_the_gpu_busy(renderer, big, heavy);
	renderer->begin_pass(small);
	small_draw->execute();
	renderer->end_pass();
	renderer->submit_frame();
	auto texture_mapping = texture->write(Contents::DISCARD);
	uint32_t *texels = texture_mapping.as<uint32_t>().data();
	std::fill(texels, texels + 16, 0xFF00FF00u);        // green
	texture_mapping.unmap();
	renderer->begin_frame();
	renderer->finish_frame();
	Bgra const p = bgra_pixel_of(small, 8, 8);
	BOOST_CHECK_MESSAGE(p.r == 255 && p.g == 0, "the pixel is (r" << int(p.r) << " g" << int(p.g) << " b" << int(p.b) << "): the later write got in");

	// and the next frame draws what was written
	renderer->begin_frame();
	renderer->begin_pass(small);
	small_draw->execute();
	renderer->end_pass();
	renderer->finish_frame();
	Bgra const q = bgra_pixel_of(small, 8, 8);
	BOOST_CHECK_MESSAGE(q.g == 255 && q.r == 0, "the next frame draws (r" << int(q.r) << " g" << int(q.g) << " b" << int(q.b) << ")");
}

BOOST_FIXTURE_TEST_CASE(thousands_of_draws_in_a_frame_each_read_their_own_uniforms, MetalFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << error);
	// 70 x 70 draws, each into a pixel of its own by a viewport, each with a material block of its own: the draws' blocks (a kilobyte
	// each) and constants add up to more than one chunk of the arena they are written into
	int const side = 70;
	RenderTargetPtr const target = renderer->create_render_target(side, side, PixelFormat::R32G32B32A32F);
	VertexBufferPtr const vb = quad_buffer(renderer);
	IndexBufferPtr const ib = quad_indices(renderer);
	std::vector<DrawCallPtr> draws;
	for (int i = 0; i < side * side; ++i)
	{
		MaterialPtr m = material_of(renderer, "test_frame.vert", "test_material_parameters.frag");
		m->set_extra_parameter(0, rove::vec<4>(3, 0, 0, 0));
		std::vector<float> block(Material::PARAMETERS_SIZE / 4, 0.0f);
		for (int k = 0; k < 4; ++k)
			block[12 + k] = float(i) + 0.25f * float(k);
		m->set_parameters(block.data(), block.size() * sizeof(float));
		draws.push_back(quad_draw(renderer, vb, ib, m));
	}
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	for (int i = 0; i < side * side; ++i)
	{
		renderer->set_viewport(i % side, i / side, 1, 1);
		draws[i]->execute();
	}
	renderer->end_pass();
	renderer->finish_frame();
	Image<Rgba> const image = read_image<Rgba>(*target->color_texture());
	int wrong = 0;
	for (int i = 0; i < side * side; ++i)
	{
		Rgba const p = image.at(i % side, i / side);
		if (p.r != float(i) || p.g != float(i) + 0.25f || p.b != float(i) + 0.5f || p.a != float(i) + 0.75f)
			++wrong;
	}
	BOOST_CHECK_EQUAL(wrong, 0);
}

BOOST_FIXTURE_TEST_CASE(an_attribute_the_vertex_lacks_reads_as_gl_has_it_zeros_and_a_one, MetalFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << error);
	RenderTargetPtr const target = renderer->create_render_target(8, 8, PixelFormat::R32G32B32A32F);
	// test_vertex_color.vert reads a color at location 10, which a position-only vertex does not have
	VertexBufferPtr const vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_P>(), sizeof(VERTEX_P) * 3);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_P *v = vb_mapping.as<VERTEX_P>().data();
	v[0].position = rove::vec<3>(-1, -1, 0);
	v[1].position = rove::vec<3>(3, -1, 0);
	v[2].position = rove::vec<3>(-1, 3, 0);
	vb_mapping.unmap();
	MaterialPtr const m = material_of(renderer, "test_vertex_color.vert", "test_vertex_color.frag");
	DrawCallPtr draw = renderer->create_draw_call();
	draw->set_vertex_buffer(vb);
	draw->set_material(m);
	draw->set_camera(view());
	draw->set_triangles_count(1);
	draw->set_always_visible(true);
	draw->commit();
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	draw->execute();
	renderer->end_pass();
	renderer->finish_frame();
	Rgba const p = pixel_of(target, 4, 4);
	BOOST_CHECK_MESSAGE(p.r == 0 && p.g == 0 && p.b == 0 && p.a == 1, "(" << p.r << " " << p.g << " " << p.b << " " << p.a << ")");
}

BOOST_FIXTURE_TEST_CASE(a_shader_without_its_translation_is_refused_naming_the_file, MetalFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << error);
	RenderTargetPtr const target = renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8);
	MaterialPtr const m = renderer->create_material();
	m->set_shader_program(renderer->create_shader_program("no_such_shader.vert.spv", "no_such_shader.frag.spv"));
	DrawCallPtr const draw = quad_draw(renderer, quad_buffer(renderer), quad_indices(renderer), m);
	renderer->begin_frame();
	renderer->begin_pass(target);
	try
	{
		draw->execute();
		BOOST_ERROR("a draw with a shader that is not there");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("no_such_shader.vert.msl.map") != std::string::npos, e.what());
	}
	renderer->end_pass();
	renderer->finish_frame();
	// GLSL ES sources are another backend's: a name that is not SPIR-V is not a Metal program
	MaterialPtr const glsl = renderer->create_material();
	glsl->set_shader_program(renderer->create_shader_program("x.vert", "x.frag"));
	DrawCallPtr const bad = quad_draw(renderer, quad_buffer(renderer), quad_indices(renderer), glsl);
	renderer->begin_frame();
	renderer->begin_pass(target);
	BOOST_CHECK_THROW(bad->execute(), std::invalid_argument);
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE(the_things_of_another_renderer_are_refused)
{
	MetalFixture one, other;
	BOOST_REQUIRE_MESSAGE(one.renderer && other.renderer, "renderer unavailable: " << one.error << other.error);
	RenderTargetPtr const foreign_target = other.renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8);
	RenderTargetPtr const target = one.renderer->create_render_target(8, 8, PixelFormat::A8R8G8B8);
	one.renderer->begin_frame();
	BOOST_CHECK_THROW(one.renderer->begin_pass(foreign_target), std::invalid_argument);
	one.renderer->begin_pass(target);
	MaterialPtr const m = material_of(one.renderer, "test_vertex_color.vert", "test_vertex_color.frag");
	VertexBufferPtr const foreign_vb = colored_quad(other.renderer, rove::vec<4>(1, 0, 0, 1));
	DrawCallPtr const draw = quad_draw(one.renderer, foreign_vb, quad_indices(one.renderer), m);
	BOOST_CHECK_THROW(draw->execute(), std::invalid_argument);
	one.renderer->end_pass();
	one.renderer->finish_frame();
	BOOST_CHECK_THROW(one.renderer->set_global_texture(0, colored_texture(other.renderer, 0xFFFFFFFFu)), std::invalid_argument);
}

BOOST_FIXTURE_TEST_CASE(showing_a_frame_needs_the_hosts_window, MetalFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << error);
	BOOST_CHECK(!renderer->context()->hosted());
	std::vector<uint8_t> const frame(4 * 4 * 4, 0);
	BOOST_CHECK_THROW(renderer->show_frame(frame.data(), 4, 4, 8, 8), std::logic_error);
	BOOST_CHECK_THROW(renderer->show_frame(colored_texture(renderer, 0xFFFFFFFFu), 8, 8), std::logic_error);
	BOOST_CHECK_THROW(renderer->swap_window(), std::logic_error);
	BOOST_CHECK_THROW(MetalContext(static_cast<void *>(nullptr)), std::invalid_argument);
}

BOOST_FIXTURE_TEST_CASE(the_gpu_timer_reads_in_microseconds_of_the_clock_the_frame_ran_by, MetalFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << error);
	RenderTargetPtr const big = renderer->create_render_target(2048, 2048, PixelFormat::A8R8G8B8);
	MaterialPtr const blended = material_of(renderer, "test_vertex_color.vert", "test_vertex_color.frag", true);
	VertexBufferPtr const vb = colored_quad(renderer, rove::vec<4>(1, 1, 1, 0.01f));
	DrawCallPtr const heavy = quad_draw(renderer, vb, quad_indices(renderer), blended);
	renderer->set_gpu_trace_enabled(true);
	renderer->begin_frame();
	uint64_t const frame = renderer->frame_serial();
	keep_the_gpu_busy(renderer, big, heavy);
	auto const begun = std::chrono::steady_clock::now();
	renderer->finish_frame();
	double const wall_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begun).count();
	std::optional<std::vector<double>> const times = renderer->pass_times_us(frame);
	BOOST_REQUIRE_MESSAGE(times, "no durations after finish_frame()");
	BOOST_REQUIRE_EQUAL(times->size(), 1u);
	// the frame was handed over a moment before the wait began: the pass took at most what the wait did and some of it
	BOOST_CHECK_MESSAGE((*times)[0] > 0.2 * wall_us && (*times)[0] < 1.5 * wall_us + 2000.0,
		"the pass took " << (*times)[0] << " us on the GPU's clock, the wait for it " << wall_us << " us on the CPU's");
}

BOOST_FIXTURE_TEST_CASE(many_frames_in_flight_are_drawn_in_order_and_a_renderer_dropped_with_some_ends_well, MetalFixture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << error);
	RenderTargetPtr const target = renderer->create_render_target(16, 16, PixelFormat::A8R8G8B8);
	MaterialPtr const m = material_of(renderer, "test_vertex_color.vert", "test_vertex_color.frag");
	VertexBufferPtr const vb = colored_quad(renderer, rove::vec<4>(0, 0, 0, 1));
	IndexBufferPtr const ib = quad_indices(renderer);
	DrawCallPtr const draw = quad_draw(renderer, vb, ib, m);
	// 40 frames submitted one after the other, each a different color; the last one is what the target holds
	for (int i = 0; i < 40; ++i)
	{
		fill_colored(vb, rove::vec<4>(float(i) / 255.0f, 0, 0, 1), 0.0f);
		renderer->begin_frame();
		renderer->begin_pass(target);
		draw->execute();
		renderer->end_pass();
		renderer->submit_frame();
	}
	BOOST_CHECK_EQUAL(bgra_pixel_of(target, 8, 8).r, 39);       // (a download waits for what was submitted)
	// and one dropped with frames in flight: the renderer waits for them, nothing is left running on what it freed
	{
		MetalFixture short_lived;
		BOOST_REQUIRE(short_lived.renderer);
		RenderTargetPtr const t = short_lived.renderer->create_render_target(256, 256, PixelFormat::A8R8G8B8);
		for (int i = 0; i < 3; ++i)
		{
			short_lived.renderer->begin_frame();
			short_lived.renderer->begin_pass(t);
			short_lived.renderer->end_pass();
			short_lived.renderer->submit_frame();
		}
	}
}

BOOST_AUTO_TEST_CASE(a_renderer_dropped_inside_a_frame_or_a_pass_does_not_wait_for_ever)
{
	// an exception in a frame leaves it open (the host's stack unwinds, the renderer goes with it): the destructor has the frame ended and
	// waits for the GPU, not for a frame that was never handed over
	for (bool in_pass : {false, true})
	{
		std::atomic<bool> done{false};
		std::thread dropper([&, in_pass] {
			{
				MetalFixture fixture;
				if (fixture.renderer)
				{
					RenderTargetPtr const target = fixture.renderer->create_render_target(16, 16, PixelFormat::A8R8G8B8);
					fixture.renderer->begin_frame();
					if (in_pass)
						fixture.renderer->begin_pass(target);
				}
			}
			done = true;
		});
		for (int i = 0; i < 100 && !done; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		BOOST_REQUIRE_MESSAGE(done, "a renderer dropped " << (in_pass ? "in a pass" : "in a frame") << " waits for ever");
		dropper.join();
	}
}

BOOST_AUTO_TEST_SUITE_END()
