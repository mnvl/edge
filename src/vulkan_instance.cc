// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_instance.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>

#include "log.h"
#include "vulkan_device.h"
#include "vulkan_error.h"

namespace edge
{

namespace
{

std::mutex instance_lock;
std::weak_ptr<VulkanInstance> live_instance;       // under instance_lock
std::atomic<uint64_t> instances_made{0};

bool loader_has(char const *name)
{
	uint32_t count = 0;
	check_vk_result(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "vkEnumerateInstanceExtensionProperties(count)");
	std::vector<VkExtensionProperties> available(count);
	if (count > 0)
		check_vk_result(vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()), "vkEnumerateInstanceExtensionProperties(list)");
	return std::any_of(available.begin(), available.end(), [&](VkExtensionProperties const &e) { return std::strcmp(e.extensionName, name) == 0; });
}

}

std::shared_ptr<VulkanInstance> VulkanInstance::acquire(std::vector<std::string> const &application_extensions)
{
	std::lock_guard<std::mutex> const lock(instance_lock);
	if (std::shared_ptr<VulkanInstance> const live = live_instance.lock())
	{
		for (std::string const &name : application_extensions)
		{
			if (live->has_extension(name))
				continue;
			if (!loader_has(name.c_str()))
				throw std::runtime_error("Vulkan instance extension " + name + " is not available");
			throw std::logic_error("Vulkan instance extension " + name + " was asked for, and the process's instance, which its renderers "
				"share, was made without it: make first the renderer that needs it");
		}
		EDGE_LOG_INFO("Vulkan instance: the process's, shared ({} holders)", live.use_count() - 1);
		return live;
	}
	std::shared_ptr<VulkanInstance> made(new VulkanInstance());
	made->instance_ = create_vulkan_instance(application_extensions, made->surface_extension_, made->extensions_);
	++instances_made;
	live_instance = made;
	return made;
}

uint64_t VulkanInstance::made_count()
{
	return instances_made.load();
}

bool VulkanInstance::alive()
{
	std::lock_guard<std::mutex> const lock(instance_lock);
	return !live_instance.expired();
}

VulkanInstance::~VulkanInstance()
{
	if (instance_ != VK_NULL_HANDLE)
	{
		vkDestroyInstance(instance_, nullptr);
		EDGE_LOG_INFO("Vulkan instance destroyed: its last holder let it go");
	}
}

bool VulkanInstance::has_extension(std::string const &name) const
{
	return std::find(extensions_.begin(), extensions_.end(), name) != extensions_.end();
}

}
