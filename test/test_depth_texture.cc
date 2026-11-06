// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of a render target's readable depth (Renderer::create_render_target with readable_depth, RenderTarget::depth_texture): the
// depth a pass wrote is a texture that a later pass of the same frame samples, the depth of each pixel as the depth test saw it (to the depth
// format's precision, far finer than a half float's), the clear value where nothing was drawn, and the current frame's depth, not an older
// one.  A target asked for none has none.  On Vulkan and OpenGL ES alike (WebGL 2 samples depth textures too).  Every check renders through the renderer and reads the pixels back (test_depth_frag.glsl copies
// the depth texture's texel under each pixel into the colour).

#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "test_backends.h"
#include "texture.h"
#include "image.h"
#include <utility>
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

int const SIZE = 64;

struct Rgba
{
	float r, g, b, a;
};

/// A quad across the whole width of an orthographic view (eye at z = 1 looking down -z, near 0, far 2: the depth is half the distance from
/// the eye), `half_height` clip units above and below the middle, its z going from `z_left` at the left edge to `z_right` at the right.
DrawCallPtr quad(RendererPtr const &renderer, MaterialPtr const &material, CameraPtr const &camera, float half_height, float z_left,
	float z_right)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	BOOST_REQUIRE(v);
	v[0].position = rove::vec<3>(-1, -half_height, z_left);
	v[1].position = rove::vec<3>(1, -half_height, z_right);
	v[2].position = rove::vec<3>(1, half_height, z_right);
	v[3].position = rove::vec<3>(-1, half_height, z_left);
	for (int k = 0; k < 4; ++k)
		v[k].texcoords = rove::vec<2>(0.5f, 0.5f);
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
	BOOST_REQUIRE(idx);
	idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
	ib_mapping.unmap();
	DrawCallPtr draw_call = renderer->create_draw_call();
	draw_call->set_vertex_buffer(vb);
	draw_call->set_index_buffer(ib);
	draw_call->set_material(material);
	draw_call->set_camera(camera);
	draw_call->set_triangles_count(2);
	draw_call->set_always_visible(true);
	draw_call->commit();
	return draw_call;
}

CameraPtr view()
{
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

MaterialPtr material(RendererPtr const &renderer, std::string const &fragment)
{
	MaterialPtr m = renderer->create_material();
	m->set_cull_mode(Material::CullMode::NONE);
	m->set_shader_program(renderer->create_shader_program(edge::test_shader("test_frame.vert"), fragment));
	return m;
}

/// Draws the scenes into `scene` (one pass each frame, depth cleared to `clear_depth`), and in the same frame copies its depth texture into
/// `copy` (a pass that covers it all); returns what the copy holds after the last frame.
struct DepthReader
{
	RendererPtr renderer;
	CameraPtr camera = view();
	RenderTargetPtr scene, copy;
	MaterialPtr flat, reader;
	DrawCallPtr cover;

	explicit DepthReader(RendererPtr const &r):
		renderer(r),
		scene(r->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F, true)),
		copy(r->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F)),
		flat(material(r, edge::test_shader("test_sample.frag"))),
		reader(material(r, edge::test_shader("test_depth.frag")))
	{
		BOOST_REQUIRE(scene->depth_texture());
		reader->set_diffuse_texture(scene->depth_texture());
		cover = quad(r, reader, camera, 1.0f, 0.0f, 0.0f);
	}

	std::vector<float> frame(DrawCallPtr const &drawn, float clear_depth = 1.0f)
	{
		RenderPassClearState clear;
		clear.clear_depth_value = clear_depth;
		renderer->begin_frame();
		renderer->begin_pass(scene, clear);
		drawn->execute();
		renderer->end_pass();
		RenderPassClearState plain;
		plain.clear_color_r = plain.clear_color_g = plain.clear_color_b = plain.clear_color_a = -1.0f;
		renderer->begin_pass(copy, plain);
		cover->execute();
		renderer->end_pass();
		renderer->finish_frame();

		TexturePtr color = copy->color_texture();
		color->download();
		auto color_mapping = color->read();
		Rgba const *pixels = color_mapping.as<Rgba const>().data();
		BOOST_REQUIRE(pixels);
		std::vector<float> depth(SIZE * SIZE);
		for (int i = 0; i < SIZE * SIZE; ++i)
			depth[i] = pixels[i].r;
		color_mapping.unmap();
		return depth;
	}
};

/// The depth the quad of `quad(.., z_left, z_right)` has at the middle of column x: its z is linear across the view, and the depth is half
/// its distance from the eye at z = 1.
float expected_depth(int x, float z_left, float z_right)
{
	float const t = (float(x) + 0.5f) / float(SIZE);
	return 0.5f * (1.0f - (z_left + (z_right - z_left) * t));
}

}

BOOST_AUTO_TEST_SUITE(depth_texture)

BOOST_AUTO_TEST_CASE_TEMPLATE(only_a_target_asked_for_one_has_a_depth_texture_of_its_size, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetPtr const plain = renderer->create_render_target(40, 24, PixelFormat::R16G16B16A16F);
	BOOST_CHECK(!plain->depth_texture());
	RenderTargetPtr const readable = renderer->create_render_target(40, 24, PixelFormat::R16G16B16A16F, true);
	TexturePtr const depth = readable->depth_texture();
	BOOST_REQUIRE(depth);
	BOOST_CHECK_EQUAL(depth->width(), 40);
	BOOST_CHECK_EQUAL(depth->height(), 24);
	BOOST_CHECK(depth != readable->color_texture());
	// the GPU writes it and shaders read it: there is no CPU copy to write, or to read back
	BOOST_CHECK_THROW((void)depth->write(Contents::DISCARD), std::logic_error);
	BOOST_CHECK_THROW(depth->download(), std::logic_error);
}

/// A tilted quad over the middle half of the rows: each pixel under it reads its own depth, to within what a 24-bit depth holds (a half float
/// would be 2.4e-4 off at 0.5), and the rows above and below it read the clear.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_later_pass_reads_the_depth_of_each_pixel_and_the_clear_around_it, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	DepthReader reader(renderer);
	float const z_left = -0.6f, z_right = 0.7f;
	std::vector<float> const depth = reader.frame(quad(renderer, reader.flat, reader.camera, 0.5f, z_left, z_right), 1.0f);
	int wrong = 0;
	float worst = 0;
	for (int y = 0; y < SIZE; ++y)
		for (int x = 0; x < SIZE; ++x)
		{
			bool const under = y >= SIZE / 4 + 1 && y < SIZE * 3 / 4 - 1, beside = y < SIZE / 4 - 1 || y >= SIZE * 3 / 4 + 1;
			if (!under && !beside)
				continue;       // the quad's own edge rows
			float const want = under ? expected_depth(x, z_left, z_right) : 1.0f;
			float const off = std::fabs(depth[y * SIZE + x] - want);
			worst = std::max(worst, off);
			if (off > 2e-6f && wrong++ < 5)
				BOOST_ERROR("pixel (" << x << ", " << y << "): depth " << depth[y * SIZE + x] << ", want " << want);
		}
	BOOST_TEST_MESSAGE("worst depth error " << worst);
	BOOST_CHECK_EQUAL(wrong, 0);
	// and neighbouring columns differ by the quad's slope, not by steps
	int const y = SIZE / 2;
	for (int x = 1; x < SIZE; ++x)
		BOOST_CHECK_CLOSE(depth[y * SIZE + x] - depth[y * SIZE + x - 1], -0.5f * (z_right - z_left) / SIZE, 0.1);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(the_clear_value_is_what_nothing_drawn_reads, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	DepthReader reader(renderer);
	std::vector<float> const depth = reader.frame(quad(renderer, reader.flat, reader.camera, 0.5f, 0.2f, 0.2f), 0.75f);
	BOOST_CHECK_CLOSE(depth[2 * SIZE + 7], 0.75f, 1e-4);                     // above the quad
	BOOST_CHECK_CLOSE(depth[(SIZE / 2) * SIZE + 7], 0.4f, 1e-3);             // the quad, nearer than the clear
}

/// Frame after frame the depth texture holds this frame's depth: the copy follows the quad as it moves, and back.
BOOST_AUTO_TEST_CASE_TEMPLATE(each_frame_reads_its_own_depth, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	DepthReader reader(renderer);
	float const zs[] = {0.5f, -0.5f, 0.5f, 0.0f};
	for (float z : zs)
	{
		std::vector<float> const depth = reader.frame(quad(renderer, reader.flat, reader.camera, 0.5f, z, z));
		BOOST_CHECK_CLOSE(depth[(SIZE / 2) * SIZE + SIZE / 2], 0.5f * (1.0f - z), 1e-3);
	}
}

/// A readable depth set as a shadow texture is sampled with a comparison (sampler2DShadow): 1 where the reference depth is at most the
/// depth stored, 0 where it is past it.  The scene's quad is at depth 0.4 over the middle rows and the clear, 1, around it.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_shadow_texture_compares_a_depth_with_the_one_stored, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	CameraPtr const camera = view();
	RenderTargetPtr const scene = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F, true);
	RenderTargetPtr const copy = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
	MaterialPtr const flat = material(renderer, edge::test_shader("test_sample.frag"));
	MaterialPtr const compare = material(renderer, edge::test_shader("test_shadow_compare.frag"));
	DrawCallPtr const drawn = quad(renderer, flat, camera, 0.5f, 0.2f, 0.2f);
	DrawCallPtr const cover = quad(renderer, compare, camera, 1.0f, 0.0f, 0.0f);
	renderer->set_global_shadow_texture(0, scene->depth_texture());

	auto const lit = [&](float reference)
	{
		compare->set_extra_parameter(0, rove::vec<4>(reference, float(SIZE), float(SIZE), 0));
		renderer->begin_frame();
		renderer->begin_pass(scene);
		drawn->execute();
		renderer->end_pass();
		renderer->begin_pass(copy);
		cover->execute();
		renderer->end_pass();
		renderer->finish_frame();
		Image<rove::rgba<float>> const image = read_image<rove::rgba<float>>(*copy->color_texture());
		return std::pair{image.at(SIZE / 2, SIZE / 2).r, image.at(SIZE / 2, 2).r};      // under the quad, and above it
	};
	// (a reference past 1, outside the depth range, means nothing: OpenGL ES clamps it to 1 for its 24-bit depth, Vulkan's float depth
	// does not)
	auto const nearer = lit(0.3f), between = lit(0.5f), far = lit(0.95f);
	BOOST_CHECK_EQUAL(nearer.first, 1.0f);          // 0.3 <= 0.4
	BOOST_CHECK_EQUAL(nearer.second, 1.0f);         // 0.3 <= 1
	BOOST_CHECK_EQUAL(between.first, 0.0f);         // 0.5 > 0.4: in the quad's shadow
	BOOST_CHECK_EQUAL(between.second, 1.0f);
	BOOST_CHECK_EQUAL(far.first, 0.0f);
	BOOST_CHECK_EQUAL(far.second, 1.0f);            // 0.95 <= 1
}

/// Only a readable depth is a shadow texture, set between frames and in a slot there is.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_shadow_texture_is_a_readable_depth_set_between_frames, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetPtr const target = renderer->create_render_target(16, 16, PixelFormat::R32G32B32A32F, true);
	TexturePtr const depth = target->depth_texture();
	BOOST_REQUIRE(depth);
	BOOST_CHECK(depth->is_depth());
	BOOST_CHECK(!target->color_texture()->is_depth());
	BOOST_CHECK_THROW(renderer->set_global_shadow_texture(0, target->color_texture()), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->set_global_shadow_texture(0, TexturePtr()), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->set_global_shadow_texture(Renderer::GLOBAL_TEXTURES_COUNT, depth), std::out_of_range);
	renderer->set_global_shadow_texture(Renderer::GLOBAL_TEXTURES_COUNT - 1, depth);
	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->set_global_shadow_texture(0, depth), std::logic_error);
	renderer->finish_frame();
	renderer->set_global_texture(Renderer::GLOBAL_TEXTURES_COUNT - 1, TexturePtr());       // and plain again
}

BOOST_AUTO_TEST_SUITE_END()
