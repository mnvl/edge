// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// The render pass contract of VulkanRenderer: one pass per colour format (and per whether the depth is kept), the same handle every time, and a depth format
// the device can attach.

#include <boost/test/unit_test.hpp>

#include <vector>

#include "test_vulkan_fixture.h"
#include "vulkan_renderer.h"

using namespace edge;

BOOST_AUTO_TEST_SUITE(vulkan_render_pass_cache)

BOOST_AUTO_TEST_CASE(the_depth_format_is_one_the_device_can_attach)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	VkFormat const depth = f.renderer->depth_format();
	BOOST_CHECK(depth == VK_FORMAT_D32_SFLOAT_S8_UINT || depth == VK_FORMAT_D24_UNORM_S8_UINT || depth == VK_FORMAT_D32_SFLOAT);
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(f.renderer->physical_device(), depth, &props);
	BOOST_CHECK(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

BOOST_AUTO_TEST_CASE(a_pass_is_made_once_per_format)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	VkRenderPass const a = f.renderer->offscreen_render_pass(VK_FORMAT_B8G8R8A8_UNORM);
	BOOST_CHECK(a != VK_NULL_HANDLE);
	BOOST_CHECK(f.renderer->offscreen_render_pass(VK_FORMAT_B8G8R8A8_UNORM) == a);
}

BOOST_AUTO_TEST_CASE(other_formats_get_their_own_passes)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	VkRenderPass const base = f.renderer->offscreen_render_pass(VK_FORMAT_B8G8R8A8_UNORM);
	VkRenderPass const other_format = f.renderer->offscreen_render_pass(VK_FORMAT_R16G16B16A16_SFLOAT);
	BOOST_CHECK(base != VK_NULL_HANDLE);
	BOOST_CHECK(other_format != VK_NULL_HANDLE);
	BOOST_CHECK(base != other_format);
	// asking again does not disturb what was made
	BOOST_CHECK(f.renderer->offscreen_render_pass(VK_FORMAT_R16G16B16A16_SFLOAT) == other_format);
	BOOST_CHECK(f.renderer->offscreen_render_pass(VK_FORMAT_B8G8R8A8_UNORM) == base);
}

/// A pass that keeps its depth (a readable depth's) is not the one that throws it away, and is made once too.
BOOST_AUTO_TEST_CASE(keeping_the_depth_is_a_pass_of_its_own)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	VkRenderPass const thrown = f.renderer->offscreen_render_pass(VK_FORMAT_R16G16B16A16_SFLOAT);
	VkRenderPass const kept = f.renderer->offscreen_render_pass(VK_FORMAT_R16G16B16A16_SFLOAT, true);
	BOOST_CHECK(kept != VK_NULL_HANDLE);
	BOOST_CHECK(kept != thrown);
	BOOST_CHECK(f.renderer->offscreen_render_pass(VK_FORMAT_R16G16B16A16_SFLOAT, true) == kept);
	BOOST_CHECK(f.renderer->offscreen_render_pass(VK_FORMAT_R16G16B16A16_SFLOAT, false) == thrown);
}

/// A pass of several colours, in their order, or of none (a target of depth alone) is a pass of its own, made once; one colour asked for as
/// a list of one is the pass of that colour.
BOOST_AUTO_TEST_CASE(several_colours_or_none_are_passes_of_their_own)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	VkFormat const a = VK_FORMAT_R16G16B16A16_SFLOAT, b = VK_FORMAT_R32_SFLOAT;
	VkRenderPass const one = f.renderer->offscreen_render_pass(a, true);
	VkRenderPass const two = f.renderer->offscreen_render_pass(std::vector<VkFormat>{a, b}, true);
	VkRenderPass const swapped = f.renderer->offscreen_render_pass(std::vector<VkFormat>{b, a}, true);
	VkRenderPass const none = f.renderer->offscreen_render_pass(std::vector<VkFormat>{}, true);
	for (VkRenderPass pass : {two, swapped, none})
	{
		BOOST_CHECK(pass != VK_NULL_HANDLE);
		BOOST_CHECK(pass != one);
	}
	BOOST_CHECK(two != swapped);
	BOOST_CHECK(two != none);
	BOOST_CHECK(f.renderer->offscreen_render_pass(std::vector<VkFormat>{a}, true) == one);
	BOOST_CHECK(f.renderer->offscreen_render_pass(std::vector<VkFormat>{a, b}, true) == two);
	BOOST_CHECK(f.renderer->offscreen_render_pass(std::vector<VkFormat>{a, b}, false) != two);
	BOOST_CHECK(f.renderer->offscreen_render_pass(std::vector<VkFormat>{}, true) == none);
}

/// What the destructor does with the passes is all there is to see: run it.
BOOST_AUTO_TEST_CASE(a_renderer_that_made_passes_is_destroyed_cleanly)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	f.renderer->offscreen_render_pass(VK_FORMAT_R16G16B16A16_SFLOAT);
	f.renderer.reset();
}

BOOST_AUTO_TEST_SUITE_END()
