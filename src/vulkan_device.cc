// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_device.h"

#include <cstring>
#include <stdexcept>

#include "log.h"
#include "vulkan_device_info.h"
#include "vulkan_error.h"

#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif

#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001
#endif

#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#ifndef VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
#define VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME "VK_KHR_get_physical_device_properties2"
#endif

namespace edge
{

namespace
{

char const *device_type_name(VkPhysicalDeviceType type)
{
	switch (type)
	{
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
	case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
	default: return "other";
	}
}

std::string version_string(uint32_t v)
{
	return std::to_string(VK_VERSION_MAJOR(v)) + "." + std::to_string(VK_VERSION_MINOR(v)) + "." + std::to_string(VK_VERSION_PATCH(v));
}

std::string join(std::vector<char const *> const &names)
{
	std::string joined;
	for (char const *name : names)
		joined += (joined.empty() ? "" : ", ") + std::string(name);
	return joined.empty() ? "none" : joined;
}

bool contains_extension_name(std::vector<VkExtensionProperties> const &extensions, char const *name)
{
	for (auto const &extension : extensions)
	{
		if (std::strcmp(extension.extensionName, name) == 0)
		{
			return true;
		}
	}
	return false;
}

bool contains_extension_name(std::vector<char const *> const &extensions, char const *name)
{
	for (auto extension : extensions)
	{
		if (std::strcmp(extension, name) == 0)
		{
			return true;
		}
	}
	return false;
}

}

VkInstance create_vulkan_instance(std::vector<std::string> const &application_extensions, bool &surface_extension,
	std::vector<std::string> &enabled)
{
	VkInstance instance = VK_NULL_HANDLE;
	std::vector<char const *> extensions;

	uint32_t available_extensions_count = 0;
	check_vk_result(vkEnumerateInstanceExtensionProperties(nullptr, &available_extensions_count, nullptr),
		"vkEnumerateInstanceExtensionProperties(count)");

	std::vector<VkExtensionProperties> available_extensions(available_extensions_count);
	if (available_extensions_count > 0)
	{
		check_vk_result(vkEnumerateInstanceExtensionProperties(nullptr, &available_extensions_count,
			&available_extensions[0]),
			"vkEnumerateInstanceExtensionProperties(list)");
	}

	if (contains_extension_name(available_extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)
		&& !contains_extension_name(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
	{
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	}

	if (contains_extension_name(available_extensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
		&& !contains_extension_name(extensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
	{
		extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	}

	// Pairs with the above to make VkPhysicalDeviceIDProperties -- and so the
	// device UUID this renderer selects and reports by -- readable on a
	// Vulkan 1.0 instance.
	if (contains_extension_name(available_extensions, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME)
		&& !contains_extension_name(extensions, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME))
	{
		extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
	}

	// The application's own, a missing one is its error: a window system's surface extension that is not there means a
	// surface cannot be made later, and the message is better here than "extension not present" from vkCreateInstance.
	for (auto const &application_extension : application_extensions)
	{
		char const *name = application_extension.c_str();
		if (!contains_extension_name(available_extensions, name))
		{
			throw std::runtime_error(std::string("Vulkan instance extension ") + name + " is not available");
		}
		if (!contains_extension_name(extensions, name))
		{
			extensions.push_back(name);
		}
	}

	VkApplicationInfo app_info;
	std::memset(&app_info, 0, sizeof(app_info));
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "edge";
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "edge";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo create_info;
	std::memset(&create_info, 0, sizeof(create_info));
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	create_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : &extensions[0];

	if (contains_extension_name(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
	{
		create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}

	VkResult create_result = vkCreateInstance(&create_info, nullptr, &instance);
	if (create_result == VK_ERROR_INCOMPATIBLE_DRIVER)
	{
		EDGE_LOG_WARN("vkCreateInstance refused api version {} (VK_ERROR_INCOMPATIBLE_DRIVER): trying again with none",
			version_string(app_info.apiVersion));
		app_info.apiVersion = 0;
		create_result = vkCreateInstance(&create_info, nullptr, &instance);
	}

	check_vk_result(create_result, "vkCreateInstance");
	surface_extension = contains_extension_name(extensions, VK_KHR_SURFACE_EXTENSION_NAME);
	enabled.assign(extensions.begin(), extensions.end());

	// the loader's version: the instance can be 1.0 whatever the devices below it can do
	uint32_t loader_version = VK_API_VERSION_1_0;
	if (auto const enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion")))
		enumerate_version(&loader_version);
	EDGE_LOG_INFO("Vulkan instance: loader {}, asked for api {}, extensions: {} ({} available)", version_string(loader_version),
		version_string(app_info.apiVersion), join(extensions), available_extensions.size());
	return instance;
}

int vulkan_graphics_queue_family(VkPhysicalDevice device)
{
	uint32_t count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
	std::vector<VkQueueFamilyProperties> families(count);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
	for (uint32_t i = 0; i < count; ++i)
	{
		if (families[i].queueCount > 0 && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

std::string vulkan_device_uuid(VkInstance instance, VkPhysicalDevice device)
{
	PFN_vkGetPhysicalDeviceProperties2KHR get_properties2 =
		reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
			vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
	if (get_properties2 == nullptr)
	{
		return std::string();
	}

	VkPhysicalDeviceIDPropertiesKHR id;
	std::memset(&id, 0, sizeof(id));
	id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR;

	VkPhysicalDeviceProperties2KHR properties2;
	std::memset(&properties2, 0, sizeof(properties2));
	properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
	properties2.pNext = &id;

	get_properties2(device, &properties2);
	// VkPhysicalDeviceIDProperties has no validity flag for deviceUUID (only
	// for the LUID), so an all-zero uuid is how a driver that did not fill it
	// in shows up.  A real device never has one.
	if (!vulkan_device_uuid_is_set(id.deviceUUID))
	{
		return std::string();
	}
	return format_vulkan_device_uuid(id.deviceUUID);
}

VkPhysicalDevice pick_vulkan_physical_device(VkInstance instance, std::string const &wanted_uuid, std::string &uuid)
{
	uint32_t device_count = 0;
	check_vk_result(vkEnumeratePhysicalDevices(instance, &device_count, nullptr),
		"vkEnumeratePhysicalDevices(count)");
	if (device_count == 0)
	{
		throw std::runtime_error("Vulkan physical device not found");
	}

	std::vector<VkPhysicalDevice> devices(device_count);
	check_vk_result(vkEnumeratePhysicalDevices(instance, &device_count, &devices[0]),
		"vkEnumeratePhysicalDevices(list)");

	for (size_t i = 0; i < devices.size(); ++i)
	{
		VkPhysicalDeviceProperties properties;
		std::memset(&properties, 0, sizeof(properties));
		vkGetPhysicalDeviceProperties(devices[i], &properties);
		int const family = vulkan_graphics_queue_family(devices[i]);
		EDGE_LOG_INFO("Vulkan device {}: {} ({}), api {}, driver 0x{:x}, vendor 0x{:04x}, device 0x{:04x}, uuid {}, {}", i, properties.deviceName,
			device_type_name(properties.deviceType), version_string(properties.apiVersion), properties.driverVersion, properties.vendorID,
			properties.deviceID, vulkan_device_uuid(instance, devices[i]), family >= 0 ? "graphics queue family " + std::to_string(family) : "no graphics queue: not suitable");
	}

	// Asked for a specific device: match on UUID and fail if it is not here.
	// Never silently fall through to the first suitable one -- a run that
	// quietly lands on the wrong GPU looks identical in every log to one that
	// landed on the right GPU, and the difference only shows up as a slow
	// PCIe link nobody thinks to check.
	if (!wanted_uuid.empty())
	{
		std::string const wanted = normalize_vulkan_device_uuid(wanted_uuid);
		if (wanted.empty())
		{
			throw std::runtime_error("malformed Vulkan device uuid: " + wanted_uuid);
		}

		std::string available;
		for (size_t i = 0; i < devices.size(); ++i)
		{
			VkPhysicalDeviceProperties properties;
			std::memset(&properties, 0, sizeof(properties));
			vkGetPhysicalDeviceProperties(devices[i], &properties);

			std::string const found = vulkan_device_uuid(instance, devices[i]);
			if (!available.empty())
			{
				available += ", ";
			}
			available += found + " (" + properties.deviceName + ")";

			if (found == wanted && vulkan_graphics_queue_family(devices[i]) >= 0)
			{
				uuid = found;
				EDGE_LOG_INFO("Vulkan device {} taken: it has the uuid that was asked for", i);
				return devices[i];
			}
		}

		throw std::runtime_error("no suitable Vulkan device with uuid " + wanted
			+ "; this machine has: " + available);
	}

	for (size_t i = 0; i < devices.size(); ++i)
	{
		if (vulkan_graphics_queue_family(devices[i]) >= 0)
		{
			uuid = vulkan_device_uuid(instance, devices[i]);
			EDGE_LOG_INFO("Vulkan device {} taken: the first with a graphics queue", i);
			return devices[i];
		}
	}
	throw std::runtime_error("no suitable Vulkan physical device found");
}

VulkanLogicalDevice create_vulkan_device(VkPhysicalDevice physical_device, bool surface_extension)
{
	VulkanLogicalDevice result;
	int const graphics_family = vulkan_graphics_queue_family(physical_device);
	if (graphics_family < 0)
	{
		throw std::runtime_error("the Vulkan device has no graphics queue");
	}
	result.graphics_queue_family = static_cast<uint32_t>(graphics_family);

	std::vector<uint32_t> unique_queues;
	unique_queues.push_back(result.graphics_queue_family);

	float queue_priority = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
	for (unsigned int unique_queue : unique_queues)
	{
		VkDeviceQueueCreateInfo queue_create_info;
		std::memset(&queue_create_info, 0, sizeof(queue_create_info));
		queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_create_info.queueFamilyIndex = unique_queue;
		queue_create_info.queueCount = 1;
		queue_create_info.pQueuePriorities = &queue_priority;
		queue_create_infos.push_back(queue_create_info);
	}

	std::vector<char const *> extensions;

	uint32_t extension_count = 0;
	check_vk_result(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr),
		"vkEnumerateDeviceExtensionProperties(count)");
	std::vector<VkExtensionProperties> available_extensions(extension_count);
	if (extension_count > 0)
	{
		check_vk_result(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count,
			&available_extensions[0]),
			"vkEnumerateDeviceExtensionProperties(list)");
	}
	if (contains_extension_name(available_extensions, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
	{
		extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
	}
	// What present() needs, where the instance can make surfaces at all (VK_KHR_swapchain needs VK_KHR_surface); a device without
	// it still renders offscreen.
	if (surface_extension && contains_extension_name(available_extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
	{
		extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		result.swapchain = true;
	}
	// The viewport of negative height that turns the image upright (viewport): Vulkan 1.0 allows it with this, and draws
	// nothing defined without it.
	if (!contains_extension_name(available_extensions, VK_KHR_MAINTENANCE1_EXTENSION_NAME))
	{
		throw std::runtime_error(std::string("the Vulkan device has no ") + VK_KHR_MAINTENANCE1_EXTENSION_NAME +
			", which edge's upright viewport needs");
	}
	extensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);

	VkPhysicalDeviceFeatures supported_features;
	vkGetPhysicalDeviceFeatures(physical_device, &supported_features);

	VkPhysicalDeviceFeatures device_features;
	std::memset(&device_features, 0, sizeof(device_features));
	if (supported_features.textureCompressionBC)
	{
		device_features.textureCompressionBC = VK_TRUE;
	}
	if (supported_features.textureCompressionASTC_LDR)
	{
		device_features.textureCompressionASTC_LDR = VK_TRUE;
	}
	if (supported_features.samplerAnisotropy)
	{
		device_features.samplerAnisotropy = VK_TRUE;
		result.anisotropy = true;
	}

	if (!supported_features.textureCompressionBC && !supported_features.textureCompressionASTC_LDR)
	{
		EDGE_LOG_WARN("the device samples neither BC nor ASTC compressed textures: every compressed texture will be refused");
	}
	if (surface_extension && !result.swapchain)
	{
		EDGE_LOG_WARN("the device has no {}: it renders offscreen only", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	}
	if (!supported_features.samplerAnisotropy)
	{
		EDGE_LOG_WARN("the device has no sampler anisotropy: textures are filtered without it");
	}
	EDGE_LOG_INFO("Vulkan device features: textureCompressionBC {}, ASTC_LDR {}, ETC2 {} (not used), samplerAnisotropy {}; extensions: {}",
		supported_features.textureCompressionBC, supported_features.textureCompressionASTC_LDR, supported_features.textureCompressionETC2,
		supported_features.samplerAnisotropy, join(extensions));
	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		VkPhysicalDeviceLimits const &l = properties.limits;
		EDGE_LOG_INFO("Vulkan device limits: push constants {} B, image 2D {}, uniform buffer {} B (offset alignment {}), sampled images per stage {}, "
			"descriptor sets {}, color attachments {}, anisotropy {}, timestamp period {} ns", l.maxPushConstantsSize, l.maxImageDimension2D,
			l.maxUniformBufferRange, l.minUniformBufferOffsetAlignment, l.maxPerStageDescriptorSampledImages, l.maxBoundDescriptorSets,
			l.maxColorAttachments, l.maxSamplerAnisotropy, l.timestampPeriod);
		VkPhysicalDeviceMemoryProperties memory;
		vkGetPhysicalDeviceMemoryProperties(physical_device, &memory);
		for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
			EDGE_LOG_INFO("Vulkan memory heap {}: {} MB{}", i, memory.memoryHeaps[i].size >> 20,
				(memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? ", device local" : "");
	}

	VkDeviceCreateInfo create_info;
	std::memset(&create_info, 0, sizeof(create_info));
	create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
	create_info.pQueueCreateInfos = queue_create_infos.empty() ? nullptr : &queue_create_infos[0];
	create_info.pEnabledFeatures = &device_features;
	create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	create_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : &extensions[0];

	check_vk_result(vkCreateDevice(physical_device, &create_info, nullptr, &result.device), "vkCreateDevice");
	vkGetDeviceQueue(result.device, result.graphics_queue_family, 0, &result.graphics_queue);
	return result;
}

}
