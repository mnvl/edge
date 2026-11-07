// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_instance.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

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
// The process's instance, once made, whoever holds it (under instance_lock).  It is kept when the last renderer goes: a driver may not
// give back what an instance took (NVIDIA's loses 4 file descriptors each time, and vkCreateDevice fails after about 150 rounds of making
// and dropping a renderer).  Never destroyed: on the way out of a process the driver's own statics may already be gone.  A fork's child
// cannot use its parent's, which it forgets rather than destroys.
std::shared_ptr<VulkanInstance> *kept_instance = nullptr;
long kept_for_process = 0;

long this_process()
{
#if defined(_WIN32)
	return long(_getpid());
#else
	return long(getpid());
#endif
}
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
	if (kept_instance && kept_for_process != this_process())
		kept_instance = nullptr;       // (the parent's: not this process's to use or to destroy)
	if (kept_instance)
	{
		std::shared_ptr<VulkanInstance> const &live = *kept_instance;
		bool has_all = true;
		for (std::string const &name : application_extensions)
		{
			if (live->has_extension(name))
				continue;
			if (!loader_has(name.c_str()))
				throw std::runtime_error("Vulkan instance extension " + name + " is not available");
			has_all = false;
			// with no renderer on it, the kept one is made again with what is asked
			if (live.use_count() > 1)
				throw std::logic_error("Vulkan instance extension " + name + " was asked for, and the process's instance, which its renderers "
					"share, was made without it: make first the renderer that needs it");
		}
		if (has_all)
		{
			EDGE_LOG_INFO("Vulkan instance: the process's, shared ({} holders)", live.use_count() - 1);
			return live;
		}
		delete kept_instance;
		kept_instance = nullptr;
	}
	std::shared_ptr<VulkanInstance> made(new VulkanInstance());
	made->instance_ = create_vulkan_instance(application_extensions, made->surface_extension_, made->extensions_);
	++instances_made;
	kept_instance = new std::shared_ptr<VulkanInstance>(made);
	kept_for_process = this_process();
	return made;
}

uint64_t VulkanInstance::made_count()
{
	return instances_made.load();
}

bool VulkanInstance::alive()
{
	std::lock_guard<std::mutex> const lock(instance_lock);
	return kept_instance && kept_for_process == this_process() && kept_instance->use_count() > 1;
}

void VulkanInstance::release_unused()
{
	std::lock_guard<std::mutex> const lock(instance_lock);
	if (kept_instance && kept_for_process == this_process() && kept_instance->use_count() == 1)
	{
		delete kept_instance;
		kept_instance = nullptr;
	}
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
