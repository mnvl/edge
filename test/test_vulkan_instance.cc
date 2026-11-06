// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// One Vulkan instance a process (vulkan_instance.h): its renderers share it, the first makes it, the last holder lets it go, on one GPU or
// on several, made and destroyed in any order (the first while the second draws), from several threads; a renderer that asks for an
// extension the live instance was made without is refused, not given something else.  Every renderer here draws, and its pixels are
// checked: a renderer that shares an instance draws what one alone draws.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "vulkan_device_info.h"
#include "vulkan_instance.h"
#include "vulkan_renderer.h"

using namespace edge;

namespace
{

struct Rgb
{
	int r, g, b;
};

/// A renderer's picture, started: a target cleared to `clear`, a square of `square` drawn in its middle, the frame and pass open.
struct Picture
{
	VulkanRendererPtr renderer;
	RenderTargetPtr target;
	DrawCallPtr draw;

	Picture(VulkanRendererPtr const &renderer_, Rgb clear, Rgb square): renderer(renderer_)
	{
		target = renderer->create_render_target(32, 32, PixelFormat::A8R8G8B8);
		VertexBufferPtr const vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 6);
		auto vb_mapping = vb->write(Contents::DISCARD);
		auto *v = vb_mapping.as<VERTEX_PT>().data();
		float const corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
		for (int i = 0; i < 6; ++i)
		{
			v[i].position = rove::vec<3>(corners[i][0], corners[i][1], 5.0f);       // (the default camera looks down +z)
			v[i].texcoords = rove::vec<2>(0, 0);
		}
		vb_mapping.unmap();
		MaterialPtr const material = renderer->create_material();
		material->set_cull_mode(Material::CullMode::NONE);
		material->set_base_color_factor(rove::vec<4>(square.r / 255.0f, square.g / 255.0f, square.b / 255.0f, 1.0f));
		draw = renderer->create_draw_call();
		draw->set_vertex_buffer(vb);
		draw->set_material(material);
		draw->set_camera(std::make_shared<Camera>());
		draw->set_triangles_count(2);
		draw->set_always_visible(true);
		draw->commit();

		RenderPassClearState clear_state;
		clear_state.clear_color = true;
		clear_state.clear_color_r = clear.r / 255.0f;
		clear_state.clear_color_g = clear.g / 255.0f;
		clear_state.clear_color_b = clear.b / 255.0f;
		clear_state.clear_color_a = 1.0f;
		clear_state.clear_depth = true;
		clear_state.clear_depth_value = 1.0f;
		renderer->begin_frame();
		renderer->begin_pass(target, clear_state);
		draw->execute();
	}

	/// Ends the frame; whether the middle is the square's colour and a corner the clear colour.
	bool finish(Rgb clear, Rgb square)
	{
		renderer->end_pass();
		renderer->finish_frame();
		TexturePtr const colour = target->color_texture();
		colour->download();
		auto colour_mapping = colour->read();
		auto const *pixels = colour_mapping.as<uint32_t const>().data();
		uint32_t const middle = pixels[16 * 32 + 16], corner = pixels[1 * 32 + 1];
		colour_mapping.unmap();
		auto const is = [](uint32_t pixel, Rgb c)
		{
			int const b = pixel & 0xFF, g = (pixel >> 8) & 0xFF, r = (pixel >> 16) & 0xFF;
			return std::abs(r - c.r) <= 2 && std::abs(g - c.g) <= 2 && std::abs(b - c.b) <= 2;
		};
		return is(middle, square) && is(corner, clear);
	}
};

/// The renderer's picture, drawn whole; whether its pixels are right.  Each renderer gets colours of its own (by `n`).
bool draws_right(VulkanRendererPtr const &renderer, int n)
{
	Rgb const clear{(n * 37) % 200, 20, 200 - (n * 37) % 200}, square{240, (n * 53) % 230, 10};
	Picture picture(renderer, clear, square);
	return picture.finish(clear, square);
}

std::vector<std::string> gpus()
{
	std::vector<std::string> uuids;
	for (PhysicalDeviceInfo const &device : enumerate_vulkan_physical_devices())
		uuids.push_back(device.uuid);
	return uuids;
}

VulkanRendererPtr renderer_on(std::string const &uuid, std::vector<std::string> const &extensions = std::vector<std::string>())
{
	return std::make_shared<VulkanRenderer>(32, 32, uuid, extensions);
}

/// An instance extension the loader has that the live instance was not made with; empty for none.
std::string extension_not_on(VulkanInstance const &instance)
{
	uint32_t count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> available(count);
	vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
	for (VkExtensionProperties const &extension : available)
	{
		std::vector<std::string> const &on = instance.extensions();
		if (std::find(on.begin(), on.end(), std::string(extension.extensionName)) == on.end())
			return extension.extensionName;
	}
	return std::string();
}

struct two_gpus
{
	boost::test_tools::assertion_result operator()(boost::unit_test::test_unit_id) const
	{
		boost::test_tools::assertion_result result(gpus().size() >= 2);
		result.message() << "this machine has fewer than two GPUs";
		return result;
	}
};

}

BOOST_AUTO_TEST_SUITE(vulkan_instance)

BOOST_AUTO_TEST_CASE(renderers_share_the_instance_the_first_makes_and_the_last_lets_go)
{
	BOOST_REQUIRE_MESSAGE(!VulkanInstance::alive(), "an instance is alive before the test");
	uint64_t const made = VulkanInstance::made_count();
	VulkanRendererPtr a = renderer_on("");
	BOOST_CHECK(VulkanInstance::alive());
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 1);
	VulkanRendererPtr b = renderer_on(""), c = renderer_on("");
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 1);
	BOOST_CHECK(a->instance() != VK_NULL_HANDLE);
	BOOST_CHECK(b->instance() == a->instance());
	BOOST_CHECK(c->instance() == a->instance());
	BOOST_CHECK(b->device() != a->device());           // a device each
	BOOST_CHECK(draws_right(a, 0));
	BOOST_CHECK(draws_right(b, 1));
	BOOST_CHECK(draws_right(c, 2));

	b.reset();
	BOOST_CHECK(draws_right(a, 3));
	// the first let go while the last draws
	Rgb const clear{10, 90, 170}, square{200, 30, 60};
	{
		Picture picture(c, clear, square);
		a.reset();
		BOOST_CHECK(VulkanInstance::alive());
		BOOST_CHECK(picture.finish(clear, square));
	}
	BOOST_CHECK(draws_right(c, 4));
	c.reset();
	BOOST_CHECK(!VulkanInstance::alive());

	// the next renderer makes another
	VulkanRendererPtr d = renderer_on("");
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 2);
	BOOST_CHECK(draws_right(d, 5));
	d.reset();
	BOOST_CHECK(!VulkanInstance::alive());
}

BOOST_AUTO_TEST_CASE(renderers_on_two_gpus_share_one_instance_and_either_goes_first, *boost::unit_test::precondition(two_gpus()))
{
	std::vector<std::string> const uuids = gpus();
	BOOST_REQUIRE(!VulkanInstance::alive());
	uint64_t const made = VulkanInstance::made_count();
	VulkanRendererPtr first = renderer_on(uuids[0]), second = renderer_on(uuids[1]);
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 1);
	BOOST_CHECK_EQUAL(first->device_uuid(), uuids[0]);
	BOOST_CHECK_EQUAL(second->device_uuid(), uuids[1]);
	BOOST_CHECK(first->instance() == second->instance());
	BOOST_CHECK(first->physical_device() != second->physical_device());
	BOOST_CHECK(draws_right(first, 0));
	BOOST_CHECK(draws_right(second, 1));

	// the first GPU's renderer let go while the second GPU's draws
	Rgb const clear{5, 250, 5}, square{250, 5, 250};
	{
		Picture picture(second, clear, square);
		first.reset();
		BOOST_CHECK(picture.finish(clear, square));
	}
	// and one on the first GPU again while the second lives: the same instance
	VulkanRendererPtr again = renderer_on(uuids[0]);
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 1);
	BOOST_CHECK(again->instance() == second->instance());
	second.reset();
	BOOST_CHECK(draws_right(again, 2));
	again.reset();
	BOOST_CHECK(!VulkanInstance::alive());
}

BOOST_AUTO_TEST_CASE(renderers_made_and_let_go_in_mixed_order_draw_what_they_should)
{
	// on the GPUs there are in turn; after each step every renderer alive draws its picture
	std::vector<std::string> const uuids = gpus();
	BOOST_REQUIRE(!uuids.empty());
	BOOST_REQUIRE(!VulkanInstance::alive());
	uint64_t const made = VulkanInstance::made_count();
	std::vector<VulkanRendererPtr> live(6);
	auto const make = [&](int n) { live[n] = renderer_on(uuids[n % uuids.size()]); };
	auto const all_draw = [&](char const *step)
	{
		for (size_t n = 0; n < live.size(); ++n)
			if (live[n])
				BOOST_CHECK_MESSAGE(draws_right(live[n], int(n)), "after " << step << ", renderer " << n << " draws wrong");
		BOOST_CHECK_MESSAGE(VulkanInstance::made_count() == made + 1, "after " << step << ": " << VulkanInstance::made_count() - made
			<< " instances made");
	};
	make(0); make(1); make(2);
	all_draw("0, 1, 2 made");
	live[1].reset();
	all_draw("1 let go");
	make(3); make(4);
	all_draw("3, 4 made");
	live[0].reset();
	all_draw("0 let go");
	live[4].reset();
	make(5);
	all_draw("4 let go, 5 made");
	live[3].reset();
	live[2].reset();
	all_draw("3, 2 let go");
	for (size_t n = 0; n + 1 < live.size(); ++n)
		BOOST_CHECK(!live[n]);
	BOOST_CHECK(VulkanInstance::alive());
	live[5].reset();
	BOOST_CHECK(!VulkanInstance::alive());
}

BOOST_AUTO_TEST_CASE(a_texture_that_outlives_its_renderer_keeps_the_instance)
{
	BOOST_REQUIRE(!VulkanInstance::alive());
	uint64_t const made = VulkanInstance::made_count();
	VulkanRendererPtr renderer = renderer_on("");
	VkInstance const instance = renderer->instance();
	TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	renderer.reset();
	BOOST_CHECK(VulkanInstance::alive());
	VulkanRendererPtr next = renderer_on("");
	BOOST_CHECK(next->instance() == instance);
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 1);
	BOOST_CHECK(draws_right(next, 0));
	next.reset();
	BOOST_CHECK(VulkanInstance::alive());
	texture.reset();
	BOOST_CHECK(!VulkanInstance::alive());
}

BOOST_AUTO_TEST_CASE(a_renderer_asking_for_an_extension_the_instance_was_made_without_is_refused)
{
	BOOST_REQUIRE(!VulkanInstance::alive());
	uint64_t const made = VulkanInstance::made_count();
	VulkanRendererPtr plain = renderer_on("");
	std::string missing;
	{
		std::shared_ptr<VulkanInstance> const instance = VulkanInstance::acquire({});
		BOOST_CHECK(instance->get() == plain->instance());
		missing = extension_not_on(*instance);
	}
	BOOST_REQUIRE_MESSAGE(!missing.empty(), "the loader has no extension the instance was made without");
	try
	{
		renderer_on("", {missing});
		BOOST_ERROR("a renderer with " << missing << " was made on an instance without it");
	}
	catch (std::logic_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find(missing) != std::string::npos, e.what());
	}
	// one the loader does not have at all is what it was: std::runtime_error naming it
	BOOST_CHECK_THROW(renderer_on("", {"VK_EDGE_no_such_extension"}), std::runtime_error);
	BOOST_CHECK(draws_right(plain, 0));
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 1);
	plain.reset();
	BOOST_REQUIRE(!VulkanInstance::alive());

	// made with it first, the instance has it; a renderer asking for less is given it as it is
	VulkanRendererPtr with = renderer_on("", {missing});
	VulkanRendererPtr without = renderer_on("");
	BOOST_CHECK(with->instance() == without->instance());
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 2);
	{
		std::vector<std::string> const on = VulkanInstance::acquire({})->extensions();
		BOOST_CHECK(std::find(on.begin(), on.end(), missing) != on.end());
	}
	BOOST_CHECK(draws_right(with, 1));
	BOOST_CHECK(draws_right(without, 2));
}

BOOST_AUTO_TEST_CASE(device_enumeration_uses_the_instance_there_is)
{
	BOOST_REQUIRE(!VulkanInstance::alive());
	uint64_t const made = VulkanInstance::made_count();
	std::vector<std::string> const alone = gpus();         // none alive: one made for it, and let go
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 1);
	BOOST_CHECK(!VulkanInstance::alive());
	VulkanRendererPtr renderer = renderer_on("");
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 2);
	std::vector<std::string> const beside = gpus();
	BOOST_CHECK_EQUAL(VulkanInstance::made_count(), made + 2);
	BOOST_CHECK(alone == beside);
	BOOST_CHECK(std::find(beside.begin(), beside.end(), renderer->device_uuid()) != beside.end());
}

BOOST_AUTO_TEST_CASE(renderers_made_and_let_go_on_several_threads_at_once_draw_what_they_should)
{
	std::vector<std::string> const uuids = gpus();
	BOOST_REQUIRE(!uuids.empty());
	int const threads = 4, rounds = 4;
	std::vector<std::exception_ptr> failed(threads);
	std::vector<int> wrong(threads, 0);
	std::vector<std::thread> running;
	for (int t = 0; t < threads; ++t)
		running.emplace_back([&, t]
		{
			try
			{
				for (int round = 0; round < rounds; ++round)
				{
					VulkanRendererPtr const renderer = renderer_on(uuids[(t + round) % uuids.size()]);
					if (!draws_right(renderer, t * rounds + round))
						++wrong[t];
				}
			}
			catch (...)
			{
				failed[t] = std::current_exception();
			}
		});
	for (std::thread &thread : running)
		thread.join();
	for (int t = 0; t < threads; ++t)
	{
		if (failed[t])
			std::rethrow_exception(failed[t]);
		BOOST_CHECK_MESSAGE(wrong[t] == 0, "thread " << t << ": " << wrong[t] << " of " << rounds << " pictures wrong");
	}
	BOOST_CHECK(!VulkanInstance::alive());
}

BOOST_AUTO_TEST_SUITE_END()
