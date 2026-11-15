// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "vulkan_renderer.h"
#include <memory>
#include <stdexcept>
#include <string>

namespace edge
{

struct VulkanFixture
{
	std::shared_ptr<VulkanRenderer> renderer;
	std::string error;

	VulkanFixture()
	{
		try
		{
			renderer = std::make_shared<VulkanRenderer>(800, 600);
		}
		catch (std::exception const &exception)
		{
			error = exception.what();
			renderer.reset();
		}
	}

	~VulkanFixture()
	{
		renderer.reset();
	}
};

}
