// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// The presenting contract of VulkanRenderer that needs no window: what it refuses, and what it exposes so that an
// application can make the surface.  Presenting to a real surface is tested where a window system is at hand (the game's
// tests make an SDL window).

#include <boost/test/unit_test.hpp>
#include <utility>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <stdexcept>
#include <string>

#include "vulkan_instance.h"
#include "log.h"
#include "pixel.h"
#include "test_vulkan_fixture.h"
#include "vulkan_device_info.h"
#include "vulkan_presenter.h"
#include "vulkan_renderer.h"

using namespace edge;

BOOST_AUTO_TEST_SUITE(vulkan_present)

namespace
{
VkSurfaceCapabilitiesKHR caps_of(VkSurfaceTransformFlagBitsKHR current, uint32_t w, uint32_t h, uint32_t supported)
{
	VkSurfaceCapabilitiesKHR c = {};
	c.currentTransform = current;
	c.currentExtent = {w, h};
	c.supportedTransforms = supported;
	return c;
}
}

// A phone's panel is portrait: held sideways its surface reports the panel's extent and a 90 degree transform.  The picture the game draws is
// landscape, so the swapchain is made upright and the compositor turns it.
BOOST_AUTO_TEST_CASE(a_turned_surface_gets_an_upright_swapchain_when_it_allows_no_transform)
{
	uint32_t const all = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
	for (auto turned : {VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR})
	{
		SwapchainGeometry const g = choose_swapchain_geometry(caps_of(turned, 1080, 2400, all));
		BOOST_CHECK_EQUAL(g.transform, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
		BOOST_CHECK_EQUAL(g.extent.width, 2400u);
		BOOST_CHECK_EQUAL(g.extent.height, 1080u);
	}
	// an upright surface is left as it is
	SwapchainGeometry const upright = choose_swapchain_geometry(caps_of(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 1280, 800, all));
	BOOST_CHECK_EQUAL(upright.transform, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
	BOOST_CHECK_EQUAL(upright.extent.width, 1280u);
	BOOST_CHECK_EQUAL(upright.extent.height, 800u);
	// a turned surface that cannot do without the transform keeps it and its extent
	SwapchainGeometry const stuck = choose_swapchain_geometry(caps_of(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, 1080, 2400, VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR));
	BOOST_CHECK_EQUAL(stuck.transform, VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR);
	BOOST_CHECK_EQUAL(stuck.extent.width, 1080u);
	// a surface with no size of its own is not touched: the caller's size is used
	SwapchainGeometry const free = choose_swapchain_geometry(caps_of(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, 0xFFFFFFFFu, 0xFFFFFFFFu, all));
	BOOST_CHECK_EQUAL(free.extent.width, 0xFFFFFFFFu);
}

// A driver that is asked for no transform on a turned panel answers every present with VK_SUBOPTIMAL_KHR, however well the swapchain fits what
// the application asked for: rebuilding it each time cost a frame's half on a phone (vkDeviceWaitIdle and a new swapchain, 40-66 ms).  The
// swapchain is rebuilt when what the surface asks for is not what it was made for.
BOOST_AUTO_TEST_CASE(a_swapchain_made_for_the_geometry_the_surface_still_asks_for_fits)
{
	uint32_t const all = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
	// a turned panel: the swapchain the geometry chose is the one it still asks for, and a flip to the other landscape changes nothing for it
	SwapchainGeometry const landscape = choose_swapchain_geometry(caps_of(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, 1080, 2400, all));
	BOOST_CHECK(swapchain_fits(landscape, caps_of(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, 1080, 2400, all)));
	BOOST_CHECK(swapchain_fits(landscape, caps_of(VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR, 1080, 2400, all)));
	// the phone held upright: the surface wants a portrait picture now
	BOOST_CHECK(!swapchain_fits(landscape, caps_of(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 1080, 2400, all)));
	// a window of another size (split screen, a resized window)
	BOOST_CHECK(!swapchain_fits(landscape, caps_of(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, 1080, 1200, all)));
	// an upright surface: the same size fits, another does not
	SwapchainGeometry const desktop = choose_swapchain_geometry(caps_of(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 1280, 800, all));
	BOOST_CHECK(swapchain_fits(desktop, caps_of(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 1280, 800, all)));
	BOOST_CHECK(!swapchain_fits(desktop, caps_of(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 1920, 1080, all)));
	// a turned surface that keeps its transform: it fits while it stays so, and not once it is turned the other way
	SwapchainGeometry const stuck = choose_swapchain_geometry(caps_of(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, 1080, 2400, VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR));
	BOOST_CHECK(swapchain_fits(stuck, caps_of(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, 1080, 2400, VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR)));
	BOOST_CHECK(!swapchain_fits(stuck, caps_of(VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR, 1080, 2400, VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)));
	// a surface with no size of its own cannot say: the swapchain is made again, as it was
	BOOST_CHECK(!swapchain_fits(desktop, caps_of(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 0xFFFFFFFFu, 0xFFFFFFFFu, all)));
}

namespace
{
size_t count_of(std::string const &text, std::string const &what)
{
	size_t n = 0;
	for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size()))
		++n;
	return n;
}
}

// Starting up on a new device is where the surprises are (a missing compressed format, a small limit, the wrong GPU): what the renderer found
// and chose is in the log at the default level, so that a run on a phone can be read without a debugger.
BOOST_AUTO_TEST_CASE(the_renderer_logs_the_device_it_found_and_took)
{
	auto const file = std::filesystem::temp_directory_path() / ("edge_test_vulkan_init_" + std::to_string(::getpid()) + ".log");
	std::filesystem::remove(file);
	init_logging(file, "info");
	std::string text;
	VulkanInstance::release_unused();      // (the instance is kept between renderers: this one is to make it, and say so)
	{
		VulkanFixture f;
		BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
		std::ifstream in(file);
		std::stringstream all;
		all << in.rdbuf();
		text = all.str();
	}
	init_logging(std::nullopt);
	std::filesystem::remove(file);
	for (char const *line : {"Vulkan instance: loader ", "Vulkan device 0: ", "Vulkan device features: textureCompressionBC ", "Vulkan device limits: push constants ",
			"Vulkan memory heap 0: ", "Vulkan depth format: "})
		BOOST_CHECK_MESSAGE(text.find(line) != std::string::npos, std::string("the log has no \"") + line + "\":\n" + text);
	// one device is taken, and said so, once
	BOOST_CHECK_MESSAGE(count_of(text, " taken: ") == 1, text);
}

BOOST_AUTO_TEST_CASE(the_renderer_exposes_its_instance_for_making_a_surface)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	BOOST_CHECK(f.renderer->instance() != VK_NULL_HANDLE);
}

BOOST_AUTO_TEST_CASE(an_instance_extension_the_driver_lacks_is_an_error_naming_it)
{
	try
	{
		VulkanRenderer renderer(64, 64, std::string(), {"VK_EXT_there_is_no_such_extension"});
		BOOST_FAIL("a renderer was made with an instance extension that does not exist");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("VK_EXT_there_is_no_such_extension") != std::string::npos, e.what());
	}
}

// What acquiring an image or presenting it answers.  A phone's window loses its surface when the app goes to the background and the
// system takes the window back, which can happen between a frame being drawn and its present (a pause and a resume close together):
// that is the window's doing, not an error of the device's, and an application must be able to get over it (detach the surface, attach
// the window's next) rather than die of it.  Any other error is the device's.
BOOST_AUTO_TEST_CASE(a_lost_surface_is_an_answer_of_its_own_and_other_errors_are_the_devices)
{
	BOOST_CHECK(present_answer(VK_SUCCESS, "vkQueuePresentKHR") == PresentAnswer::Done);
	BOOST_CHECK(present_answer(VK_SUBOPTIMAL_KHR, "vkQueuePresentKHR") == PresentAnswer::Suboptimal);
	BOOST_CHECK(present_answer(VK_ERROR_OUT_OF_DATE_KHR, "vkAcquireNextImageKHR") == PresentAnswer::OutOfDate);
	BOOST_CHECK(present_answer(VK_ERROR_SURFACE_LOST_KHR, "vkAcquireNextImageKHR") == PresentAnswer::Lost);
	BOOST_CHECK(present_answer(VK_ERROR_SURFACE_LOST_KHR, "vkQueuePresentKHR") == PresentAnswer::Lost);
	for (VkResult error : {VK_ERROR_DEVICE_LOST, VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_TIMEOUT, VK_NOT_READY})
	{
		try
		{
			present_answer(error, "vkAcquireNextImageKHR");
			BOOST_ERROR("an answer of " << int(error) << " taken");
		}
		catch (std::runtime_error const &e)
		{
			BOOST_CHECK_MESSAGE(std::string(e.what()).find("vkAcquireNextImageKHR") != std::string::npos, e.what());
		}
	}
}

BOOST_AUTO_TEST_CASE(a_renderer_without_a_surface_has_not_lost_one)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	BOOST_CHECK(!f.renderer->surface_lost());
}

BOOST_AUTO_TEST_CASE(presenting_without_a_surface_is_a_logic_error)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	RenderTargetPtr const target = f.renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	BOOST_CHECK_THROW(f.renderer->present(target->color_texture(), 64, 64), std::logic_error);
}

BOOST_AUTO_TEST_CASE(a_null_surface_cannot_be_attached)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	BOOST_CHECK_THROW(f.renderer->attach_surface(VK_NULL_HANDLE, true), std::invalid_argument);
}


namespace
{

// A surface with no window: what present() can be driven through without a window system.
struct HeadlessWindow
{
	VulkanRendererPtr renderer;
	VkSurfaceKHR surface = VK_NULL_HANDLE;

	// Which device's graphics queue can present to a headless surface (NVIDIA's cannot, the CPU rasteriser's can) does
	// not change between one test case and the next within a run, so the search for it -- a full renderer made and
	// torn down per device tried -- is done once per process and the uuid it found is kept: making every renderer in
	// this file on NVIDIA's machines cost three times what one renderer does (every unwilling device tried before the
	// one that works), which alone pushed this suite's time past its ctest budget for no coverage gained, since the
	// search itself is this file's scaffolding, not what present() is tested on.
	explicit HeadlessWindow(int frames_in_flight = 1)
	{
		static std::string cached_uuid;
		static bool cached = false;

		if (cached)
		{
			renderer.reset(new VulkanRenderer(64, 64, cached_uuid, {"VK_KHR_surface", "VK_EXT_headless_surface"}, frames_in_flight));
			make_surface();
			VkBool32 supported = VK_FALSE;
			BOOST_REQUIRE_EQUAL(vkGetPhysicalDeviceSurfaceSupportKHR(renderer->physical_device(),
				renderer->graphics_queue_family(), surface, &supported), VK_SUCCESS);
			BOOST_REQUIRE_MESSAGE(supported, "the device found earlier to support a headless surface no longer does");
			return;
		}

		std::string devices;
		for (PhysicalDeviceInfo const &device : enumerate_vulkan_physical_devices())
		{
			renderer.reset(new VulkanRenderer(64, 64, device.uuid, {"VK_KHR_surface", "VK_EXT_headless_surface"}, frames_in_flight));
			make_surface();
			VkBool32 supported = VK_FALSE;
			BOOST_REQUIRE_EQUAL(vkGetPhysicalDeviceSurfaceSupportKHR(renderer->physical_device(),
				renderer->graphics_queue_family(), surface, &supported), VK_SUCCESS);
			if (supported)
			{
				cached_uuid = device.uuid;
				cached = true;
				return;
			}
			vkDestroySurfaceKHR(renderer->instance(), surface, nullptr);
			surface = VK_NULL_HANDLE;
			renderer.reset();
			devices += " " + device.name;
		}
		BOOST_FAIL("no device can present to a headless surface, tried:" + devices);
	}

	void make_surface()
	{
		auto create = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
			vkGetInstanceProcAddr(renderer->instance(), "vkCreateHeadlessSurfaceEXT"));
		BOOST_REQUIRE(create);
		VkHeadlessSurfaceCreateInfoEXT info = {};
		info.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
		BOOST_REQUIRE_EQUAL(create(renderer->instance(), &info, nullptr, &surface), VK_SUCCESS);
	}

	TexturePtr rendered_frame(int size, float red)
	{
		RenderTargetPtr const target = renderer->create_render_target(size, size, PixelFormat::A8R8G8B8);
		RenderPassClearState clear;
		clear.clear_color = true;
		clear.clear_color_r = red;
		clear.clear_color_a = 1.0f;
		clear.clear_depth = true;
		clear.clear_depth_value = 1.0f;
		renderer->begin_frame();
		renderer->begin_pass(target, clear);
		renderer->end_pass();
		renderer->finish_frame();
		return target->color_texture();
	}
};

}

BOOST_AUTO_TEST_CASE(a_rendered_frame_is_presented_and_the_frame_stays_usable)
{
	HeadlessWindow w;
	w.renderer->attach_surface(w.surface, true);
	TexturePtr const frame = w.rendered_frame(64, 1.0f);
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
	// the frame is as it was left: it can be rendered into again at once
	TexturePtr const again = w.rendered_frame(64, 0.0f);
	BOOST_CHECK(w.renderer->present(again, 64, 64));
}

BOOST_AUTO_TEST_CASE(frames_submitted_without_waiting_are_presented_and_drawn_into_again_at_once)
{
	// neither the frame nor the copy to the window is waited for: the next frame draws into the target the copy reads, the GPU
	// copies first; a presented texture can be dropped at once
	HeadlessWindow w(3);
	w.renderer->attach_surface(w.surface, false);
	RenderTargetPtr const target = w.renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	for (int k = 0; k < 12; ++k)
	{
		RenderPassClearState clear;
		clear.clear_color_r = float(k % 2);
		clear.clear_color_g = float(1 - k % 2);
		clear.clear_color_a = 1.0f;
		w.renderer->begin_frame();
		w.renderer->begin_pass(target, clear);
		w.renderer->end_pass();
		w.renderer->submit_frame();
		BOOST_CHECK(w.renderer->present(target->color_texture(), 64, 64));
	}
	TexturePtr const color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	uint32_t const pixel = color_mapping.as<uint32_t const>().data()[32 * 64 + 32];
	color_mapping.unmap();
	BOOST_CHECK_MESSAGE(((pixel >> 16) & 0xFFu) > 200 && ((pixel >> 8) & 0xFFu) < 50, "the last frame (11: red) shows 0x" << std::hex << pixel);
	{
		RenderTargetPtr const dropped = w.renderer->create_render_target(64, 64, PixelFormat::A8R8G8B8);
		w.renderer->begin_frame();
		w.renderer->begin_pass(dropped);
		w.renderer->end_pass();
		w.renderer->submit_frame();
		BOOST_CHECK(w.renderer->present(dropped->color_texture(), 64, 64));
	}
	BOOST_CHECK(w.renderer->present(target->color_texture(), 64, 64));
	w.renderer->wait_frames();
}

BOOST_AUTO_TEST_CASE(a_window_of_another_size_gets_the_frame_scaled)
{
	HeadlessWindow w;
	w.renderer->attach_surface(w.surface, false);
	TexturePtr const frame = w.rendered_frame(64, 1.0f);
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
	BOOST_CHECK(w.renderer->present(frame, 128, 96));
	BOOST_CHECK(w.renderer->present(frame, 32, 32));
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
}

BOOST_AUTO_TEST_CASE(vsync_can_be_switched_while_presenting)
{
	HeadlessWindow w;
	w.renderer->attach_surface(w.surface, true);
	BOOST_CHECK(w.renderer->vsync());
	TexturePtr const frame = w.rendered_frame(64, 1.0f);
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
	for (bool on : {false, false, true, false, true})
	{
		w.renderer->set_vsync(on);
		BOOST_CHECK_EQUAL(w.renderer->vsync(), on);
		BOOST_CHECK(w.renderer->present(frame, 64, 64));
		BOOST_CHECK(w.renderer->present(frame, 64, 64));
	}
}

BOOST_AUTO_TEST_CASE(vsync_cannot_be_switched_without_a_surface)
{
	HeadlessWindow w;
	BOOST_CHECK_THROW(w.renderer->set_vsync(false), std::logic_error);
}

BOOST_AUTO_TEST_CASE(a_window_without_area_shows_nothing)
{
	HeadlessWindow w;
	w.renderer->attach_surface(w.surface, true);
	TexturePtr const frame = w.rendered_frame(64, 1.0f);
	BOOST_CHECK(!w.renderer->present(frame, 0, 64));
	BOOST_CHECK(!w.renderer->present(frame, 64, 0));
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
}

BOOST_AUTO_TEST_CASE(present_refuses_what_it_cannot_show)
{
	HeadlessWindow w;
	w.renderer->attach_surface(w.surface, true);
	TexturePtr const frame = w.rendered_frame(64, 1.0f);
	BOOST_CHECK_THROW(w.renderer->present(frame, -1, 64), std::invalid_argument);
	BOOST_CHECK_THROW(w.renderer->present(TexturePtr(), 64, 64), std::invalid_argument);
	// never rendered into: its image has no layout to be copied from
	TexturePtr const blank = w.renderer->create_texture(16, 16, PixelFormat::A8R8G8B8, TextureUsage());
	BOOST_CHECK_THROW(w.renderer->present(blank, 64, 64), std::logic_error);
	// a frame another renderer drew is an image on another device
	VulkanRendererPtr other(new VulkanRenderer(64, 64));
	RenderTargetPtr const foreign = other->create_render_target(64, 64, PixelFormat::A8R8G8B8);
	other->begin_frame();
	other->begin_pass(foreign);
	other->end_pass();
	other->finish_frame();
	BOOST_CHECK_THROW(w.renderer->present(foreign->color_texture(), 64, 64), std::invalid_argument);
	// refusing left the window usable
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
}

BOOST_AUTO_TEST_CASE(a_second_surface_cannot_be_attached_over_the_first)
{
	HeadlessWindow w;
	w.renderer->attach_surface(w.surface, true);
	VkSurfaceKHR second = VK_NULL_HANDLE;
	{
		auto create = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
			vkGetInstanceProcAddr(w.renderer->instance(), "vkCreateHeadlessSurfaceEXT"));
		VkHeadlessSurfaceCreateInfoEXT info = {};
		info.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
		BOOST_REQUIRE_EQUAL(create(w.renderer->instance(), &info, nullptr, &second), VK_SUCCESS);
	}
	BOOST_CHECK_THROW(w.renderer->attach_surface(second, true), std::logic_error);
	vkDestroySurfaceKHR(w.renderer->instance(), second, nullptr);   // refused, so still the caller's
}

BOOST_AUTO_TEST_CASE(detaching_frees_the_window_and_a_surface_can_be_attached_again)
{
	HeadlessWindow w;
	BOOST_CHECK_THROW(w.renderer->detach_surface(), std::logic_error);
	w.renderer->attach_surface(w.surface, true);
	TexturePtr const frame = w.rendered_frame(64, 1.0f);
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
	w.renderer->detach_surface();
	BOOST_CHECK_THROW(w.renderer->present(frame, 64, 64), std::logic_error);
	w.make_surface();
	w.renderer->attach_surface(w.surface, true);
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
}

/// The swapchain and the surface go with the renderer: what the destructor does is all there is to see, so run it.
BOOST_AUTO_TEST_CASE(a_renderer_with_a_surface_attached_is_destroyed_cleanly)
{
	HeadlessWindow w;
	w.renderer->attach_surface(w.surface, true);
	TexturePtr const frame = w.rendered_frame(64, 1.0f);
	BOOST_CHECK(w.renderer->present(frame, 64, 64));
	w.renderer.reset();
}

BOOST_AUTO_TEST_SUITE_END()
