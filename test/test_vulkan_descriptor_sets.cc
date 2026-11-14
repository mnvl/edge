// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract tests for the per-material descriptor set cache of the Vulkan
// renderer under a renderer that outlives the scenes it draws.
//
// A renderer reused across scene rebuilds sees an unbounded stream of
// short-lived materials.  The cache used to key a descriptor set by raw
// Material pointer and never release it: after as many materials as the
// descriptor pool's maxSets every later
// vkAllocateDescriptorSets failed with VK_ERROR_OUT_OF_DEVICE_MEMORY and the
// worker died.  A dangling key was also a correctness hole — a new material at
// a recycled address inherited a set written for the old one, pointing the GPU
// at destroyed image views.  That faults the device and vkQueueWaitIdle never
// returns, which is why every case here carries a timeout: a regression must
// fail, not hang the test binary.

#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "uber_draw_call.h"
#include "material.h"
#include "null_renderer.h"
#include "pixel.h"
#include "render_target.h"
#include "test_vulkan_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "vulkan_descriptor_cache.h"
#include "vulkan_renderer.h"

using namespace edge;

namespace
{

int const TARGET_SIZE = 64;

// Opaque texels: an alpha of zero is discarded by the uber shader.  Render
// targets are X8R8G8B8, so comparisons are on the colour channels only.
uint32_t const RED = 0xFFFF0000u;
uint32_t const GREEN = 0xFF00FF00u;

uint32_t rgb(uint32_t argb)
{
	return argb & 0x00FFFFFFu;
}

// The descriptor pool's capacity for this file's renderers: shrunk from VulkanDescriptorCache::MAX_SETS (8192) through
// EDGE_VK_MAX_MATERIAL_SETS so that streaming well past it stays a sub-second test instead of a many-thousand-frame,
// double-digit-second one; the cache's eviction logic does not know or care how big the pool is.
uint32_t const TEST_MAX_SETS = 200;

// Well past what the descriptor pool holds.
size_t const MATERIALS_TO_STREAM = TEST_MAX_SETS + 64;

RenderPassClearState make_clear_state()
{
	RenderPassClearState clear_state;
	clear_state.clear_color = true;
	clear_state.clear_color_r = 0.0f;
	clear_state.clear_color_g = 0.0f;
	clear_state.clear_color_b = 1.0f;
	clear_state.clear_color_a = 1.0f;
	clear_state.clear_depth = true;
	clear_state.clear_depth_value = 1.0f;
	return clear_state;
}

void write_fullscreen_quad(VertexBufferPtr const &vb)
{
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	BOOST_REQUIRE(v);
	v[0].position = rove::vec<3>(-1.0f, -1.0f, 0.0f); v[0].texcoords = rove::vec<2>(0, 0);
	v[1].position = rove::vec<3>( 0.0f, -1.0f, 0.0f); v[1].texcoords = rove::vec<2>(1, 0);
	v[2].position = rove::vec<3>( 0.0f,  1.0f, 0.0f); v[2].texcoords = rove::vec<2>(1, 1);
	v[3].position = rove::vec<3>(-1.0f,  1.0f, 0.0f); v[3].texcoords = rove::vec<2>(0, 1);
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

CameraPtr make_ortho_camera()
{
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(
		rove::vec<3>(0, 0, 1),
		rove::vec<3>(0, 0, 0),
		rove::vec<3>(0, 1, 0));
	return camera;
}

// A 1x1 texture of one solid colour, so a covered pixel reads that colour
// back.
TexturePtr make_solid_texture(VulkanRendererPtr const &renderer, uint32_t argb)
{
	TexturePtr texture = renderer->create_texture(2, 2, PixelFormat::X8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	unsigned *p = texture_mapping.as<unsigned>().data();
	BOOST_REQUIRE(p);
	p[0] = p[1] = p[2] = p[3] = argb;
	texture_mapping.unmap();
	return texture;
}

MaterialPtr make_material(VulkanRendererPtr const &renderer, uint32_t argb)
{
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(make_solid_texture(renderer, argb));
	material->set_cull_mode(Material::CullMode::NONE);
	return material;
}

struct Quad
{
	VertexBufferPtr vb;
	IndexBufferPtr ib;
	CameraPtr camera;
	RenderTargetPtr target;

	explicit Quad(VulkanRendererPtr const &renderer)
	{
		vb = renderer->create_vertex_buffer(
			VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
		ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
		write_fullscreen_quad(vb);
		write_quad_indices(ib);
		camera = make_ortho_camera();
		target = renderer->create_render_target(
			TARGET_SIZE, TARGET_SIZE, PixelFormat::X8R8G8B8);
	}

	void render(VulkanRendererPtr const &renderer, MaterialPtr const &material)
	{
		DrawCallPtr draw_call = renderer->create_uber_draw_call();
		draw_call->set_vertex_buffer(vb);
		draw_call->set_index_buffer(ib);
		draw_call->set_material(material);
		draw_call->set_camera(camera);
		draw_call->set_triangles_count(2);
		draw_call->set_always_visible(true);
		draw_call->commit();

		renderer->begin_frame();
		renderer->begin_pass(target, make_clear_state());
		draw_call->execute();
		renderer->end_pass();
		renderer->finish_frame();
	}

	uint32_t render_with(VulkanRendererPtr const &renderer, MaterialPtr const &material)
	{
		render(renderer, material);
		TexturePtr color = target->color_texture();
		color->download();
		auto color_mapping = color->read();
		uint32_t const *pixels = color_mapping.as<uint32_t const>().data();
		BOOST_REQUIRE(pixels);
		uint32_t const centre = pixels[(TARGET_SIZE / 2) * TARGET_SIZE + TARGET_SIZE / 4];
		color_mapping.unmap();
		return rgb(centre);
	}
};

}

// VulkanFixture with the descriptor pool shrunk to TEST_MAX_SETS (see EDGE_VK_MAX_MATERIAL_SETS, vulkan_renderer.cc): every
// case in this suite gets a renderer whose material descriptor pool is small enough that exhausting it, and proving reclaim
// keeps it from ever doing so, is cheap.  None of the other cases here come close to needing TEST_MAX_SETS live sets at once,
// so the shrink changes nothing about what they exercise.
struct SmallPoolVulkanFixture
{
	VulkanRendererPtr renderer;
	std::string error;

	SmallPoolVulkanFixture()
	{
		setenv("EDGE_VK_MAX_MATERIAL_SETS", std::to_string(TEST_MAX_SETS).c_str(), 1);
		try
		{
			renderer = std::make_shared<VulkanRenderer>(800, 600);
		}
		catch (std::exception const &exception)
		{
			error = exception.what();
			renderer.reset();
		}
		unsetenv("EDGE_VK_MAX_MATERIAL_SETS");
	}

	~SmallPoolVulkanFixture()
	{
		renderer.reset();
	}
};

BOOST_FIXTURE_TEST_SUITE(vulkan_descriptor_sets, SmallPoolVulkanFixture)

// Seconds; every case here, including the streamed one, takes a few seconds on a healthy device. Generous margin is kept
// because the timeout exists to turn a device fault (the pool handing a stale set out, faulting the GPU so
// vkQueueWaitIdle never returns) into a failure instead of a hang, not to bound the normal case tightly.
unsigned const CASE_TIMEOUT_S = 60;

// One renderer, thousands of scenes' worth of materials in sequence.  Each
// material is dropped after its frame, so the renderer has every opportunity
// to reclaim the set — and must, or the pool runs dry.
BOOST_AUTO_TEST_CASE(streaming_materials_through_one_renderer_never_exhausts_the_pool,
	*boost::unit_test::timeout(CASE_TIMEOUT_S))
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	Quad quad(renderer);

	for (size_t i = 0; i < MATERIALS_TO_STREAM; ++i)
	{
		uint32_t const colour = (i % 2 == 0) ? RED : GREEN;
		MaterialPtr material = make_material(renderer, colour);
		if (i % 64 != 63)
		{
			BOOST_REQUIRE_NO_THROW(quad.render(renderer, material));
			continue;
		}
		uint32_t pixel = 0;
		BOOST_REQUIRE_NO_THROW(pixel = quad.render_with(renderer, material));
		BOOST_REQUIRE_MESSAGE(pixel == rgb(colour),
			"material #" << i << " rendered 0x" << std::hex << pixel
				<< ", expected 0x" << rgb(colour));
	}

	// Reclaim happens at the frame boundary, so one more frame with a live
	// material leaves exactly that material's set behind — every streamed
	// one is gone.
	MaterialPtr last = make_material(renderer, RED);
	BOOST_CHECK_EQUAL(quad.render_with(renderer, last), rgb(RED));
	BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 1u);
	// Reclaimed, not outgrown: a pool would be added if the dead sets were not freed.
	BOOST_CHECK_EQUAL(renderer->material_descriptor_pool_count(), 1u);
}

// More materials alive at once than a pool holds: another pool is made, every material draws as itself, and their sets go back to
// the pools they came from when they die.
BOOST_AUTO_TEST_CASE(more_live_materials_than_a_pool_holds_get_another_pool,
	*boost::unit_test::timeout(CASE_TIMEOUT_S))
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	Quad quad(renderer);

	std::vector<MaterialPtr> live;
	for (size_t i = 0; i < MATERIALS_TO_STREAM; ++i)
	{
		uint32_t const colour = (i % 2 == 0) ? RED : GREEN;
		live.push_back(make_material(renderer, colour));
		if (i % 32 != 31)
		{
			BOOST_REQUIRE_NO_THROW(quad.render(renderer, live.back()));
			continue;
		}
		uint32_t pixel = 0;
		BOOST_REQUIRE_NO_THROW(pixel = quad.render_with(renderer, live.back()));
		BOOST_REQUIRE_MESSAGE(pixel == rgb(colour), "live material #" << i << " rendered 0x" << std::hex << pixel);
	}
	BOOST_CHECK_EQUAL(renderer->material_descriptor_pool_count(), 2u);
	// The first, from the first pool, still draws as itself.
	BOOST_CHECK_EQUAL(quad.render_with(renderer, live.front()), rgb(RED));

	live.clear();
	MaterialPtr last = make_material(renderer, GREEN);
	BOOST_CHECK_EQUAL(quad.render_with(renderer, last), rgb(GREEN));
	BOOST_CHECK_EQUAL(quad.render_with(renderer, last), rgb(GREEN));
	BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 1u);
}

// Live materials keep their sets; only dead ones are reclaimed.
BOOST_AUTO_TEST_CASE(sets_of_live_materials_survive_reclaim,
	*boost::unit_test::timeout(CASE_TIMEOUT_S))
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	Quad quad(renderer);

	MaterialPtr kept = make_material(renderer, RED);
	BOOST_CHECK_EQUAL(quad.render_with(renderer, kept), rgb(RED));
	BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 1u);

	{
		MaterialPtr transient = make_material(renderer, GREEN);
		BOOST_CHECK_EQUAL(quad.render_with(renderer, transient), rgb(GREEN));
		BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 2u);
	}

	// The transient material is gone; its set goes at the next frame boundary,
	// the kept one's stays and still renders correctly.
	BOOST_CHECK_EQUAL(quad.render_with(renderer, kept), rgb(RED));
	BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 1u);
}

// What draws without a material (shadow volumes may) bind has a set too, of the
// default textures: there is no material to die, so it is kept from one frame
// to the next, not made anew.
BOOST_AUTO_TEST_CASE(the_set_of_draws_without_a_material_is_kept,
	*boost::unit_test::timeout(CASE_TIMEOUT_S))
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	renderer->begin_frame();
	BOOST_CHECK(renderer->material_descriptor_set(MaterialPtr()) != VK_NULL_HANDLE);
	renderer->finish_frame();
	BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 1u);
	renderer->begin_frame();
	renderer->finish_frame();
	BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 1u);
}

// A material allocated at the address of a dead one must not inherit the
// dead one's descriptor set: it has its own textures.
BOOST_AUTO_TEST_CASE(recycled_material_address_does_not_inherit_a_stale_set,
	*boost::unit_test::timeout(CASE_TIMEOUT_S))
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	Quad quad(renderer);

	// Force address reuse by dropping one material and making the next
	// immediately; the allocator gives the same block back nearly always,
	// and the loop makes "nearly" irrelevant.
	for (int i = 0; i < 64; ++i)
	{
		MaterialPtr first = make_material(renderer, RED);
		BOOST_REQUIRE_EQUAL(quad.render_with(renderer, first), rgb(RED));
		first.reset();

		MaterialPtr second = make_material(renderer, GREEN);
		BOOST_REQUIRE_EQUAL(quad.render_with(renderer, second), rgb(GREEN));
	}
}

// The set on file for a live material is rewritten for a new texture, not reused: the old texture must not show.
BOOST_AUTO_TEST_CASE(a_live_material_whose_texture_changes_shows_the_new_texture,
	*boost::unit_test::timeout(CASE_TIMEOUT_S))
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	Quad quad(renderer);

	MaterialPtr material = make_material(renderer, RED);
	BOOST_CHECK_EQUAL(quad.render_with(renderer, material), rgb(RED));
	material->set_diffuse_texture(make_solid_texture(renderer, GREEN));
	BOOST_CHECK_EQUAL(quad.render_with(renderer, material), rgb(GREEN));
	material->set_diffuse_texture(make_solid_texture(renderer, RED));
	BOOST_CHECK_EQUAL(quad.render_with(renderer, material), rgb(RED));
	BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 1u);
}

// An unchanged material keeps its one set however many frames draw it.
BOOST_AUTO_TEST_CASE(an_unchanged_material_keeps_one_set_across_frames,
	*boost::unit_test::timeout(CASE_TIMEOUT_S))
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	Quad quad(renderer);

	MaterialPtr material = make_material(renderer, GREEN);
	for (int frame = 0; frame < 5; ++frame)
	{
		BOOST_CHECK_EQUAL(quad.render_with(renderer, material), rgb(GREEN));
		BOOST_CHECK_EQUAL(renderer->material_descriptor_set_count(), 1u);
	}
}

// The anisotropy of the material sampler: 8 where the device allows it, capped by its limit, and none on the Deck under RADV (its filter garbles the picture there).
BOOST_AUTO_TEST_CASE(sampler_anisotropy_is_off_on_the_deck_and_lavapipe_only)
{
	BOOST_CHECK_EQUAL(VulkanDescriptorCache::sampler_anisotropy("AMD Custom GPU 0405 (RADV VANGOGH)", 16.0f), 1.0f);
	BOOST_CHECK_EQUAL(VulkanDescriptorCache::sampler_anisotropy("llvmpipe (LLVM 19.1.7, 128 bits)", 16.0f), 1.0f);
	BOOST_CHECK_EQUAL(VulkanDescriptorCache::sampler_anisotropy("AMD Radeon RX 7900 XTX (RADV NAVI31)", 16.0f), 8.0f);   // desktop RADV: unaffected
	BOOST_CHECK_EQUAL(VulkanDescriptorCache::sampler_anisotropy("NVIDIA GeForce RTX 3080", 16.0f), 8.0f);
	BOOST_CHECK_EQUAL(VulkanDescriptorCache::sampler_anisotropy("NVIDIA GeForce RTX 3080", 4.0f), 4.0f);
	BOOST_CHECK_EQUAL(VulkanDescriptorCache::sampler_anisotropy("AMD Radeon RX 7900 XTX", 16.0f), 8.0f);   // the proprietary driver: unaffected
}

// A texture of another renderer is a mistake to tell, not a white square to draw.
BOOST_AUTO_TEST_CASE(a_texture_of_another_renderer_is_refused,
	*boost::unit_test::timeout(CASE_TIMEOUT_S))
{
	BOOST_REQUIRE_MESSAGE(renderer, "Vulkan renderer unavailable: " << error);
	Quad quad(renderer);
	RendererPtr other(new NullRenderer(4, 4));
	MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(other->create_texture(2, 2, PixelFormat::X8R8G8B8));
	BOOST_CHECK_THROW(quad.render(renderer, material), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
