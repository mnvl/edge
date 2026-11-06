// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the GLES renderer as a Renderer: what a pass clears, where a triangle lands in the target's rows (row 0 is the top, as
// Vulkan's), which faces are culled, that the depth test orders draws, that a texture comes back as it was written, and what the
// renderer refuses.  Every check goes through the Renderer interface and reads the pixels back; none looks at GL.

#include <boost/test/unit_test.hpp>
#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "camera.h"
#include "draw_call.h"
#include "gles_renderer.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "gles_sdl_context.h"
#include "shader_program.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

int const SIZE = 64;

struct Bgra
{
	uint8_t b, g, r, a;
};

bool operator==(Bgra const &x, Bgra const &y) { return x.b == y.b && x.g == y.g && x.r == y.r && x.a == y.a; }
std::ostream &operator<<(std::ostream &s, Bgra const &p)
{
	return s << "(b" << int(p.b) << " g" << int(p.g) << " r" << int(p.r) << " a" << int(p.a) << ")";
}

Bgra const RED = {0, 0, 255, 255};
Bgra const GREEN = {0, 255, 0, 255};
Bgra const BLUE = {255, 0, 0, 255};
Bgra const GRAY = {64, 64, 64, 255};

struct GlesFixture
{
	std::shared_ptr<GLESRenderer> renderer;

	GlesFixture()
	{
		// headless: SDL's offscreen video driver makes the context with no display
		if (!std::getenv("SDL_VIDEO_DRIVER"))
			setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
		renderer = std::make_shared<GLESRenderer>(SIZE, SIZE, std::string(EDGE_ROOT) + "/test/shaders", std::make_shared<GLESSDLContext>());
	}
};

/// A triangle wide at the bottom with its apex at the top, counter-clockwise as the target is looked at, at depth z (the eye is at z = 1
/// looking at the origin: a smaller z is nearer).
VertexBufferPtr triangle(RendererPtr const &renderer, float z, bool counter_clockwise = true)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_P>(), sizeof(VERTEX_P) * 3);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_P *v = vb_mapping.as<VERTEX_P>().data();
	BOOST_REQUIRE(v);
	v[0].position = rove::vec<3>(-0.9f, -0.9f, z);
	v[1].position = rove::vec<3>(counter_clockwise ? 0.9f : 0.0f, counter_clockwise ? -0.9f : 0.9f, z);
	v[2].position = rove::vec<3>(counter_clockwise ? 0.0f : 0.9f, counter_clockwise ? 0.9f : -0.9f, z);
	vb_mapping.unmap();
	return vb;
}

CameraPtr camera()
{
	CameraPtr c(new Camera());
	c->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	c->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return c;
}

DrawCallPtr draw_of(RendererPtr const &renderer, VertexBufferPtr const &vb, Bgra color, Material::CullMode cull)
{
	MaterialPtr material = renderer->create_material();
	material->set_cull_mode(cull);
	material->set_base_color_factor(rove::vec<4>(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f));
	material->set_shader_program(renderer->create_shader_program(
		"test_gles_triangle.es.vert", "test_gles_triangle.es.frag"));
	DrawCallPtr draw = renderer->create_draw_call();
	draw->set_vertex_buffer(vb);
	draw->set_material(material);
	draw->set_camera(camera());
	draw->set_triangles_count(1);
	draw->set_always_visible(true);
	draw->commit();
	return draw;
}

RenderPassClearState clear_to(Bgra c)
{
	RenderPassClearState clear;
	clear.clear_color_r = c.r / 255.0f;
	clear.clear_color_g = c.g / 255.0f;
	clear.clear_color_b = c.b / 255.0f;
	clear.clear_color_a = c.a / 255.0f;
	return clear;
}

/// Draws into a fresh target (cleared to gray) and returns its pixels.
std::vector<Bgra> render(RendererPtr const &renderer, std::vector<DrawCallPtr> const &draws)
{
	RenderTargetPtr target = renderer->create_render_target(SIZE, SIZE, PixelFormat::A8R8G8B8);
	renderer->begin_frame();
	renderer->begin_pass(target, clear_to(GRAY));
	for (auto const &draw : draws)
		draw->execute();
	renderer->end_pass();
	renderer->finish_frame();
	TexturePtr color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	Bgra const *p = color_mapping.as<Bgra const>().data();
	BOOST_REQUIRE(p);
	std::vector<Bgra> pixels(p, p + SIZE * SIZE);
	color_mapping.unmap();
	return pixels;
}

Bgra at(std::vector<Bgra> const &pixels, int x, int y) { return pixels.at(y * SIZE + x); }

}

BOOST_AUTO_TEST_SUITE(gles_renderer)

BOOST_FIXTURE_TEST_CASE(a_pass_clears_its_target, GlesFixture)
{
	auto pixels = render(renderer, {});
	for (int y : {0, SIZE / 2, SIZE - 1})
		for (int x : {0, SIZE / 2, SIZE - 1})
			BOOST_TEST(at(pixels, x, y) == GRAY);
}

BOOST_FIXTURE_TEST_CASE(a_float_target_is_cleared_and_read_back_as_floats, GlesFixture)
{
	RenderTargetPtr target = renderer->create_render_target(8, 4, PixelFormat::R32G32B32A32F);
	RenderPassClearState clear;
	clear.clear_color_r = 0.25f;
	clear.clear_color_g = 2.5f;      // above 1: a float target keeps it
	clear.clear_color_b = -1.0f;
	clear.clear_color_a = 0.5f;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	renderer->end_pass();
	renderer->finish_frame();
	TexturePtr color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	float const *p = color_mapping.as<float const>().data();
	BOOST_REQUIRE(p);
	for (int i = 0; i < 8 * 4; ++i)
	{
		BOOST_TEST(p[4 * i + 0] == 0.25f);
		BOOST_TEST(p[4 * i + 1] == 2.5f);
		BOOST_TEST(p[4 * i + 2] == -1.0f);
		BOOST_TEST(p[4 * i + 3] == 0.5f);
	}
	color_mapping.unmap();
}

BOOST_FIXTURE_TEST_CASE(a_triangle_lands_with_its_apex_in_the_top_rows, GlesFixture)
{
	auto pixels = render(renderer, {draw_of(renderer, triangle(renderer, 0.0f), RED, Material::CullMode::NONE)});
	BOOST_TEST(at(pixels, SIZE / 2, 8) == RED);        // near the apex
	BOOST_TEST(at(pixels, SIZE / 2, SIZE - 8) == RED); // the wide base
	BOOST_TEST(at(pixels, 8, SIZE - 8) == RED);        // the base's left end
	BOOST_TEST(at(pixels, SIZE - 9, SIZE - 8) == RED); // and its right end
	BOOST_TEST(at(pixels, 8, 8) == GRAY);              // beside the apex, left
	BOOST_TEST(at(pixels, SIZE - 9, 8) == GRAY);       // and right
	BOOST_TEST(at(pixels, SIZE / 2, 1) == GRAY);       // above the apex
	BOOST_TEST(at(pixels, SIZE / 2, SIZE - 1) == GRAY);// below the base
}

BOOST_FIXTURE_TEST_CASE(culling_keeps_the_faces_the_material_names, GlesFixture)
{
	auto drawn = [&](bool counter_clockwise, Material::CullMode cull)
	{
		auto pixels = render(renderer, {draw_of(renderer, triangle(renderer, 0.0f, counter_clockwise), RED, cull)});
		return at(pixels, SIZE / 2, SIZE - 8) == RED;
	};
	// CullMode::CCW keeps what runs counter-clockwise on the screen (glTF's front faces), CullMode::CW the others
	BOOST_TEST(drawn(true, Material::CullMode::CCW));
	BOOST_TEST(!drawn(false, Material::CullMode::CCW));
	BOOST_TEST(!drawn(true, Material::CullMode::CW));
	BOOST_TEST(drawn(false, Material::CullMode::CW));
	BOOST_TEST(drawn(true, Material::CullMode::NONE));
	BOOST_TEST(drawn(false, Material::CullMode::NONE));
}

BOOST_FIXTURE_TEST_CASE(the_nearer_triangle_wins_whichever_is_drawn_first, GlesFixture)
{
	VertexBufferPtr near_triangle = triangle(renderer, 0.5f), far_triangle = triangle(renderer, -0.5f);
	auto near_draw = draw_of(renderer, near_triangle, RED, Material::CullMode::NONE);
	auto far_draw = draw_of(renderer, far_triangle, GREEN, Material::CullMode::NONE);
	BOOST_TEST(at(render(renderer, {near_draw, far_draw}), SIZE / 2, SIZE - 8) == RED);
	BOOST_TEST(at(render(renderer, {far_draw, near_draw}), SIZE / 2, SIZE - 8) == RED);
}

BOOST_FIXTURE_TEST_CASE(a_triangle_outside_the_depth_range_is_clipped, GlesFixture)
{
	// the camera sees z from 0 to 2 in front of the eye at z = 1: z = 1.5 is behind the eye's near plane, z = -1.5 beyond the far one
	for (float z : {1.5f, -1.5f})
		BOOST_TEST(at(render(renderer, {draw_of(renderer, triangle(renderer, z), RED, Material::CullMode::NONE)}), SIZE / 2, SIZE - 8) == GRAY);
}

BOOST_FIXTURE_TEST_CASE(a_viewport_confines_the_draws_to_its_rectangle_from_the_top_left, GlesFixture)
{
	RenderTargetPtr target = renderer->create_render_target(SIZE, SIZE, PixelFormat::A8R8G8B8);
	auto red = draw_of(renderer, triangle(renderer, 0.0f), RED, Material::CullMode::NONE);
	auto green = draw_of(renderer, triangle(renderer, 0.0f), GREEN, Material::CullMode::NONE);
	renderer->begin_frame();
	renderer->begin_pass(target, clear_to(GRAY));
	renderer->set_viewport(0, 0, SIZE / 2, SIZE / 2);                  // the top left quarter
	red->execute();
	renderer->set_viewport(SIZE / 2, SIZE / 2, SIZE / 2, SIZE / 2);    // the bottom right one
	green->execute();
	renderer->end_pass();
	renderer->finish_frame();
	TexturePtr color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	Bgra const *p = color_mapping.as<Bgra const>().data();
	std::vector<Bgra> pixels(p, p + SIZE * SIZE);
	color_mapping.unmap();
	// the triangle's base, shrunk into each quarter
	BOOST_TEST(at(pixels, SIZE / 4, SIZE / 2 - 4) == RED);
	BOOST_TEST(at(pixels, 3 * SIZE / 4, SIZE - 4) == GREEN);
	// and nothing outside them
	BOOST_TEST(at(pixels, SIZE / 4, SIZE / 2 + 4) == GRAY);
	BOOST_TEST(at(pixels, SIZE / 2 + 4, SIZE / 2 - 4) == GRAY);
	BOOST_TEST(at(pixels, SIZE / 4, SIZE - 4) == GRAY);
	BOOST_TEST(at(pixels, 3 * SIZE / 4, SIZE / 2 - 4) == GRAY);
	BOOST_TEST(at(pixels, 3 * SIZE / 4, SIZE / 2 + 4) == GREEN);
}

BOOST_FIXTURE_TEST_CASE(alpha_blending_mixes_with_what_is_there, GlesFixture)
{
	auto blended = [&](bool preserve_destination_alpha)
	{
		VertexBufferPtr vb = triangle(renderer, 0.0f);
		auto draw = draw_of(renderer, vb, Bgra{0, 0, 255, 128}, Material::CullMode::NONE);
		MaterialPtr material = draw->material();
		material->set_alpha_blend_enabled(true);
		material->set_src_blend_mode(Material::BlendMode::SRC_ALPHA);
		material->set_dest_blend_mode(Material::BlendMode::INV_SRC_ALPHA);
		material->set_preserve_destination_alpha(preserve_destination_alpha);
		RenderTargetPtr target = renderer->create_render_target(SIZE, SIZE, PixelFormat::A8R8G8B8);
		renderer->begin_frame();
		renderer->begin_pass(target, clear_to(Bgra{64, 64, 64, 200}));
		draw->execute();
		renderer->end_pass();
		renderer->finish_frame();
		TexturePtr color = target->color_texture();
		color->download();
		auto color_mapping = color->read();
		Bgra const *p = color_mapping.as<Bgra const>().data();
		Bgra const inside = p[(SIZE - 8) * SIZE + SIZE / 2];
		color_mapping.unmap();
		return inside;
	};
	// red at alpha 128/255 over gray 64: red 255 * 0.502 + 64 * 0.498 = 160, green and blue 64 * 0.498 = 32
	Bgra const plain = blended(false);
	BOOST_TEST(std::abs(plain.r - 160) <= 2);
	BOOST_TEST(std::abs(plain.g - 32) <= 2);
	BOOST_TEST(std::abs(plain.b - 32) <= 2);
	BOOST_TEST(std::abs(plain.a - (128 * 0.502 + 200 * 0.498)) <= 2);     // the alpha is blended as a channel
	Bgra const kept = blended(true);
	BOOST_TEST(std::abs(kept.r - 160) <= 2);
	BOOST_TEST(kept.a == 200);                                           // the target's alpha is left as it is
}

BOOST_FIXTURE_TEST_CASE(a_draw_that_does_not_write_depth_is_overdrawn_by_a_farther_one, GlesFixture)
{
	auto near_draw = draw_of(renderer, triangle(renderer, 0.5f), RED, Material::CullMode::NONE);
	near_draw->material()->set_zwrite_enabled(false);
	auto far_draw = draw_of(renderer, triangle(renderer, -0.5f), GREEN, Material::CullMode::NONE);
	BOOST_TEST(at(render(renderer, {near_draw, far_draw}), SIZE / 2, SIZE - 8) == GREEN);
}

BOOST_FIXTURE_TEST_CASE(a_texture_comes_back_as_written, GlesFixture)
{
	// every texel different: a swapped channel or a flipped row shows
	int const W = 5, H = 3;
	std::vector<Bgra> expected;
	for (int i = 0; i < W * H; ++i)
		expected.push_back(Bgra{uint8_t(10 + i), uint8_t(100 + 3 * i), uint8_t(200 - 7 * i), uint8_t(255 - i)});
	TexturePtr texture = renderer->create_texture(W, H, PixelFormat::A8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	std::memcpy(texture_mapping.data(), expected.data(), sizeof(Bgra) * expected.size());
	texture_mapping.unmap();

	// what is written through the CPU view stays on the CPU (Texture::pixels): download() must bring the GPU's texels over it
	auto view = texture->pixels<rove::bgra<unsigned char>>();
	Bgra *cpu = reinterpret_cast<Bgra *>(view.pixels());
	for (int i = 0; i < W * H; ++i)
		cpu[i] = Bgra{1, 2, 3, 4};
	texture->download();
	auto texture_mapping2 = texture->read();
	Bgra const *r = texture_mapping2.as<Bgra const>().data();
	BOOST_REQUIRE(r);
	for (int i = 0; i < W * H; ++i)
		BOOST_TEST(r[i] == expected[i]);
	texture_mapping2.unmap();
}

BOOST_FIXTURE_TEST_CASE(half_float_texels_come_back_as_they_were_written_and_as_rendered, GlesFixture)
{
	// bits of IEEE binary16: 1, -2.5, the largest, the smallest normal, a third, the smallest subnormal, minus zero, pi, both infinities,
	// a half, and a number between
	uint16_t const written[] = {0x3C00, 0xC100, 0x7BFF, 0x0400,  0x3555, 0x0001, 0x8000, 0x4248,  0x7C00, 0xFC00, 0x3800, 0x5640};
	TexturePtr texture = renderer->create_texture(3, 1, PixelFormat::R16G16B16A16F);
	auto texture_mapping = texture->write(Contents::DISCARD);
	std::memcpy(texture_mapping.data(), written, sizeof(written));
	texture_mapping.unmap();
	auto view = texture->pixels<rove::rgba<unsigned short>>();
	std::memset(view.pixels(), 0, sizeof(written));
	texture->download();
	auto read_mapping = texture->read();
	BOOST_TEST(std::memcmp(read_mapping.data(), written, sizeof(written)) == 0);
	read_mapping.unmap();

	// a clear to values with no exact half: the GPU's own conversion, the nearest half or the one next to it
	RenderTargetPtr target = renderer->create_render_target(1, 1, PixelFormat::R16G16B16A16F);
	RenderPassClearState clear;
	clear.clear_color_r = 0.3f;
	clear.clear_color_g = 1e-6f;
	clear.clear_color_b = -100000.0f;    // beyond the largest half: minus infinity
	clear.clear_color_a = 6e-5f;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	renderer->end_pass();
	renderer->finish_frame();
	TexturePtr color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	auto const *h = color_mapping.as<uint16_t const>().data();
	auto const within_one = [](int got, int want) { return std::abs(got - want) <= 1; };
	BOOST_TEST(within_one(h[0], 0x34CD), "0.3: " << h[0]);
	BOOST_TEST(within_one(h[1], 0x0011), "1e-6: " << h[1]);
	BOOST_TEST(within_one(h[2], 0xFC00), "-100000: " << h[2]);      // minus infinity, or the largest negative
	BOOST_TEST(within_one(h[3], 0x03EF), "6e-5: " << h[3]);
	color_mapping.unmap();
}

BOOST_FIXTURE_TEST_CASE(a_target_is_the_size_it_was_made_with, GlesFixture)
{
	RenderTargetPtr target = renderer->create_render_target(12, 7, PixelFormat::A8R8G8B8);
	BOOST_TEST(target->width() == 12);
	BOOST_TEST(target->height() == 7);
	BOOST_TEST(target->color_texture()->width() == 12);
	BOOST_TEST(target->color_texture()->height() == 7);
	BOOST_TEST(renderer->screen_width() == SIZE);
	BOOST_TEST(renderer->default_render_target()->width() == SIZE);
	renderer->resize(20, 10);
	BOOST_TEST(renderer->screen_width() == 20);
	BOOST_TEST(renderer->screen_height() == 10);
	BOOST_TEST(renderer->default_render_target()->width() == 20);
	BOOST_TEST(renderer->default_render_target()->height() == 10);
}

BOOST_FIXTURE_TEST_CASE(the_frame_and_pass_protocol_is_enforced, GlesFixture)
{
	RenderTargetPtr target = renderer->default_render_target();
	auto draw = draw_of(renderer, triangle(renderer, 0.0f), RED, Material::CullMode::NONE);
	BOOST_CHECK_THROW(renderer->begin_pass(target), std::logic_error);      // no frame
	BOOST_CHECK_THROW(renderer->end_pass(), std::logic_error);              // no pass
	BOOST_CHECK_THROW(draw->execute(), std::logic_error);                   // no frame
	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->begin_frame(), std::logic_error);           // already open
	BOOST_CHECK_THROW(draw->execute(), std::logic_error);                   // a frame, no pass
	BOOST_CHECK_THROW(renderer->begin_pass(RenderTargetPtr()), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, 1, 1), std::logic_error);  // outside a pass
	renderer->begin_pass(target);
	BOOST_CHECK_THROW(renderer->begin_pass(target), std::logic_error);      // already in a pass
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, SIZE + 1, 1), std::out_of_range);
	BOOST_CHECK_THROW(renderer->set_viewport(-1, 0, 1, 1), std::out_of_range);
	BOOST_CHECK_THROW(renderer->set_viewport(0, 0, 0, 1), std::out_of_range);
	renderer->end_pass();
	renderer->finish_frame();
	BOOST_CHECK_THROW(renderer->finish_frame(), std::logic_error);           // idle: no frame to finish, on every renderer
	BOOST_CHECK_THROW(draw->execute(), std::logic_error);
}

BOOST_FIXTURE_TEST_CASE(a_material_without_a_shader_program_draws_with_the_built_in_one_and_names_it_where_missing, GlesFixture)
{
	// (this fixture's directory has only the hand-written shaders: the built-in vertex.spv is not translated there)
	MaterialPtr material = renderer->create_material();
	DrawCallPtr draw = renderer->create_draw_call();
	draw->set_vertex_buffer(triangle(renderer, 0.0f));
	draw->set_material(material);
	draw->set_camera(camera());
	draw->set_triangles_count(1);
	draw->set_always_visible(true);
	draw->commit();
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	try
	{
		draw->execute();
		BOOST_FAIL("drew with a built-in shader that is not there");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_TEST(std::string(e.what()).find("vertex.es.") != std::string::npos, e.what());
	}
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_FIXTURE_TEST_CASE(a_shader_that_does_not_compile_names_its_file_and_the_log, GlesFixture)
{
	// a fragment shader with an error in it (a vertex shader given as the fragment one crashes Mesa 25's linker instead)
	MaterialPtr material = renderer->create_material();
	material->set_shader_program(renderer->create_shader_program("test_gles_triangle.es.vert", "test_gles_broken.es.frag"));
	DrawCallPtr draw = renderer->create_draw_call();
	draw->set_vertex_buffer(triangle(renderer, 0.0f));
	draw->set_material(material);
	draw->set_camera(camera());
	draw->set_triangles_count(1);
	draw->set_always_visible(true);
	draw->commit();
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	try
	{
		draw->execute();
		BOOST_FAIL("a fragment shader that does not compile drew");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_TEST(std::string(e.what()).find("test_gles_broken.es.frag") != std::string::npos, e.what());
		BOOST_TEST(std::string(e.what()).find("no_such_colour") != std::string::npos, e.what());   // the compiler's log
	}
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_FIXTURE_TEST_CASE(a_missing_shader_file_is_a_runtime_error_naming_it, GlesFixture)
{
	MaterialPtr material = renderer->create_material();
	material->set_shader_program(renderer->create_shader_program("no_such.es.vert", "test_gles_triangle.es.frag"));
	DrawCallPtr draw = renderer->create_draw_call();
	draw->set_vertex_buffer(triangle(renderer, 0.0f));
	draw->set_material(material);
	draw->set_camera(camera());
	draw->set_triangles_count(1);
	draw->set_always_visible(true);
	draw->commit();
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	try
	{
		draw->execute();
		BOOST_FAIL("a missing shader drew");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_TEST(std::string(e.what()).find("no_such.es.vert") != std::string::npos);
	}
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE(a_frame_is_shown_in_the_hosts_window_stretched_to_it)
{
	if (!std::getenv("SDL_VIDEO_DRIVER"))
		setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
	BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO));
	// a window the host made, of a size of its own
	SDL_Window *window = SDL_CreateWindow("host", 40, 20, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	BOOST_REQUIRE(window);
	{
		GLESRenderer renderer(SIZE, SIZE, std::string(EDGE_ROOT) + "/test/shaders", std::make_shared<GLESSDLContext>(window));
		// 2 x 2: top row red, green; bottom row blue, white (B, G, R, X bytes, row 0 the top)
		uint8_t const frame[] = {0, 0, 255, 0,  0, 255, 0, 0,
		                         255, 0, 0, 0,  255, 255, 255, 0};
		renderer.show_frame(frame, 2, 2, 40, 20);
		// before the swap the window's back buffer holds the frame: GL rows run from the bottom
		auto pixel = [](int x, int y_from_top)
		{
			uint8_t rgba[4];
			glReadPixels(x, 20 - 1 - y_from_top, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
			return std::array<int, 3>{rgba[0], rgba[1], rgba[2]};
		};
		BOOST_TEST((pixel(5, 3) == std::array<int, 3>{255, 0, 0}), "top left is red");
		BOOST_TEST((pixel(35, 3) == std::array<int, 3>{0, 255, 0}), "top right is green");
		BOOST_TEST((pixel(5, 16) == std::array<int, 3>{0, 0, 255}), "bottom left is blue");
		BOOST_TEST((pixel(35, 16) == std::array<int, 3>{255, 255, 255}), "bottom right is white");
		BOOST_CHECK_THROW(renderer.show_frame(frame, 0, 2, 40, 20), std::invalid_argument);
		BOOST_CHECK_THROW(renderer.show_frame(frame, 2, 2, 40, 0), std::invalid_argument);
	}
	SDL_DestroyWindow(window);
}

BOOST_AUTO_TEST_CASE(a_texture_is_shown_in_the_hosts_window_without_a_download)
{
	if (!std::getenv("SDL_VIDEO_DRIVER"))
		setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
	BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO));
	SDL_Window *window = SDL_CreateWindow("host", 40, 20, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	BOOST_REQUIRE(window);
	{
		GLESRenderer renderer(SIZE, SIZE, std::string(EDGE_ROOT) + "/test/shaders", std::make_shared<GLESSDLContext>(window));
		// the same 2 x 2 as a texture of the renderer's (a render target's colour texture is one): top row red, green; bottom blue, white
		TexturePtr texture = renderer.create_texture(2, 2, PixelFormat::A8R8G8B8);
		uint8_t const texels[] = {0, 0, 255, 255,  0, 255, 0, 255,
		                          255, 0, 0, 255,  255, 255, 255, 255};          // (B, G, R, A bytes, as a texture is locked)
		auto texture_mapping = texture->write(Contents::DISCARD);
		std::memcpy(texture_mapping.data(), texels, sizeof texels);
		texture_mapping.unmap();
		renderer.show_frame(texture, 40, 20);
		auto pixel = [](int x, int y_from_top)
		{
			uint8_t rgba[4];
			glReadPixels(x, 20 - 1 - y_from_top, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
			return std::array<int, 3>{rgba[0], rgba[1], rgba[2]};
		};
		BOOST_TEST((pixel(5, 3) == std::array<int, 3>{255, 0, 0}), "top left is red");
		BOOST_TEST((pixel(35, 3) == std::array<int, 3>{0, 255, 0}), "top right is green");
		BOOST_TEST((pixel(5, 16) == std::array<int, 3>{0, 0, 255}), "bottom left is blue");
		BOOST_TEST((pixel(35, 16) == std::array<int, 3>{255, 255, 255}), "bottom right is white");
		BOOST_CHECK_THROW(renderer.show_frame(TexturePtr(), 40, 20), std::invalid_argument);
		BOOST_CHECK_THROW(renderer.show_frame(texture, 40, 0), std::invalid_argument);
		GLESRenderer other(SIZE, SIZE, std::string(EDGE_ROOT) + "/test/shaders", std::make_shared<GLESSDLContext>());
		BOOST_CHECK_THROW(renderer.show_frame(other.create_texture(2, 2, PixelFormat::A8R8G8B8), 40, 20), std::invalid_argument);
	}
	SDL_DestroyWindow(window);
}

BOOST_AUTO_TEST_CASE(a_renderer_needs_a_size)
{
	if (!std::getenv("SDL_VIDEO_DRIVER"))
		setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
	auto const context = std::make_shared<GLESSDLContext>();
	BOOST_CHECK_THROW(GLESRenderer(0, 10, ".", context), std::invalid_argument);
	BOOST_CHECK_THROW(GLESRenderer(10, -1, ".", context), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
