// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// What edge promises threads (README, Threads): everything a renderer made -- its models, caches, materials, textures, the pose caches
// their instances share -- is used by one thread at a time, and two renderers have nothing in common, so two threads that each drive
// their own renderer need no lock.  pyedge is built free-threaded on that promise.  Here two renderers draw the same scene at once,
// from two threads, and each gets the pixels one renderer gets alone.

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_model.h"
#include "camera.h"
#include "model_cache.h"
#include "pixel.h"
#include "render_target.h"
#include "scheduler.h"
#include "test_fixture.h"
#include "texture.h"
#include "vulkan_renderer.h"

namespace
{

/// A renderer of its own: the test figure (through a cache, two instances in cached poses, which share their pose cache) drawn at a few times
/// of its first clip, the target's bytes after each frame.
std::vector<std::uint8_t> draw_figure()
{
	auto const renderer = std::make_shared<edge::VulkanRenderer>(64, 64);
	auto const scheduler = std::make_shared<edge::Scheduler>(renderer);
	edge::ModelCache cache(renderer);
	std::string const file = edge::TestFixture::graphic_sources_path() + "test/data/test_figure.glb";
	auto const a = cache.load<edge::AnimatedModelPTBNJWT>(file, scheduler);
	auto const b = cache.load<edge::AnimatedModelPTBNJWT>(file, scheduler);
	auto const camera = std::make_shared<edge::Camera>();
	camera->set_perspective_projection(0.8f, 1.0f, 0.05f, 50.0f);
	camera->look_at(rove::vec<3>(0, 1, 4), rove::vec<3>(0, 1, 0), rove::vec<3>(0, 1, 0));
	edge::RenderTargetPtr const target = renderer->create_render_target(64, 64, edge::PixelFormat::A8R8G8B8);
	rove::matrix<4,4> left, right;
	left.identity();
	left.translate(rove::vec<3>(-0.6f, 0, 0));
	right.identity();
	right.translate(rove::vec<3>(0.6f, 0, 0));

	std::vector<std::uint8_t> pixels;
	for (int frame = 0; frame < 6; ++frame)
	{
		a->set_cached_pose(0, 0.1f * float(frame));
		b->set_cached_pose(0, 0.1f * float(frame));
		a->draw(camera, left);
		b->draw(camera, right);
		renderer->begin_frame();
		renderer->begin_pass(target);
		scheduler->flush();
		renderer->end_pass();
		renderer->finish_frame();
		edge::TexturePtr const colour = target->color_texture();
		colour->download();
		auto colour_mapping = colour->read();
		auto const *bytes = colour_mapping.as<std::uint8_t const>().data();
		pixels.insert(pixels.end(), bytes, bytes + 64 * 64 * 4);
		colour_mapping.unmap();
	}
	return pixels;
}

}

BOOST_AUTO_TEST_SUITE(threads)

BOOST_AUTO_TEST_CASE(two_renderers_driven_from_two_threads_at_once_draw_what_one_draws_alone)
{
	std::vector<std::uint8_t> const alone = draw_figure();
	std::size_t lit = 0;
	for (std::size_t i = 0; i < alone.size(); i += 4)
		lit += alone[i] != 0 || alone[i + 1] != 0 || alone[i + 2] != 0;
	BOOST_REQUIRE_MESSAGE(lit > 64, "the scene draws something (" << lit << " lit pixels over the frames)");

	for (int round = 0; round < 3; ++round)
	{
		std::vector<std::uint8_t> first, second;
		std::exception_ptr failed_first, failed_second;
		std::thread one([&] { try { first = draw_figure(); } catch (...) { failed_first = std::current_exception(); } });
		std::thread two([&] { try { second = draw_figure(); } catch (...) { failed_second = std::current_exception(); } });
		one.join();
		two.join();
		if (failed_first)
			std::rethrow_exception(failed_first);
		if (failed_second)
			std::rethrow_exception(failed_second);
		BOOST_CHECK_MESSAGE(first == alone, "round " << round << ": the first thread's frames are not the ones drawn alone");
		BOOST_CHECK_MESSAGE(second == alone, "round " << round << ": the second thread's frames are not the ones drawn alone");
	}
}

BOOST_AUTO_TEST_SUITE_END()
