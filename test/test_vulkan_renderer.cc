// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "vertex.h"
#include "vertex_traits.h"
#include "pixel.h"
#include "camera.h"
#include "material.h"
#include "draw_call.h"
#include "uber_draw_call.h"
#include "render_target.h"
#include "test_vulkan_fixture.h"

using namespace edge;

BOOST_FIXTURE_TEST_SUITE(vulkan, VulkanFixture)

BOOST_AUTO_TEST_CASE(basic)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	BOOST_CHECK(renderer);
	BOOST_CHECK_EQUAL(renderer->screen_width(), 800);
	BOOST_CHECK_EQUAL(renderer->screen_height(), 600);

	for (int i = 0; i < 5; ++i)
	{
		renderer->begin_frame();
		renderer->begin_pass(renderer->default_render_target());
		renderer->end_pass();
		renderer->finish_frame();
	}
}

BOOST_AUTO_TEST_CASE(vertex_buffer)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);

	BOOST_CHECK(vb);
	BOOST_CHECK_EQUAL(vb->size(), sizeof(VERTEX_PT) * 3);

	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *vertices = vb_mapping.as<VERTEX_PT>().data();
	vertices[0].position = rove::vec<3>(0.0f, 0.0f, 0.0f);
	vertices[0].texcoords = rove::vec<2>(0.0f, 0.0f);
	vertices[1].position = rove::vec<3>(1.0f, 0.0f, 0.0f);
	vertices[1].texcoords = rove::vec<2>(1.0f, 0.0f);
	vertices[2].position = rove::vec<3>(0.0f, 1.0f, 0.0f);
	vertices[2].texcoords = rove::vec<2>(0.0f, 1.0f);
	vb_mapping.unmap();

	auto vb_mapping2 = vb->read();
	VERTEX_PT const *locked = vb_mapping2.as<VERTEX_PT const>().data();
	BOOST_CHECK_CLOSE(locked[0].position.x, 0.0f, 0.01f);
	BOOST_CHECK_CLOSE(locked[1].position.x, 1.0f, 0.01f);
	BOOST_CHECK_CLOSE(locked[2].position.y, 1.0f, 0.01f);
	vb_mapping2.unmap();
}

BOOST_AUTO_TEST_CASE(index_buffer)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 3);

	BOOST_CHECK(ib);

	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *indices = ib_mapping.as<INDEX_U16>().data();
	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	ib_mapping.unmap();

	auto ib_mapping2 = ib->read();
	INDEX_U16 const *locked = ib_mapping2.as<INDEX_U16 const>().data();
	BOOST_CHECK_EQUAL(locked[0], 0);
	BOOST_CHECK_EQUAL(locked[1], 1);
	BOOST_CHECK_EQUAL(locked[2], 2);
	ib_mapping2.unmap();
}

BOOST_AUTO_TEST_CASE(texture)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	TexturePtr tex = renderer->create_texture(4, 4, PixelFormat::X8R8G8B8);

	BOOST_CHECK(tex);
	BOOST_CHECK_EQUAL(tex->width(), 4);
	BOOST_CHECK_EQUAL(tex->height(), 4);

	auto tex_mapping = tex->write(Contents::DISCARD);
	unsigned *pixels = tex_mapping.as<unsigned>().data();
	for (int i = 0; i < 16; ++i)
		pixels[i] = 0xFF0000FFu;
	tex_mapping.unmap();

	auto tex_mapping2 = tex->read();
	unsigned const *locked = tex_mapping2.as<unsigned const>().data();
	BOOST_CHECK_EQUAL(locked[0], 0xFF0000FFu);
	tex_mapping2.unmap();
}

BOOST_AUTO_TEST_CASE(material)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	MaterialPtr mat = renderer->create_material();

	BOOST_CHECK(mat);

	mat->set_cull_mode(Material::CullMode::CW);
	mat->set_zwrite_enabled(true);
	mat->set_alpha_blend_enabled(false);

	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE(draw_call)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 3);
	MaterialPtr mat = renderer->create_material();
	TexturePtr tex = renderer->create_texture(2, 2, PixelFormat::X8R8G8B8);

	{
		auto vb_mapping = vb->write(Contents::DISCARD);
		VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
		v[0].position = rove::vec<3>(-1, -1, 0); v[0].texcoords = rove::vec<2>(0, 0);
		v[1].position = rove::vec<3>( 1, -1, 0); v[1].texcoords = rove::vec<2>(1, 0);
		v[2].position = rove::vec<3>( 0,  1, 0); v[2].texcoords = rove::vec<2>(0.5f, 1);
		vb_mapping.unmap();
	}

	{
		auto ib_mapping = ib->write(Contents::DISCARD);
		INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
		idx[0] = 0; idx[1] = 1; idx[2] = 2;
		ib_mapping.unmap();
	}

	{
		auto tex_mapping = tex->write(Contents::DISCARD);
		unsigned *p = tex_mapping.as<unsigned>().data();
		p[0] = p[1] = p[2] = p[3] = 0xFFFFFFFFu;
		tex_mapping.unmap();
	}

	mat->set_diffuse_texture(tex);

	CameraPtr camera(new Camera());
	camera->set_perspective_projection(1.0f, 800.0f / 600.0f, 0.1f, 100.0f);
	camera->look_at(
		rove::vec<3>(0, 0, -5),
		rove::vec<3>(0, 0, 0),
		rove::vec<3>(0, 1, 0));

	DrawCallPtr dc = renderer->create_draw_call();
	dc->set_vertex_buffer(vb);
	dc->set_index_buffer(ib);
	dc->set_material(mat);
	dc->set_camera(camera);
	dc->set_triangles_count(1);
	dc->set_always_visible(true);
	dc->commit();

	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	dc->execute();
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE(uber_draw_call)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PNT>(), sizeof(VERTEX_PNT) * 3);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 3);
	MaterialPtr mat = renderer->create_material();
	TexturePtr tex = renderer->create_texture(2, 2, PixelFormat::X8R8G8B8);

	{
		auto vb_mapping = vb->write(Contents::DISCARD);
		VERTEX_PNT *v = vb_mapping.as<VERTEX_PNT>().data();
		v[0].position = rove::vec<3>(-1, -1, 0); v[0].normal = rove::vec<3>(0, 0, -1); v[0].texcoords = rove::vec<2>(0, 0);
		v[1].position = rove::vec<3>( 1, -1, 0); v[1].normal = rove::vec<3>(0, 0, -1); v[1].texcoords = rove::vec<2>(1, 0);
		v[2].position = rove::vec<3>( 0,  1, 0); v[2].normal = rove::vec<3>(0, 0, -1); v[2].texcoords = rove::vec<2>(0.5f, 1);
		vb_mapping.unmap();
	}

	{
		auto ib_mapping = ib->write(Contents::DISCARD);
		INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
		idx[0] = 0; idx[1] = 1; idx[2] = 2;
		ib_mapping.unmap();
	}

	{
		auto tex_mapping = tex->write(Contents::DISCARD);
		unsigned *p = tex_mapping.as<unsigned>().data();
		p[0] = p[1] = p[2] = p[3] = 0xFF00FFFFu;
		tex_mapping.unmap();
	}

	mat->set_diffuse_texture(tex);

	CameraPtr camera(new Camera());
	camera->set_perspective_projection(1.0f, 800.0f / 600.0f, 0.1f, 100.0f);
	camera->look_at(
		rove::vec<3>(0, 0, -5),
		rove::vec<3>(0, 0, 0),
		rove::vec<3>(0, 1, 0));

	UberDrawCallPtr dc = renderer->create_uber_draw_call();
	BOOST_CHECK(dc);
	dc->set_vertex_buffer(vb);
	dc->set_index_buffer(ib);
	dc->set_material(mat);
	dc->set_camera(camera);
	dc->set_triangles_count(1);
	dc->set_always_visible(true);
	dc->commit();

	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	dc->execute();
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE(offscreen_render_target)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr offscreen = renderer->create_render_target(256, 256, PixelFormat::X8R8G8B8);
	BOOST_REQUIRE(offscreen);
	TexturePtr offscreen_texture = offscreen->color_texture();
	BOOST_REQUIRE(offscreen_texture);

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 3);
	MaterialPtr write_material = renderer->create_material();
	MaterialPtr sample_material = renderer->create_material();
	TexturePtr source_texture = renderer->create_texture(2, 2, PixelFormat::X8R8G8B8);

	{
		auto vb_mapping = vb->write(Contents::DISCARD);
		VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
		v[0].position = rove::vec<3>(-1, -1, 0); v[0].texcoords = rove::vec<2>(0, 0);
		v[1].position = rove::vec<3>( 1, -1, 0); v[1].texcoords = rove::vec<2>(1, 0);
		v[2].position = rove::vec<3>( 0,  1, 0); v[2].texcoords = rove::vec<2>(0.5f, 1);
		vb_mapping.unmap();
	}

	{
		auto ib_mapping = ib->write(Contents::DISCARD);
		INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
		idx[0] = 0; idx[1] = 1; idx[2] = 2;
		ib_mapping.unmap();
	}

	{
		auto source_texture_mapping = source_texture->write(Contents::DISCARD);
		unsigned *p = source_texture_mapping.as<unsigned>().data();
		p[0] = p[1] = p[2] = p[3] = 0xFF00FF00u;
		source_texture_mapping.unmap();
	}

	write_material->set_diffuse_texture(source_texture);
	sample_material->set_diffuse_texture(offscreen_texture);

	CameraPtr camera(new Camera());
	camera->set_perspective_projection(1.0f, 800.0f / 600.0f, 0.1f, 100.0f);
	camera->look_at(
		rove::vec<3>(0, 0, -5),
		rove::vec<3>(0, 0, 0),
		rove::vec<3>(0, 1, 0));

	DrawCallPtr draw_to_offscreen = renderer->create_draw_call();
	draw_to_offscreen->set_vertex_buffer(vb);
	draw_to_offscreen->set_index_buffer(ib);
	draw_to_offscreen->set_material(write_material);
	draw_to_offscreen->set_camera(camera);
	draw_to_offscreen->set_triangles_count(1);
	draw_to_offscreen->set_always_visible(true);
	draw_to_offscreen->commit();

	DrawCallPtr draw_to_backbuffer = renderer->create_draw_call();
	draw_to_backbuffer->set_vertex_buffer(vb);
	draw_to_backbuffer->set_index_buffer(ib);
	draw_to_backbuffer->set_material(sample_material);
	draw_to_backbuffer->set_camera(camera);
	draw_to_backbuffer->set_triangles_count(1);
	draw_to_backbuffer->set_always_visible(true);
	draw_to_backbuffer->commit();

	renderer->begin_frame();
	renderer->begin_pass(offscreen);
	draw_to_offscreen->execute();
	renderer->end_pass();
	renderer->begin_pass(renderer->default_render_target());
	draw_to_backbuffer->execute();
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE(offscreen_render_target_readback)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr offscreen = renderer->create_render_target(64, 64, PixelFormat::X8R8G8B8);
	BOOST_REQUIRE(offscreen);
	TexturePtr offscreen_texture = offscreen->color_texture();
	BOOST_REQUIRE(offscreen_texture);

	RenderPassClearState clear_state;
	clear_state.clear_color = true;
	clear_state.clear_color_r = 0.22f;
	clear_state.clear_color_g = 0.47f;
	clear_state.clear_color_b = 0.73f;
	clear_state.clear_color_a = 1.0f;
	clear_state.clear_depth = true;
	clear_state.clear_depth_value = 1.0f;

	renderer->begin_frame();
	renderer->begin_pass(offscreen, clear_state);
	renderer->end_pass();
	renderer->finish_frame();

	offscreen_texture->download();
	auto offscreen_texture_mapping = offscreen_texture->read();
	unsigned const *pixels = offscreen_texture_mapping.as<unsigned const>().data();
	BOOST_REQUIRE(pixels);

	unsigned const center_pixel = pixels[32 * 64 + 32];
	offscreen_texture_mapping.unmap();

	float const b = static_cast<float>((center_pixel >> 0) & 0xFF) / 255.0f;
	float const g = static_cast<float>((center_pixel >> 8) & 0xFF) / 255.0f;
	float const r = static_cast<float>((center_pixel >> 16) & 0xFF) / 255.0f;

	BOOST_CHECK_SMALL(std::abs(r - clear_state.clear_color_r), 0.08f);
	BOOST_CHECK_SMALL(std::abs(g - clear_state.clear_color_g), 0.08f);
	BOOST_CHECK_SMALL(std::abs(b - clear_state.clear_color_b), 0.08f);
}

BOOST_AUTO_TEST_CASE(offscreen_float_render_target_readback)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr offscreen16 = renderer->create_render_target(32, 32, PixelFormat::R16G16B16A16F);
	RenderTargetPtr offscreen32 = renderer->create_render_target(32, 32, PixelFormat::R32G32B32A32F);
	BOOST_REQUIRE(offscreen16);
	BOOST_REQUIRE(offscreen32);

	TexturePtr offscreen_texture16 = offscreen16->color_texture();
	TexturePtr offscreen_texture32 = offscreen32->color_texture();
	BOOST_REQUIRE(offscreen_texture16);
	BOOST_REQUIRE(offscreen_texture32);

	RenderPassClearState clear16;
	clear16.clear_color = true;
	clear16.clear_color_r = 0.25f;
	clear16.clear_color_g = 0.50f;
	clear16.clear_color_b = 0.75f;
	clear16.clear_color_a = 1.0f;
	clear16.clear_depth = true;
	clear16.clear_depth_value = 1.0f;

	RenderPassClearState clear32;
	clear32.clear_color = true;
	clear32.clear_color_r = 0.15f;
	clear32.clear_color_g = 0.35f;
	clear32.clear_color_b = 0.55f;
	clear32.clear_color_a = 1.0f;
	clear32.clear_depth = true;
	clear32.clear_depth_value = 1.0f;

	renderer->begin_frame();
	renderer->begin_pass(offscreen16, clear16);
	renderer->end_pass();
	renderer->begin_pass(offscreen32, clear32);
	renderer->end_pass();
	renderer->finish_frame();

	offscreen_texture16->download();
	auto offscreen_texture16_mapping = offscreen_texture16->read();
	unsigned short const *pixels16 = offscreen_texture16_mapping.as<unsigned short const>().data();
	BOOST_REQUIRE(pixels16);

	size_t const center16 = (16u * 32u + 16u) * 4u;
	bool any_color_nonzero = pixels16[center16 + 0] != 0
		|| pixels16[center16 + 1] != 0
		|| pixels16[center16 + 2] != 0;
	BOOST_CHECK(any_color_nonzero);
	BOOST_CHECK_EQUAL(pixels16[center16 + 3], 0x3C00u);
	offscreen_texture16_mapping.unmap();

	offscreen_texture32->download();
	auto offscreen_texture32_mapping = offscreen_texture32->read();
	float const *pixels32 = offscreen_texture32_mapping.as<float const>().data();
	BOOST_REQUIRE(pixels32);

	size_t const center32 = (16u * 32u + 16u) * 4u;
	BOOST_CHECK_SMALL(std::abs(pixels32[center32 + 0] - clear32.clear_color_r), 0.02f);
	BOOST_CHECK_SMALL(std::abs(pixels32[center32 + 1] - clear32.clear_color_g), 0.02f);
	BOOST_CHECK_SMALL(std::abs(pixels32[center32 + 2] - clear32.clear_color_b), 0.02f);
	BOOST_CHECK_SMALL(std::abs(pixels32[center32 + 3] - clear32.clear_color_a), 0.02f);
	offscreen_texture32_mapping.unmap();
}

BOOST_AUTO_TEST_CASE(multi_target_reuse_geometry)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr offscreen_a = renderer->create_render_target(256, 256, PixelFormat::X8R8G8B8);
	RenderTargetPtr offscreen_b = renderer->create_render_target(256, 256, PixelFormat::X8R8G8B8);
	BOOST_REQUIRE(offscreen_a);
	BOOST_REQUIRE(offscreen_b);
	TexturePtr offscreen_texture_a = offscreen_a->color_texture();
	TexturePtr offscreen_texture_b = offscreen_b->color_texture();
	BOOST_REQUIRE(offscreen_texture_a);
	BOOST_REQUIRE(offscreen_texture_b);

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 3);
	TexturePtr src_tex_a = renderer->create_texture(2, 2, PixelFormat::X8R8G8B8);
	TexturePtr src_tex_b = renderer->create_texture(2, 2, PixelFormat::X8R8G8B8);
	MaterialPtr write_mat_a = renderer->create_material();
	MaterialPtr write_mat_b = renderer->create_material();
	MaterialPtr sample_mat_a = renderer->create_material();
	MaterialPtr sample_mat_b = renderer->create_material();

	{
		auto vb_mapping = vb->write(Contents::DISCARD);
		VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
		v[0].position = rove::vec<3>(-1, -1, 0); v[0].texcoords = rove::vec<2>(0, 0);
		v[1].position = rove::vec<3>( 1, -1, 0); v[1].texcoords = rove::vec<2>(1, 0);
		v[2].position = rove::vec<3>( 0,  1, 0); v[2].texcoords = rove::vec<2>(0.5f, 1);
		vb_mapping.unmap();
	}

	{
		auto ib_mapping = ib->write(Contents::DISCARD);
		INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
		idx[0] = 0;
		idx[1] = 1;
		idx[2] = 2;
		ib_mapping.unmap();
	}

	{
		auto src_tex_a_mapping = src_tex_a->write(Contents::DISCARD);
		unsigned *p = src_tex_a_mapping.as<unsigned>().data();
		p[0] = p[1] = p[2] = p[3] = 0xFF0000FFu;
		src_tex_a_mapping.unmap();
	}

	{
		auto src_tex_b_mapping = src_tex_b->write(Contents::DISCARD);
		unsigned *p = src_tex_b_mapping.as<unsigned>().data();
		p[0] = p[1] = p[2] = p[3] = 0xFFFF0000u;
		src_tex_b_mapping.unmap();
	}

	write_mat_a->set_diffuse_texture(src_tex_a);
	write_mat_b->set_diffuse_texture(src_tex_b);
	sample_mat_a->set_diffuse_texture(offscreen_texture_a);
	sample_mat_b->set_diffuse_texture(offscreen_texture_b);

	CameraPtr camera(new Camera());
	camera->set_perspective_projection(1.0f, 800.0f / 600.0f, 0.1f, 100.0f);
	camera->look_at(
		rove::vec<3>(0, 0, -5),
		rove::vec<3>(0, 0, 0),
		rove::vec<3>(0, 1, 0));

	DrawCallPtr write_a = renderer->create_draw_call();
	write_a->set_vertex_buffer(vb);
	write_a->set_index_buffer(ib);
	write_a->set_material(write_mat_a);
	write_a->set_camera(camera);
	write_a->set_triangles_count(1);
	write_a->set_always_visible(true);
	write_a->commit();

	DrawCallPtr write_b = renderer->create_draw_call();
	write_b->set_vertex_buffer(vb);
	write_b->set_index_buffer(ib);
	write_b->set_material(write_mat_b);
	write_b->set_camera(camera);
	write_b->set_triangles_count(1);
	write_b->set_always_visible(true);
	write_b->commit();

	DrawCallPtr sample_a = renderer->create_draw_call();
	sample_a->set_vertex_buffer(vb);
	sample_a->set_index_buffer(ib);
	sample_a->set_material(sample_mat_a);
	sample_a->set_camera(camera);
	sample_a->set_triangles_count(1);
	sample_a->set_always_visible(true);
	sample_a->commit();

	DrawCallPtr sample_b = renderer->create_draw_call();
	sample_b->set_vertex_buffer(vb);
	sample_b->set_index_buffer(ib);
	sample_b->set_material(sample_mat_b);
	sample_b->set_camera(camera);
	sample_b->set_triangles_count(1);
	sample_b->set_always_visible(true);
	sample_b->commit();

	RenderPassClearState clear_a;
	clear_a.clear_color_r = 0.05f;
	clear_a.clear_color_g = 0.10f;
	clear_a.clear_color_b = 0.15f;

	RenderPassClearState clear_b;
	clear_b.clear_color_r = 0.20f;
	clear_b.clear_color_g = 0.05f;
	clear_b.clear_color_b = 0.10f;

	renderer->begin_frame();
	renderer->begin_pass(offscreen_a, clear_a);
	write_a->execute();
	renderer->end_pass();
	renderer->begin_pass(offscreen_b, clear_b);
	write_b->execute();
	renderer->end_pass();
	renderer->begin_pass(renderer->default_render_target());
	sample_a->execute();
	sample_b->execute();
	renderer->end_pass();
	renderer->finish_frame();

	renderer->begin_frame();
	renderer->begin_pass(offscreen_b, clear_b);
	write_b->execute();
	renderer->end_pass();
	renderer->begin_pass(offscreen_a, clear_a);
	write_a->execute();
	renderer->end_pass();
	renderer->begin_pass(renderer->default_render_target());
	sample_b->execute();
	sample_a->execute();
	renderer->end_pass();
	renderer->finish_frame();
}

/// A renderer has the size it is given: none, or a negative one, is a mistake of the caller's, not 800 x 600.
BOOST_AUTO_TEST_CASE(a_renderer_without_a_size_is_refused)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	BOOST_CHECK_THROW(VulkanRenderer(0, 600), std::invalid_argument);
	BOOST_CHECK_THROW(VulkanRenderer(800, -1), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->resize(0, 600), std::invalid_argument);
	BOOST_CHECK_EQUAL(renderer->screen_width(), 800);
	renderer->resize(320, 200);
	BOOST_CHECK_EQUAL(renderer->screen_width(), 320);
	BOOST_CHECK_EQUAL(renderer->screen_height(), 200);
}

/// The default target is the renderer's size, after a resize too.
BOOST_AUTO_TEST_CASE(the_default_target_has_the_renderers_size)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	BOOST_CHECK_EQUAL(renderer->default_render_target()->width(), 800);
	renderer->resize(320, 200);
	RenderTargetPtr const target = renderer->default_render_target();
	BOOST_CHECK_EQUAL(target->width(), 320);
	BOOST_CHECK_EQUAL(target->height(), 200);
	BOOST_CHECK(renderer->default_render_target() == target);
}

/// A capacity set in the environment that is not a number is a mistake to say, not one to replace by the default.
BOOST_AUTO_TEST_CASE(a_malformed_capacity_in_the_environment_is_refused)
{
	for (char const *value : {"lots", "12x", "0"})
	{
		setenv("EDGE_VK_JOINT_MATRICES", value, 1);
		try
		{
			VulkanRenderer r(64, 64);
			BOOST_ERROR("EDGE_VK_JOINT_MATRICES=" << value << " was taken");
		}
		catch (std::invalid_argument const &e)
		{
			BOOST_CHECK_MESSAGE(std::string(e.what()).find("EDGE_VK_JOINT_MATRICES") != std::string::npos, e.what());
		}
	}
	unsetenv("EDGE_VK_JOINT_MATRICES");
}

namespace
{

/// `count` skinned draws of `joints` joints each
std::vector<int> draws(int count, int joints)
{
	return std::vector<int>(static_cast<size_t>(count), joints);
}

std::vector<int> operator+(std::vector<int> a, std::vector<int> const &b)
{
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

/// How many of the skinned draws `joints` (the joints of each) one frame of `r` takes before it refuses one; the refusal in `refusal`.
int skinned_draws_a_frame_takes(VulkanRenderer &r, std::vector<int> const &joints, std::string &refusal)
{
	r.begin_frame();
	int taken = 0;
	try
	{
		for (int n : joints)
		{
			r.update_joint_matrices(std::vector<rove::matrix<4,4>>(static_cast<size_t>(n)));
			++taken;
		}
	}
	catch (std::runtime_error const &e)
	{
		refusal = e.what();
	}
	r.finish_frame();
	return taken;
}

int skinned_draws_a_frame_takes(VulkanRenderer &r, std::vector<int> const &joints)
{
	std::string refusal;
	return skinned_draws_a_frame_takes(r, joints, refusal);
}

}

/// joint_matrices is how many joint matrices a frame has room for, a skinned draw taking as many as it has joints (not a block of 256
/// each): 1024 are four draws of 256 joints, or 256 of 4, or three of 256 and 64 of 4; the next is refused naming the argument, and
/// the next frame has them all again.
BOOST_AUTO_TEST_CASE(joint_matrices_is_how_many_joint_matrices_a_frame_takes)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	unsetenv("EDGE_VK_JOINT_MATRICES");
	VulkanRenderer r(64, 64, std::string(), std::vector<std::string>(), 1, 1024);
	std::string refusal;
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(5, 256), refusal), 4);
	BOOST_CHECK_MESSAGE(refusal.find("joint_matrices") != std::string::npos, refusal);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(257, 4)), 256);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(3, 256) + draws(65, 4)), 3 + 64);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(64, 4) + draws(4, 256)), 64 + 3);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(5, 256)), 4);
}

/// A skinned draw's joint matrices begin past the last draw's (none of them overwritten, whatever the sizes) and at most an offset
/// alignment (256 bytes at most, Vulkan's limit) past: packed, not a block of 256 matrices each.
BOOST_AUTO_TEST_CASE(a_skinned_draws_joint_matrices_follow_the_last_draws)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	std::vector<size_t> const joints{3, 168, 1, 256, 4, 77, 2, 2, 255};
	renderer->begin_frame();
	std::vector<uint32_t> offsets;
	for (size_t n : joints)
		offsets.push_back(renderer->update_joint_matrices(std::vector<rove::matrix<4,4>>(n)));
	renderer->finish_frame();
	size_t const matrix = sizeof(rove::matrix<4,4>);
	for (size_t k = 1; k < joints.size(); ++k)
	{
		BOOST_CHECK_MESSAGE(offsets[k] >= offsets[k - 1] + joints[k - 1] * matrix,
			"draw " << k << " at " << offsets[k] << " over the " << joints[k - 1] << " matrices of draw " << k - 1 << " at " << offsets[k - 1]);
		BOOST_CHECK_MESSAGE(offsets[k] < offsets[k - 1] + joints[k - 1] * matrix + 256,
			"draw " << k << " at " << offsets[k] << ", draw " << k - 1 << " of " << joints[k - 1] << " matrices at " << offsets[k - 1]);
	}
}

/// The joint matrices are shared out among the frames in flight.
BOOST_AUTO_TEST_CASE(joint_matrices_are_shared_out_among_the_frames_in_flight)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	unsetenv("EDGE_VK_JOINT_MATRICES");
	VulkanRenderer r(64, 64, std::string(), std::vector<std::string>(), 2, 2048);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(5, 256)), 4);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(5, 256)), 4);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(5, 256)), 4);
}

/// Left out, a renderer has room for 1024 draws of 256 joints (16 MB), or as many matrices in smaller draws.
BOOST_AUTO_TEST_CASE(a_renderer_has_the_default_joint_matrices_unless_told)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	unsetenv("EDGE_VK_JOINT_MATRICES");
	BOOST_CHECK_EQUAL(VulkanRenderer::DEFAULT_JOINT_MATRICES, 1024 * 256);
	VulkanRenderer r(64, 64);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(1025, 256)), 1024);
	BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(65537, 4)), 65536);
}

/// EDGE_VK_JOINT_MATRICES, set, is taken over the argument: the one who runs the program changes what it asked for without building it
/// again (more, or fewer).
BOOST_AUTO_TEST_CASE(the_environment_overrides_joint_matrices)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	for (std::pair<char const *, int> const &value_draws : {std::pair("2048", 8), std::pair("512", 2)})
	{
		setenv("EDGE_VK_JOINT_MATRICES", value_draws.first, 1);
		try
		{
			VulkanRenderer r(64, 64, std::string(), std::vector<std::string>(), 1, 1024);
			BOOST_CHECK_EQUAL(skinned_draws_a_frame_takes(r, draws(100, 256)), value_draws.second);
		}
		catch (...)
		{
			unsetenv("EDGE_VK_JOINT_MATRICES");
			throw;
		}
	}
	unsetenv("EDGE_VK_JOINT_MATRICES");
}

/// The variable's name before it counted matrices: set, it would be ignored (and its number read as blocks of 256 would be 256 times
/// too few), so it is an error naming the new one.
BOOST_AUTO_TEST_CASE(the_environments_old_joint_matrix_blocks_is_refused)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	unsetenv("EDGE_VK_JOINT_MATRICES");
	setenv("EDGE_VK_JOINT_MATRIX_BLOCKS", "4096", 1);
	try
	{
		VulkanRenderer r(64, 64);
		BOOST_ERROR("EDGE_VK_JOINT_MATRIX_BLOCKS was taken");
	}
	catch (std::invalid_argument const &e)
	{
		std::string const what = e.what();
		BOOST_CHECK_MESSAGE(what.find("EDGE_VK_JOINT_MATRIX_BLOCKS") != std::string::npos
			&& what.find("EDGE_VK_JOINT_MATRICES") != std::string::npos, what);
	}
	unsetenv("EDGE_VK_JOINT_MATRIX_BLOCKS");
}

/// No joint matrices, or fewer than the frames in flight (one would get none), is the caller's mistake, said before any device is made.
BOOST_AUTO_TEST_CASE(too_few_joint_matrices_are_refused)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	unsetenv("EDGE_VK_JOINT_MATRICES");
	for (std::pair<int, int> const &frames_matrices : {std::pair(1, 0), std::pair(1, -4), std::pair(3, 2)})
	{
		try
		{
			VulkanRenderer r(64, 64, std::string(), std::vector<std::string>(), frames_matrices.first, frames_matrices.second);
			BOOST_ERROR(frames_matrices.second << " joint matrices for " << frames_matrices.first << " frames were taken");
		}
		catch (std::invalid_argument const &e)
		{
			BOOST_CHECK_MESSAGE(std::string(e.what()).find("joint_matrices") != std::string::npos, e.what());
		}
	}
}

BOOST_AUTO_TEST_SUITE_END()
