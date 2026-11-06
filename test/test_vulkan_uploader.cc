// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// What a buffer upload costs, by where it is made: nothing extra while the renderer is idle (it rides in the next
// frame) or inside a frame between passes (it goes into the frame's command buffer).  Inside a pass, where transfers are
// illegal, it is refused: a copy sent on its own would land before the uploads the frame already holds.  Counted in the
// renderer's single-time submits, each of which stalls on the GPU.
// What the uploads deliver is tested in test_vulkan_buffer_memory.cc.

#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <vector>

#include "pixel.h"
#include "render_target.h"
#include "test_vulkan_fixture.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "vulkan_renderer.h"

using namespace edge;

namespace
{

void write(VertexBufferPtr const &vb)
{
	auto vb_mapping = vb->write(Contents::DISCARD);
	void *data = vb_mapping.data();
	BOOST_REQUIRE(data);
	vb_mapping.unmap();
}

}

BOOST_AUTO_TEST_SUITE(vulkan_uploader)

BOOST_AUTO_TEST_CASE(an_upload_while_idle_is_deferred_into_the_next_frame)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	VertexBufferPtr const vb = f.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	uint64_t const before = f.renderer->single_time_submit_count();
	write(vb);
	write(vb);
	BOOST_CHECK_EQUAL(f.renderer->single_time_submit_count(), before);
	f.renderer->begin_frame();
	f.renderer->finish_frame();
	BOOST_CHECK_EQUAL(f.renderer->single_time_submit_count(), before + 1);   // the frame's own
}

BOOST_AUTO_TEST_CASE(an_upload_inside_a_frame_between_passes_costs_no_submit)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	VertexBufferPtr const vb = f.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	RenderTargetPtr const target = f.renderer->create_render_target(32, 32, PixelFormat::A8R8G8B8);
	uint64_t const before = f.renderer->single_time_submit_count();
	f.renderer->begin_frame();
	write(vb);
	f.renderer->begin_pass(target);
	f.renderer->end_pass();
	write(vb);
	f.renderer->finish_frame();
	BOOST_CHECK_EQUAL(f.renderer->single_time_submit_count(), before + 1);
}

BOOST_AUTO_TEST_CASE(an_upload_inside_a_pass_is_refused)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	VertexBufferPtr const vb = f.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	RenderTargetPtr const target = f.renderer->create_render_target(32, 32, PixelFormat::A8R8G8B8);
	f.renderer->begin_frame();
	f.renderer->begin_pass(target);
	uint64_t const before = f.renderer->single_time_submit_count();
	BOOST_CHECK_THROW(write(vb), std::logic_error);
	BOOST_CHECK_EQUAL(f.renderer->single_time_submit_count(), before);
	f.renderer->end_pass();
	f.renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE(oversized_uploads_work_across_many_frames)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	// bigger than the staging blocks of 1 MiB, and several of them in one frame
	size_t const size = sizeof(VERTEX_PT) * 100000;
	std::vector<VertexBufferPtr> buffers;
	for (int i = 0; i < 3; ++i)
	{
		buffers.push_back(f.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), size));
	}
	for (int frame = 0; frame < 5; ++frame)
	{
		for (auto const &vb : buffers)
		{
			write(vb);
		}
		f.renderer->begin_frame();
		for (auto const &vb : buffers)
		{
			write(vb);
		}
		f.renderer->finish_frame();
	}
}

/// The staging space a load takes (everything written while the renderer is idle waits in it for the next frame) is given
/// back once a frame has gone by without needing it; the space every frame uses is kept.
BOOST_AUTO_TEST_CASE(the_staging_space_a_load_took_is_given_back_once_no_frame_needs_it)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	size_t const size = sizeof(VERTEX_PT) * 100000;              // past the 1 MiB of a block
	std::vector<VertexBufferPtr> loaded;
	for (int i = 0; i < 4; ++i)
	{
		loaded.push_back(f.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), size));
		write(loaded.back());
	}
	BOOST_CHECK_GE(f.renderer->staging_arena_size(), 4 * size);
	VertexBufferPtr const every_frame = f.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	for (int frame = 0; frame < 3; ++frame)
	{
		f.renderer->begin_frame();
		write(every_frame);
		f.renderer->finish_frame();
	}
	BOOST_CHECK_LE(f.renderer->staging_arena_size(), size_t(1) << 20);
}

BOOST_AUTO_TEST_CASE(buffers_destroyed_in_every_state_are_released_cleanly)
{
	VulkanFixture f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	RenderTargetPtr const target = f.renderer->create_render_target(32, 32, PixelFormat::A8R8G8B8);
	auto make = [&]() { return f.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4); };
	{ VertexBufferPtr vb = make(); write(vb); }            // idle, upload still deferred
	f.renderer->begin_frame();
	{ VertexBufferPtr vb = make(); write(vb); }            // inside a frame
	VertexBufferPtr written_in_the_frame = make();
	write(written_in_the_frame);
	f.renderer->begin_pass(target);
	{ VertexBufferPtr vb = make(); }                        // inside a pass
	written_in_the_frame.reset();                           // with its upload in the frame's command buffer
	f.renderer->end_pass();
	f.renderer->finish_frame();
	f.renderer->begin_frame();
	f.renderer->finish_frame();
}

BOOST_AUTO_TEST_SUITE_END()
