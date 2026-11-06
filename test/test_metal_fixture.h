// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

#include "metal_renderer.h"

namespace edge
{

/// The Metal renderer of the contract tests (the counterpart of VulkanFixture and GlesFixture): headless, on the machine's GPU, drawing
/// with the translations of edge's SPIR-V (EDGE_METAL_SHADER_DIR) and of the tests' own beside their .spv.
struct MetalFixture
{
	MetalRendererPtr renderer;
	std::string error;

	MetalFixture()
	{
		// the validation layers of Metal: what a draw gets wrong is an abort with the reason, not a picture that is a little off
		// (before the first device is made; one made earlier in the process stays as it was)
		setenv("MTL_DEBUG_LAYER", "1", 0);
		setenv("MTL_SHADER_VALIDATION", "1", 0);
		try
		{
			renderer.reset(new MetalRenderer(800, 600, EDGE_METAL_SHADER_DIR, std::make_shared<MetalContext>()));
		}
		catch (std::exception const &exception)
		{
			error = exception.what();
			renderer.reset();
		}
	}

	~MetalFixture()
	{
		renderer.reset();
	}
};

}
