// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>

namespace edge
{

char const *vk_result_name(VkResult result);
char const *vk_result_description(VkResult result);
std::string format_vulkan_error_message(char const *operation, VkResult result);

class vulkan_error: public std::runtime_error
{
public:
	vulkan_error(char const *operation, VkResult result);

	VkResult result() const noexcept { return result_; }
	std::string const &operation() const noexcept { return operation_; }

private:
	VkResult result_;
	std::string operation_;
};

void check_vk_result(VkResult result, char const *operation);

}
