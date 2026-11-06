// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// What a Vulkan resource owns of its renderer: the device, never the renderer.  A buffer, a texture or a render target a caller keeps
// (pyedge hands them out on their own) does not keep its renderer alive; once the renderer is gone it can still be let go, and
// anything else it is asked to do is a std::logic_error, not a call into a renderer that is not there.

#include <cstring>
#include <memory>
#include <stdexcept>

#include <boost/test/unit_test.hpp>

#include "index.h"
#include "index_buffer.h"
#include "pixel.h"
#include "render_target.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"
#include "vulkan_renderer.h"

using namespace edge;

BOOST_AUTO_TEST_SUITE(vulkan_ownership)

BOOST_AUTO_TEST_CASE(no_resource_keeps_its_renderer_alive_and_each_outlives_it)
{
	auto renderer = std::make_shared<VulkanRenderer>(16, 16);
	std::weak_ptr<VulkanRenderer> const alive = renderer;
	VertexBufferPtr const vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);
	IndexBufferPtr const ib = renderer->create_index_buffer(IndexFormat::U16, 6);
	TexturePtr const texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	RenderTargetPtr const target = renderer->create_render_target(4, 4, PixelFormat::A8R8G8B8);
	auto vb_mapping = vb->write(Contents::DISCARD);
	std::memset(vb_mapping.data(), 0, sizeof(VERTEX_PT) * 3);
	vb_mapping.unmap();
	renderer.reset();
	BOOST_CHECK(alive.expired());

	// what needs the renderer is refused
	auto vb_mapping2 = vb->write(Contents::DISCARD);
	std::memset(vb_mapping2.data(), 1, sizeof(VERTEX_PT) * 3);       // (the CPU copy is the buffer's own)
	BOOST_CHECK_THROW(vb_mapping2.unmap(), std::logic_error);
	auto ib_mapping = ib->write(Contents::DISCARD);
	BOOST_CHECK_THROW(ib_mapping.unmap(), std::logic_error);
	auto texture_mapping = texture->write(Contents::DISCARD);
	BOOST_CHECK_THROW(texture_mapping.unmap(), std::logic_error);
	// and letting them go is not (the end of the test)
}

BOOST_AUTO_TEST_SUITE_END()
