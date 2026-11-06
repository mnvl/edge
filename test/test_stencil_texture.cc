// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the stencil (Material::stencil_write, RenderTarget::stencil_texture): a material that writes the stencil leaves its
// value wherever one of its fragments passes the depth test, and nowhere else; a material that does not write it leaves what was there;
// a pass clears it to the clear state's value; and a later pass of the same frame reads it, the value of each pixel, from the stencil
// texture of a target made with a readable depth.  Every check renders through the renderer and reads the pixels back
// (test_stencil_frag.glsl copies the stencil texture's value under each pixel into the colour).

#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include <optional>
#include <stdexcept>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "null_renderer.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "test_vulkan_fixture.h"
#include "texture.h"
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

/// A rectangle of an orthographic view (eye at z = 1 looking down -z, near 0, far 2: a larger z is nearer), from x0 to x1 and y0 to y1 in
/// clip units, at z.
DrawCallPtr rectangle(VulkanRendererPtr const &renderer, MaterialPtr const &material, CameraPtr const &camera, float x0, float x1, float y0,
	float y1, float z)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	BOOST_REQUIRE(v);
	v[0].position = rove::vec<3>(x0, y0, z);
	v[1].position = rove::vec<3>(x1, y0, z);
	v[2].position = rove::vec<3>(x1, y1, z);
	v[3].position = rove::vec<3>(x0, y1, z);
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

/// A flat material that writes `stencil` into the stencil, or leaves it be.
MaterialPtr writer(VulkanRendererPtr const &renderer, std::optional<uint8_t> stencil)
{
	MaterialPtr m = renderer->create_material();
	m->set_cull_mode(Material::CullMode::NONE);
	m->set_shader_program(renderer->create_shader_program(edge::test_shader("test_frame.vert"), edge::test_shader("test_sample.frag")));
	m->set_stencil_write(stencil);
	return m;
}

/// Draws the calls into `scene` in their order (one pass each frame, the stencil cleared to `clear_stencil`), and in the same frame copies its
/// stencil texture into `copy` (a pass that covers it all); returns what the copy holds, a value a pixel, row by row from the top.
struct StencilReader
{
	VulkanRendererPtr renderer;
	CameraPtr camera = view();
	RenderTargetPtr scene, copy;
	MaterialPtr reader;
	DrawCallPtr cover;

	explicit StencilReader(VulkanRendererPtr const &r):
		renderer(r),
		scene(r->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F, true)),
		copy(r->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F))
	{
		BOOST_REQUIRE_MESSAGE(scene->stencil_texture(), "no stencil texture: the depth format has no stencil (VkFormat "
			<< r->depth_format() << ")");
		reader = r->create_material();
		reader->set_cull_mode(Material::CullMode::NONE);
		reader->set_shader_program(r->create_shader_program(edge::test_shader("test_frame.vert"), edge::test_shader("test_stencil.frag")));
		reader->set_diffuse_texture(scene->stencil_texture());
		cover = rectangle(r, reader, camera, -1, 1, -1, 1, 0);
	}

	std::vector<int> frame(std::vector<DrawCallPtr> const &drawn, uint32_t clear_stencil)
	{
		RenderPassClearState clear;
		clear.clear_stencil = clear_stencil;
		renderer->begin_frame();
		renderer->begin_pass(scene, clear);
		for (DrawCallPtr const &call : drawn)
			call->execute();
		renderer->end_pass();
		RenderPassClearState plain;
		plain.clear_color_r = -1.0f;
		renderer->begin_pass(copy, plain);
		cover->execute();
		renderer->end_pass();
		renderer->finish_frame();

		TexturePtr color = copy->color_texture();
		color->download();
		auto color_mapping = color->read();
		Rgba const *pixels = color_mapping.as<Rgba const>().data();
		BOOST_REQUIRE(pixels);
		std::vector<int> stencil(SIZE * SIZE);
		for (int i = 0; i < SIZE * SIZE; ++i)
			stencil[i] = static_cast<int>(pixels[i].r);
		color_mapping.unmap();
		return stencil;
	}
};

/// Pixels of `stencil` in the columns [x0, x1) and rows [y0, y1) that do not hold `want`; the first few are reported by name.
int wrong_in(std::vector<int> const &stencil, int x0, int x1, int y0, int y1, int want, char const *what)
{
	int wrong = 0;
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x)
			if (stencil[y * SIZE + x] != want && wrong++ < 3)
				BOOST_ERROR(what << ": pixel (" << x << ", " << y << ") holds " << stencil[y * SIZE + x] << ", want " << want);
	return wrong;
}

}

BOOST_FIXTURE_TEST_SUITE(vulkan_stencil_texture, VulkanFixture)

BOOST_AUTO_TEST_CASE(only_a_target_with_a_readable_depth_has_a_stencil_texture_of_its_size)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	RenderTargetPtr const plain = renderer->create_render_target(40, 24, PixelFormat::R16G16B16A16F);
	BOOST_CHECK(!plain->stencil_texture());
	RenderTargetPtr const readable = renderer->create_render_target(40, 24, PixelFormat::R16G16B16A16F, true);
	TexturePtr const stencil = readable->stencil_texture();
	BOOST_REQUIRE(stencil);
	BOOST_CHECK_EQUAL(stencil->width(), 40);
	BOOST_CHECK_EQUAL(stencil->height(), 24);
	BOOST_CHECK_EQUAL(stencil->pixel_format(), PixelFormat::R8UI);
	BOOST_CHECK(stencil != readable->depth_texture());
	BOOST_CHECK(stencil != readable->color_texture());
	// the GPU writes it and shaders read it: there is no CPU copy to write, or to read back
	BOOST_CHECK_THROW((void)stencil->write(Contents::DISCARD), std::logic_error);
	BOOST_CHECK_THROW(stencil->download(), std::logic_error);
}

/// Over the middle half of the rows, left to right by quarters: a 5 under a quad that writes none in front of it; a 5 a quad behind it
/// failed to replace with 7; a 9 from a second writer in the same frame; and a 0 written over the 9 by a nearer quad.  The rows above and
/// below hold the clear, 3.
BOOST_AUTO_TEST_CASE(a_writer_leaves_its_value_where_it_passes_the_depth_test_and_nowhere_else)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	StencilReader reader(renderer);
	CameraPtr const &c = reader.camera;
	std::vector<int> const stencil = reader.frame({
		rectangle(renderer, writer(renderer, 5), c, -1.0f, 0.0f, -0.5f, 0.5f, 0.0f),
		rectangle(renderer, writer(renderer, 9), c, 0.0f, 1.0f, -0.5f, 0.5f, 0.0f),
		rectangle(renderer, writer(renderer, std::nullopt), c, -1.0f, -0.5f, -0.5f, 0.5f, 0.5f),     // in front, writes none
		rectangle(renderer, writer(renderer, 7), c, -0.5f, 0.0f, -0.5f, 0.5f, -0.5f),               // behind: fails the depth test
		rectangle(renderer, writer(renderer, 0), c, 0.5f, 1.0f, -0.5f, 0.5f, 0.5f),                 // in front: writes its 0
	}, 3);
	int const top = SIZE / 4 + 1, bottom = SIZE * 3 / 4 - 1, q = SIZE / 4;       // (the rectangles' own edge rows and columns left out)
	BOOST_CHECK_EQUAL(wrong_in(stencil, 1, q - 1, top, bottom, 5, "under a quad that writes none"), 0);
	BOOST_CHECK_EQUAL(wrong_in(stencil, q + 1, 2 * q - 1, top, bottom, 5, "behind a quad that failed the depth test"), 0);
	BOOST_CHECK_EQUAL(wrong_in(stencil, 2 * q + 1, 3 * q - 1, top, bottom, 9, "the second writer's"), 0);
	BOOST_CHECK_EQUAL(wrong_in(stencil, 3 * q + 1, SIZE - 1, top, bottom, 0, "a nearer writer's 0"), 0);
	BOOST_CHECK_EQUAL(wrong_in(stencil, 0, SIZE, 0, SIZE / 4 - 1, 3, "above, the clear"), 0);
	BOOST_CHECK_EQUAL(wrong_in(stencil, 0, SIZE, SIZE * 3 / 4 + 1, SIZE, 3, "below, the clear"), 0);
}

/// Frame after frame the stencil texture holds this frame's stencil: the copy follows the writer from one half to the other, and the half it
/// left holds the clear again.
BOOST_AUTO_TEST_CASE(each_frame_reads_its_own_stencil)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	StencilReader reader(renderer);
	MaterialPtr const m = writer(renderer, 6);
	std::vector<int> const left = reader.frame({rectangle(renderer, m, reader.camera, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f)}, 0);
	std::vector<int> const right = reader.frame({rectangle(renderer, m, reader.camera, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f)}, 0);
	int const y = SIZE / 2;
	BOOST_CHECK_EQUAL(left[y * SIZE + 5], 6);
	BOOST_CHECK_EQUAL(left[y * SIZE + SIZE - 5], 0);
	BOOST_CHECK_EQUAL(right[y * SIZE + 5], 0);
	BOOST_CHECK_EQUAL(right[y * SIZE + SIZE - 5], 6);
}

/// The same material, its value changed between frames: the pipeline follows the material, not the first value it was made with.
BOOST_AUTO_TEST_CASE(a_changed_value_is_written_from_the_next_draw_on)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	StencilReader reader(renderer);
	MaterialPtr const m = writer(renderer, 1);
	DrawCallPtr const all = rectangle(renderer, m, reader.camera, -1.0f, 1.0f, -1.0f, 1.0f, 0.0f);
	int const middle = (SIZE / 2) * SIZE + SIZE / 2;
	BOOST_CHECK_EQUAL(reader.frame({all}, 0)[middle], 1);
	m->set_stencil_write(200);
	BOOST_CHECK_EQUAL(reader.frame({all}, 0)[middle], 200);
	m->set_stencil_write(std::nullopt);
	BOOST_CHECK_EQUAL(reader.frame({all}, 4)[middle], 4);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(null_stencil_texture)

/// The null renderer's targets agree with the Vulkan ones on which have a stencil texture.
BOOST_AUTO_TEST_CASE(a_null_target_with_a_readable_depth_has_a_stencil_texture)
{
	RendererPtr renderer(new NullRenderer(320, 200));
	BOOST_CHECK(!renderer->create_render_target(16, 8, PixelFormat::R16G16B16A16F)->stencil_texture());
	TexturePtr const stencil = renderer->create_render_target(16, 8, PixelFormat::R16G16B16A16F, true)->stencil_texture();
	BOOST_REQUIRE(stencil);
	BOOST_CHECK_EQUAL(stencil->width(), 16);
	BOOST_CHECK_EQUAL(stencil->height(), 8);
	BOOST_CHECK_EQUAL(stencil->pixel_format(), PixelFormat::R8UI);
}

BOOST_AUTO_TEST_SUITE_END()
