// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// The renderers the contract tests hold for: Vulkan's, and OpenGL ES's where edge is built with EDGE_GLES (Backends: the
// ones that draw), and the null renderer with them where what is tested is not a pixel (AllBackends).

#include <memory>
#include <string>
#include <boost/mpl/joint_view.hpp>
#include <boost/mpl/list.hpp>

#include "null_renderer.h"
#ifdef EDGE_USE_VULKAN
#include "test_vulkan_fixture.h"
#endif
#ifdef EDGE_USE_GLES
#include "test_gles_fixture.h"
#endif
#ifdef EDGE_USE_METAL
#include "test_metal_fixture.h"
#endif

namespace edge
{

struct NullFixture
{
	std::shared_ptr<NullRenderer> renderer = std::make_shared<NullRenderer>(800, 600);
	std::string error;
};

#ifdef EDGE_USE_VULKAN
using VulkanBackend = boost::mpl::list<VulkanFixture>;
#else
typedef boost::mpl::list<> VulkanBackend;
#endif
#ifdef EDGE_USE_GLES
using GlesBackend = boost::mpl::list<GlesFixture>;
#else
typedef boost::mpl::list<> GlesBackend;
#endif
#ifdef EDGE_USE_METAL
typedef boost::mpl::list<MetalFixture> MetalBackend;
#else
using MetalBackend = boost::mpl::list<>;
#endif

#if !defined(EDGE_USE_VULKAN) && !defined(EDGE_USE_GLES) && !defined(EDGE_USE_METAL)
#error "the tests need a renderer that draws"
#endif

using Backends = boost::mpl::joint_view<boost::mpl::joint_view<VulkanBackend, GlesBackend>, MetalBackend>;
using AllBackends = boost::mpl::joint_view<boost::mpl::list<NullFixture>, Backends>;

}
