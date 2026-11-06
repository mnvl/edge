// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract tests for where Vulkan buffer/staging allocations live and for the
// staged-upload path that feeds device-local vertex/index buffers.
//
// Two defects motivated these tests:
//   1. vertex/index buffers were allocated in plain HOST_VISIBLE memory, so a
//      discrete GPU streamed all geometry across PCIe on every draw.
//   2. texture staging memory did not ask for HOST_CACHED, so readback memcpy
//      read uncached write-combined memory at ~80 MB/s.
// Both are invisible to a pure "does it render" test — they only cost time —
// so the allocation properties themselves are part of the contract here, next
// to black-box tests proving the staged uploads actually reach the GPU.

#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "uber_draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "test_vulkan_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "vulkan_index_buffer.h"
#include "vulkan_texture.h"
#include "vulkan_vertex_buffer.h"

using namespace edge;

namespace
{

/// Writes @p value into every byte of @p texture's CPU copy and leaves it unsent (Mapping::release()): what batch_upload_textures() and
/// stream_textures() send.
void write_unsent(Texture &texture, int value)
{
	auto texels = texture.write(Contents::DISCARD);
	std::memset(texels.data(), value, texels.size());
	texels.release();
}

/// Byte @p i of @p texture's CPU copy.
std::uint8_t texel_byte(Texture const &texture, std::size_t i)
{
	return texture.read().as<std::uint8_t const>()[i];
}

int const TARGET_SIZE = 64;

// Clear colour is pure blue; drawn geometry samples a pure green texture.
// Any shading of green leaves the blue channel low, so the blue channel alone
// tells covered pixels from cleared ones without depending on the lighting
// model.
float const CLEAR_R = 0.0f;
float const CLEAR_G = 0.0f;
float const CLEAR_B = 1.0f;

unsigned const GREEN_TEXEL = 0xFF00FF00u;

RenderPassClearState make_clear_state()
{
	RenderPassClearState clear_state;
	clear_state.clear_color = true;
	clear_state.clear_color_r = CLEAR_R;
	clear_state.clear_color_g = CLEAR_G;
	clear_state.clear_color_b = CLEAR_B;
	clear_state.clear_color_a = 1.0f;
	clear_state.clear_depth = true;
	clear_state.clear_depth_value = 1.0f;
	return clear_state;
}

// Quad covering x in [x0, x1] and the full y range, at z = 0.
void write_quad(VertexBufferPtr const &vb, float x0, float x1)
{
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	BOOST_REQUIRE(v);
	v[0].position = rove::vec<3>(x0, -1.0f, 0.0f); v[0].texcoords = rove::vec<2>(0, 0);
	v[1].position = rove::vec<3>(x1, -1.0f, 0.0f); v[1].texcoords = rove::vec<2>(1, 0);
	v[2].position = rove::vec<3>(x1,  1.0f, 0.0f); v[2].texcoords = rove::vec<2>(1, 1);
	v[3].position = rove::vec<3>(x0,  1.0f, 0.0f); v[3].texcoords = rove::vec<2>(0, 1);
	vb_mapping.unmap();
}

void write_quad_indices(IndexBufferPtr const &ib)
{
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
	BOOST_REQUIRE(idx);
	idx[0] = 0; idx[1] = 1; idx[2] = 2;
	idx[3] = 0; idx[4] = 2; idx[5] = 3;
	ib_mapping.unmap();
}

// Degenerate indices: every triangle collapses to a point, so nothing is
// rasterised even though the vertex buffer still holds a full-screen quad.
void write_degenerate_indices(IndexBufferPtr const &ib)
{
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
	BOOST_REQUIRE(idx);
	for (int i = 0; i < 6; ++i)
	{
		idx[i] = 0;
	}
	ib_mapping.unmap();
}

CameraPtr make_ortho_camera()
{
	// World x,y in [-1, 1] maps exactly onto the render target.
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(
		rove::vec<3>(0, 0, 1),
		rove::vec<3>(0, 0, 0),
		rove::vec<3>(0, 1, 0));
	return camera;
}

TexturePtr make_green_texture(VulkanRendererPtr const &renderer)
{
	TexturePtr texture = renderer->create_texture(2, 2, PixelFormat::X8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	unsigned *p = texture_mapping.as<unsigned>().data();
	BOOST_REQUIRE(p);
	p[0] = p[1] = p[2] = p[3] = GREEN_TEXEL;
	texture_mapping.unmap();
	return texture;
}

// The uber draw call is the path production geometry goes through; it draws
// the texture's colour.
DrawCallPtr make_quad_draw_call(VulkanRendererPtr const &renderer,
	VertexBufferPtr const &vb, IndexBufferPtr const &ib,
	MaterialPtr const &material, CameraPtr const &camera)
{
	DrawCallPtr draw_call = renderer->create_uber_draw_call();
	draw_call->set_vertex_buffer(vb);
	draw_call->set_index_buffer(ib);
	draw_call->set_material(material);
	draw_call->set_camera(camera);
	draw_call->set_triangles_count(2);
	draw_call->set_always_visible(true);
	draw_call->commit();
	return draw_call;
}

uint32_t read_pixel(TexturePtr const &texture, int x, int y)
{
	texture->download();
	auto texture_mapping = texture->read();
	uint32_t const *pixels = texture_mapping.as<uint32_t const>().data();
	BOOST_REQUIRE(pixels);
	uint32_t const value = pixels[y * TARGET_SIZE + x];
	texture_mapping.unmap();
	return value;
}

int blue_of(uint32_t pixel)
{
	return static_cast<int>(pixel & 0xFFu);
}

int green_of(uint32_t pixel)
{
	return static_cast<int>((pixel >> 8) & 0xFFu);
}

// Cleared pixels are pure blue; covered pixels sample a green texel, whose
// blue channel is zero however it ends up being shaded.
bool is_cleared(uint32_t pixel)
{
	return blue_of(pixel) > 200;
}

bool is_covered(uint32_t pixel)
{
	return blue_of(pixel) < 128;
}

int const LEFT_X = TARGET_SIZE / 4;
int const RIGHT_X = (3 * TARGET_SIZE) / 4;
int const MID_Y = TARGET_SIZE / 2;

bool device_offers_cached_host_visible_type(VkPhysicalDevice device)
{
	VkPhysicalDeviceMemoryProperties properties;
	vkGetPhysicalDeviceMemoryProperties(device, &properties);

	VkMemoryPropertyFlags const wanted =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
		VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
	{
		if ((properties.memoryTypes[i].propertyFlags & wanted) == wanted)
		{
			return true;
		}
	}

	return false;
}

}

BOOST_FIXTURE_TEST_SUITE(vulkan_buffer_memory, VulkanFixture)

// ── Allocation-property contract ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(vertex_buffer_lives_in_device_local_memory)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	std::shared_ptr<VulkanVertexBuffer> vulkan_vb =
		std::dynamic_pointer_cast<VulkanVertexBuffer>(vb);
	BOOST_REQUIRE(vulkan_vb);

	BOOST_CHECK((vulkan_vb->memory_properties() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0);
}

BOOST_AUTO_TEST_CASE(index_buffer_lives_in_device_local_memory)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	std::shared_ptr<VulkanIndexBuffer> vulkan_ib =
		std::dynamic_pointer_cast<VulkanIndexBuffer>(ib);
	BOOST_REQUIRE(vulkan_ib);

	BOOST_CHECK((vulkan_ib->memory_properties() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0);
}

BOOST_AUTO_TEST_CASE(texture_staging_prefers_cached_host_memory)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	TexturePtr texture = renderer->create_texture(16, 16, PixelFormat::X8R8G8B8);
	std::shared_ptr<VulkanTexture> vulkan_texture =
		std::dynamic_pointer_cast<VulkanTexture>(texture);
	BOOST_REQUIRE(vulkan_texture);

	VkMemoryPropertyFlags const staging = vulkan_texture->staging_memory_properties();

	// Readback memcpys out of this memory, so it must be host visible and
	// coherent, and cached wherever the device offers a cached type.
	BOOST_CHECK((staging & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
	BOOST_CHECK((staging & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0);

	bool const cached_available = device_offers_cached_host_visible_type(
		renderer->physical_device());
	BOOST_CHECK_EQUAL((staging & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0, cached_available);
}

// ── Staged uploads must actually reach the GPU ──────────────────────────────

BOOST_AUTO_TEST_CASE(upload_before_frame_is_rendered)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr target = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	BOOST_REQUIRE(target);

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(make_green_texture(renderer));
	// The subject here is buffer contents, not winding order.
	material->set_cull_mode(Material::CullMode::NONE);

	// Left half only: proves the *contents* of the buffer arrived, not merely
	// that some geometry was drawn.
	write_quad(vb, -1.0f, 0.0f);
	write_quad_indices(ib);

	DrawCallPtr draw_call = make_quad_draw_call(renderer, vb, ib, material, make_ortho_camera());

	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	TexturePtr color = target->color_texture();
	uint32_t const left = read_pixel(color, LEFT_X, MID_Y);
	uint32_t const right = read_pixel(color, RIGHT_X, MID_Y);

	BOOST_CHECK_MESSAGE(is_covered(left),
		"left pixel should show geometry, got 0x" << std::hex << left);
	BOOST_CHECK_MESSAGE(is_cleared(right),
		"right pixel should show clear colour, got 0x" << std::hex << right);
}

BOOST_AUTO_TEST_CASE(reupload_between_frames_replaces_geometry)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr target = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(make_green_texture(renderer));
	// The subject here is buffer contents, not winding order.
	material->set_cull_mode(Material::CullMode::NONE);

	write_quad(vb, -1.0f, 0.0f);
	write_quad_indices(ib);

	DrawCallPtr draw_call = make_quad_draw_call(renderer, vb, ib, material, make_ortho_camera());
	TexturePtr color = target->color_texture();

	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	BOOST_CHECK(is_covered(read_pixel(color, LEFT_X, MID_Y)));
	BOOST_CHECK(is_cleared(read_pixel(color, RIGHT_X, MID_Y)));

	// Move the quad to the right half and render again — a second upload into
	// a live buffer must be picked up.
	write_quad(vb, 0.0f, 1.0f);

	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	uint32_t const left = read_pixel(color, LEFT_X, MID_Y);
	uint32_t const right = read_pixel(color, RIGHT_X, MID_Y);

	BOOST_CHECK_MESSAGE(is_cleared(left),
		"left pixel should be clear after the quad moved, got 0x" << std::hex << left);
	BOOST_CHECK_MESSAGE(is_covered(right),
		"right pixel should show geometry after the quad moved, got 0x" << std::hex << right);
}

BOOST_AUTO_TEST_CASE(index_reupload_between_frames_replaces_geometry)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr target = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(make_green_texture(renderer));
	// The subject here is buffer contents, not winding order.
	material->set_cull_mode(Material::CullMode::NONE);

	write_quad(vb, -1.0f, 1.0f);
	write_quad_indices(ib);

	DrawCallPtr draw_call = make_quad_draw_call(renderer, vb, ib, material, make_ortho_camera());
	TexturePtr color = target->color_texture();

	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	BOOST_CHECK(is_covered(read_pixel(color, LEFT_X, MID_Y)));

	// Collapse the triangles: the index upload alone must change the image.
	write_degenerate_indices(ib);

	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	uint32_t const center = read_pixel(color, LEFT_X, MID_Y);
	BOOST_CHECK_MESSAGE(is_cleared(center),
		"degenerate indices should draw nothing, got 0x" << std::hex << center);
}

BOOST_AUTO_TEST_CASE(upload_between_passes_affects_only_later_passes)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr first = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	RenderTargetPtr second = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(make_green_texture(renderer));
	// The subject here is buffer contents, not winding order.
	material->set_cull_mode(Material::CullMode::NONE);

	write_quad(vb, -1.0f, 0.0f);
	write_quad_indices(ib);

	DrawCallPtr draw_call = make_quad_draw_call(renderer, vb, ib, material, make_ortho_camera());

	renderer->begin_frame();

	renderer->begin_pass(first, make_clear_state());
	draw_call->execute();
	renderer->end_pass();

	// Mid-frame update, outside any pass: the copy has to be ordered after the
	// draws already recorded and before the ones that follow.
	write_quad(vb, 0.0f, 1.0f);

	renderer->begin_pass(second, make_clear_state());
	draw_call->execute();
	renderer->end_pass();

	renderer->finish_frame();

	TexturePtr first_color = first->color_texture();
	TexturePtr second_color = second->color_texture();

	BOOST_CHECK_MESSAGE(is_covered(read_pixel(first_color, LEFT_X, MID_Y)),
		"first pass should still show the original left-half quad");
	BOOST_CHECK_MESSAGE(is_cleared(read_pixel(first_color, RIGHT_X, MID_Y)),
		"first pass right side should be clear");
	BOOST_CHECK_MESSAGE(is_cleared(read_pixel(second_color, LEFT_X, MID_Y)),
		"second pass left side should be clear after the update");
	BOOST_CHECK_MESSAGE(is_covered(read_pixel(second_color, RIGHT_X, MID_Y)),
		"second pass should show the updated right-half quad");
}

BOOST_AUTO_TEST_CASE(many_buffers_uploaded_before_one_frame_all_arrive)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr target = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(make_green_texture(renderer));
	// The subject here is buffer contents, not winding order.
	material->set_cull_mode(Material::CullMode::NONE);
	CameraPtr camera = make_ortho_camera();

	// Four quads, each covering one vertical quarter, uploaded while the
	// renderer is idle: every pending copy has to be flushed, not just the last.
	int const STRIPES = 4;
	std::vector<DrawCallPtr> draw_calls;
	std::vector<VertexBufferPtr> vertex_buffers;
	std::vector<IndexBufferPtr> index_buffers;

	for (int i = 0; i < STRIPES; ++i)
	{
		VertexBufferPtr vb = renderer->create_vertex_buffer(
			VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
		IndexBufferPtr ib = renderer->create_index_buffer(
			IndexFormat::U16, sizeof(INDEX_U16) * 6);

		float const x0 = -1.0f + 2.0f * static_cast<float>(i) / STRIPES;
		float const x1 = -1.0f + 2.0f * static_cast<float>(i + 1) / STRIPES;
		write_quad(vb, x0, x1);
		write_quad_indices(ib);

		vertex_buffers.push_back(vb);
		index_buffers.push_back(ib);
		draw_calls.push_back(make_quad_draw_call(renderer, vb, ib, material, camera));
	}

	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	for (auto &draw_call : draw_calls)
	{
		draw_call->execute();
	}
	renderer->end_pass();
	renderer->finish_frame();

	TexturePtr color = target->color_texture();
	for (int i = 0; i < STRIPES; ++i)
	{
		int const x = (TARGET_SIZE * (2 * i + 1)) / (2 * STRIPES);
		uint32_t const pixel = read_pixel(color, x, MID_Y);
		BOOST_CHECK_MESSAGE(is_covered(pixel),
			"stripe " << i << " missing at x=" << x << ", got 0x" << std::hex << pixel);
	}
}

BOOST_AUTO_TEST_CASE(buffer_destroyed_before_flush_does_not_corrupt_next_frame)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr target = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(make_green_texture(renderer));
	// The subject here is buffer contents, not winding order.
	material->set_cull_mode(Material::CullMode::NONE);
	CameraPtr camera = make_ortho_camera();

	// Upload into a buffer and drop it before any frame flushes the copy: the
	// pending upload must go with it rather than referencing a dead VkBuffer.
	{
		VertexBufferPtr doomed_vb = renderer->create_vertex_buffer(
			VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
		IndexBufferPtr doomed_ib = renderer->create_index_buffer(
			IndexFormat::U16, sizeof(INDEX_U16) * 6);
		write_quad(doomed_vb, -1.0f, 1.0f);
		write_quad_indices(doomed_ib);
	}

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	write_quad(vb, -1.0f, 0.0f);
	write_quad_indices(ib);

	DrawCallPtr draw_call = make_quad_draw_call(renderer, vb, ib, material, camera);

	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	TexturePtr color = target->color_texture();
	BOOST_CHECK(is_covered(read_pixel(color, LEFT_X, MID_Y)));
	BOOST_CHECK(is_cleared(read_pixel(color, RIGHT_X, MID_Y)));
}

BOOST_AUTO_TEST_CASE(buffer_destroyed_mid_frame_after_upload_is_safe)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr target = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(make_green_texture(renderer));
	material->set_cull_mode(Material::CullMode::NONE);
	CameraPtr camera = make_ortho_camera();

	VertexBufferPtr vb = renderer->create_vertex_buffer(
		VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	write_quad(vb, -1.0f, 0.0f);
	write_quad_indices(ib);
	DrawCallPtr draw_call = make_quad_draw_call(renderer, vb, ib, material, camera);

	renderer->begin_frame();

	// Upload into a buffer that is then dropped while the frame is still open:
	// its copy is already recorded in the frame's command buffer, so the
	// handles must outlive the submit.
	{
		VertexBufferPtr doomed_vb = renderer->create_vertex_buffer(
			VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
		IndexBufferPtr doomed_ib = renderer->create_index_buffer(
			IndexFormat::U16, sizeof(INDEX_U16) * 6);
		write_quad(doomed_vb, 0.0f, 1.0f);
		write_quad_indices(doomed_ib);
	}

	renderer->begin_pass(target, make_clear_state());
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	TexturePtr color = target->color_texture();
	BOOST_CHECK(is_covered(read_pixel(color, LEFT_X, MID_Y)));
	BOOST_CHECK(is_cleared(read_pixel(color, RIGHT_X, MID_Y)));

	// The frame completed, so the orphaned buffers are gone by now; another
	// frame must still work.
	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	BOOST_CHECK(is_covered(read_pixel(color, LEFT_X, MID_Y)));
}

BOOST_AUTO_TEST_CASE(empty_buffers_are_safe_to_create_and_unlock)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), 0);
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, 0);
	BOOST_REQUIRE(vb);
	BOOST_REQUIRE(ib);

	BOOST_CHECK_EQUAL(vb->size(), 0u);
	BOOST_CHECK_EQUAL(ib->size(), 0u);

	auto vb_mapping = vb->write(Contents::DISCARD);
	vb_mapping.unmap();
	auto ib_mapping = ib->write(Contents::DISCARD);
	ib_mapping.unmap();

	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	renderer->end_pass();
	renderer->finish_frame();
}

// ── Texture readback must never serve stale bytes ───────────────────────────

BOOST_AUTO_TEST_CASE(download_returns_fresh_pixels_after_every_render)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr target = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	TexturePtr color = target->color_texture();
	BOOST_REQUIRE(color);

	// Three different clear colours in a row: cached staging memory must be
	// re-read each time, never served from a stale CPU copy.
	float const colors[3][3] = {
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f },
	};

	for (int i = 0; i < 3; ++i)
	{
		RenderPassClearState clear_state;
		clear_state.clear_color = true;
		clear_state.clear_color_r = colors[i][0];
		clear_state.clear_color_g = colors[i][1];
		clear_state.clear_color_b = colors[i][2];
		clear_state.clear_color_a = 1.0f;

		renderer->begin_frame();
		renderer->begin_pass(target, clear_state);
		renderer->end_pass();
		renderer->finish_frame();

		uint32_t const pixel = read_pixel(color, TARGET_SIZE / 2, MID_Y);
		int const r = static_cast<int>((pixel >> 16) & 0xFFu);
		int const g = green_of(pixel);
		int const b = blue_of(pixel);

		BOOST_CHECK_MESSAGE((r > 200) == (colors[i][0] > 0.5f),
			"red channel stale at step " << i << ", got 0x" << std::hex << pixel);
		BOOST_CHECK_MESSAGE((g > 200) == (colors[i][1] > 0.5f),
			"green channel stale at step " << i << ", got 0x" << std::hex << pixel);
		BOOST_CHECK_MESSAGE((b > 200) == (colors[i][2] > 0.5f),
			"blue channel stale at step " << i << ", got 0x" << std::hex << pixel);
	}
}

BOOST_AUTO_TEST_CASE(batch_download_returns_fresh_pixels_after_every_render)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);

	RenderTargetPtr first = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	RenderTargetPtr second = renderer->create_render_target(
		TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);

	std::vector<TexturePtr> textures;
	textures.push_back(first->color_texture());
	textures.push_back(second->color_texture());

	for (int i = 0; i < 2; ++i)
	{
		RenderPassClearState first_clear;
		first_clear.clear_color = true;
		first_clear.clear_color_r = i == 0 ? 1.0f : 0.0f;
		first_clear.clear_color_g = 0.0f;
		first_clear.clear_color_b = i == 0 ? 0.0f : 1.0f;
		first_clear.clear_color_a = 1.0f;

		RenderPassClearState second_clear;
		second_clear.clear_color = true;
		second_clear.clear_color_r = 0.0f;
		second_clear.clear_color_g = i == 0 ? 1.0f : 0.0f;
		second_clear.clear_color_b = i == 0 ? 0.0f : 1.0f;
		second_clear.clear_color_a = 1.0f;

		renderer->begin_frame();
		renderer->begin_pass(first, first_clear);
		renderer->end_pass();
		renderer->begin_pass(second, second_clear);
		renderer->end_pass();
		renderer->finish_frame();

		renderer->batch_download_textures(textures);

		auto textures_mapping = textures[0]->read();
		uint32_t const *first_pixels = textures_mapping.as<uint32_t const>().data();
		uint32_t const first_pixel = first_pixels[MID_Y * TARGET_SIZE + TARGET_SIZE / 2];
		textures_mapping.unmap();

		auto textures_mapping2 = textures[1]->read();
		uint32_t const *second_pixels = textures_mapping2.as<uint32_t const>().data();
		uint32_t const second_pixel = second_pixels[MID_Y * TARGET_SIZE + TARGET_SIZE / 2];
		textures_mapping2.unmap();

		int const first_r = static_cast<int>((first_pixel >> 16) & 0xFFu);
		int const second_g = green_of(second_pixel);

		BOOST_CHECK_MESSAGE((first_r > 200) == (i == 0),
			"first target stale at step " << i << ", got 0x" << std::hex << first_pixel);
		BOOST_CHECK_MESSAGE((second_g > 200) == (i == 0),
			"second target stale at step " << i << ", got 0x" << std::hex << second_pixel);
	}
}

/// A batch refuses what one download refuses, before it downloads anything: an array (no one image to read), a texture without
/// its CPU copy (nowhere to put the texels), and no texture at all.
BOOST_AUTO_TEST_CASE(batch_download_refuses_what_download_refuses)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	TexturePtr const fine = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, TextureUsage());
	auto fine_mapping = fine->write(Contents::DISCARD);
	std::memset(fine_mapping.data(), 0x7F, 4 * 4 * 4);
	fine_mapping.unmap();
	TextureUsage array_usage;
	array_usage.array_layers = 2;
	TexturePtr const array = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, array_usage);
	TextureUsage no_copy_usage;
	no_copy_usage.keep_cpu_copy = false;
	TexturePtr const no_copy = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, no_copy_usage);
	for (TexturePtr const &texture : {array, no_copy})
	{
		BOOST_CHECK_THROW(texture->download(), std::logic_error);
		BOOST_CHECK_THROW(renderer->batch_download_textures({fine, texture}), std::logic_error);
	}
	BOOST_CHECK_THROW(renderer->batch_download_textures({fine, TexturePtr()}), std::invalid_argument);
	renderer->batch_download_textures({fine});
	BOOST_CHECK_EQUAL(int(texel_byte(*fine, 0)), 0x7F);
}

/// Textures written and not sent (Mapping::release()) go to the GPU together: one submit and one wait for the batch, not one each, and
/// what each holds is what was written.
BOOST_AUTO_TEST_CASE(batch_upload_sends_the_written_textures_in_one_submit)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	std::vector<TexturePtr> textures;
	for (int i = 0; i < 3; ++i)
	{
		TexturePtr const texture = renderer->create_texture(8, 8, PixelFormat::A8R8G8B8, TextureUsage());
		write_unsent(*texture, 0x20 + 0x20 * i);
		textures.push_back(texture);
	}
	uint64_t const before = renderer->single_time_submit_count();
	renderer->batch_upload_textures(textures);
	BOOST_CHECK_EQUAL(renderer->single_time_submit_count(), before + 1);
	for (int i = 0; i < 3; ++i)
	{
		write_unsent(*textures[size_t(i)], 0);                // forget it here: what is read back is the GPU's
		textures[size_t(i)]->download();
		BOOST_CHECK_EQUAL(int(texel_byte(*textures[size_t(i)], 0)), 0x20 + 0x20 * i);
		BOOST_CHECK_EQUAL(int(texel_byte(*textures[size_t(i)], 8 * 8 * 4 - 1)), 0x20 + 0x20 * i);
	}
	// An empty batch costs nothing.
	renderer->batch_upload_textures({});
	BOOST_CHECK_EQUAL(renderer->single_time_submit_count(), before + 1 + 3);   // (the three downloads, one each)
}

/// A batch refuses what a texture's own write() refuses, before it uploads anything: a texture without its CPU copy, a null
/// entry, one named twice.
BOOST_AUTO_TEST_CASE(batch_upload_refuses_before_it_uploads_anything)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	TexturePtr const fine = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, TextureUsage());
	write_unsent(*fine, 0x11);
	TextureUsage no_copy_usage;
	no_copy_usage.keep_cpu_copy = false;
	TexturePtr const no_copy = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, no_copy_usage);
	{
		auto texels = no_copy->write(Contents::DISCARD);
		std::memset(texels.data(), 0x22, texels.size());
	}                                                                      // sent once, its CPU copy and staging gone
	uint64_t const before = renderer->single_time_submit_count();
	BOOST_CHECK_THROW(renderer->batch_upload_textures({fine, no_copy}), std::logic_error);
	BOOST_CHECK_THROW(renderer->batch_upload_textures({fine, TexturePtr()}), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->batch_upload_textures({fine, fine}), std::invalid_argument);
	BOOST_CHECK_EQUAL(renderer->single_time_submit_count(), before);
}

/// Textures streamed inside a frame ride in the frame's own command buffer: no submit and no wait of their own, and the frame
/// sees what was written; the texels are the GPU's after the frame.
BOOST_AUTO_TEST_CASE(stream_textures_ride_in_the_frame_without_a_submit_of_their_own)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	std::vector<TexturePtr> textures;
	for (int i = 0; i < 3; ++i)
		textures.push_back(renderer->create_texture(8, 8, PixelFormat::A8R8G8B8, TextureUsage()));
	RenderTargetPtr const target = renderer->create_render_target(TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	for (int round = 0; round < 3; ++round)                                   // again and again, as a sea is
	{
		for (int i = 0; i < 3; ++i)
			write_unsent(*textures[size_t(i)], 0x10 + 0x30 * i + round);
		uint64_t const before = renderer->single_time_submit_count();
		renderer->begin_frame();
		renderer->stream_textures(textures);
		renderer->begin_pass(target, make_clear_state());
		renderer->end_pass();
		renderer->finish_frame();
		BOOST_CHECK_EQUAL(renderer->single_time_submit_count(), before + 1);        // the frame's, no more
		for (int i = 0; i < 3; ++i)
		{
			write_unsent(*textures[size_t(i)], 0);                // forget it here: what is read back is the GPU's
			textures[size_t(i)]->download();
			BOOST_CHECK_EQUAL(int(texel_byte(*textures[size_t(i)], 0)), 0x10 + 0x30 * i + round);
			BOOST_CHECK_EQUAL(int(texel_byte(*textures[size_t(i)], 8 * 8 * 4 - 1)), 0x10 + 0x30 * i + round);
		}
	}
}

/// Out of a frame there is nothing to ride in: the streamed textures are sent as a batch is.  In a pass, copies are illegal.  A
/// texture with levels or layers is refused, as are a null entry and a texture named twice, before any is sent.
BOOST_AUTO_TEST_CASE(stream_textures_outside_a_frame_are_sent_at_once_and_refuse_what_they_cannot_send)
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	TexturePtr const fine = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, TextureUsage());
	write_unsent(*fine, 0x44);
	renderer->stream_textures({fine});                                       // idle: sent now
	write_unsent(*fine, 0);
	fine->download();
	BOOST_CHECK_EQUAL(int(texel_byte(*fine, 0)), 0x44);
	TextureUsage mips;
	mips.mipmaps = true;
	TexturePtr const mipped = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, mips);
	TextureUsage layers;
	layers.array_layers = 2;
	TexturePtr const array = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, layers);
	for (TexturePtr const &texture : {mipped, array})
		BOOST_CHECK_THROW(renderer->stream_textures({fine, texture}), std::logic_error);
	BOOST_CHECK_THROW(renderer->stream_textures({fine, TexturePtr()}), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->stream_textures({fine, fine}), std::invalid_argument);
	RenderTargetPtr const target = renderer->create_render_target(TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	renderer->begin_frame();
	renderer->begin_pass(target, make_clear_state());
	BOOST_CHECK_THROW(renderer->stream_textures({fine}), std::logic_error);   // inside a pass
	renderer->end_pass();
	renderer->finish_frame();
}

BOOST_AUTO_TEST_SUITE_END()
