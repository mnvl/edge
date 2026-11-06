// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>
#include "log.h"
#include "vulkan_renderer.h"

BOOST_AUTO_TEST_SUITE(renderer)

BOOST_AUTO_TEST_CASE(basic)
{
	edge::VulkanRendererPtr renderer(new edge::VulkanRenderer(800, 600));

	for (int i = 0; i < 10; ++i)
	{
		renderer->begin_frame();
		renderer->begin_pass(renderer->default_render_target());
		renderer->end_pass();
		renderer->finish_frame();
	}
}

BOOST_AUTO_TEST_SUITE_END()
