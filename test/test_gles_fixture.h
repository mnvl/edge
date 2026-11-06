// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include "gles_renderer.h"
#include "gles_sdl_context.h"

namespace edge
{

/// The GLES renderer of the contract tests (the counterpart of VulkanFixture): headless on SDL's offscreen driver, drawing with the
/// translations of edge's SPIR-V (EDGE_GLES_SHADER_DIR).
struct GlesFixture
{
	GLESRendererPtr renderer;
	std::string error;

	GlesFixture()
	{
		try
		{
			if (!std::getenv("SDL_VIDEO_DRIVER"))
				setenv("SDL_VIDEO_DRIVER", "offscreen", 1);
			renderer = std::make_shared<GLESRenderer>(800, 600, EDGE_GLES_SHADER_DIR, std::make_shared<GLESSDLContext>());
		}
		catch (std::exception const &exception)
		{
			error = exception.what();
			renderer.reset();
		}
	}

	~GlesFixture()
	{
		renderer.reset();
	}
};

}
