// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "vulkan_device_info.h"
#include "vulkan_instance.h"

namespace edge
{

bool vulkan_device_uuid_is_set(uint8_t const *uuid)
{
	for (int i = 0; i < VK_UUID_SIZE; ++i)
	{
		if (uuid[i] != 0)
		{
			return true;
		}
	}
	return false;
}

std::string format_vulkan_device_uuid(uint8_t const *uuid)
{
	static char const *hex = "0123456789abcdef";
	std::string out;
	out.reserve(36);
	for (int i = 0; i < VK_UUID_SIZE; ++i)
	{
		if (i == 4 || i == 6 || i == 8 || i == 10)
		{
			out.push_back('-');
		}
		out.push_back(hex[(uuid[i] >> 4) & 0xF]);
		out.push_back(hex[uuid[i] & 0xF]);
	}
	return out;
}

std::string normalize_vulkan_device_uuid(std::string const &text)
{
	std::string digits;
	digits.reserve(32);

	// Skip an "GPU-" prefix if present -- that is how nvidia-smi prints it.
	size_t start = 0;
	if (text.size() >= 4
		&& (text[0] == 'G' || text[0] == 'g')
		&& (text[1] == 'P' || text[1] == 'p')
		&& (text[2] == 'U' || text[2] == 'u')
		&& text[3] == '-')
	{
		start = 4;
	}

	for (size_t i = start; i < text.size(); ++i)
	{
		unsigned char const c = static_cast<unsigned char>(text[i]);
		if (c == '-')
		{
			continue;
		}
		if (!std::isxdigit(c))
		{
			return std::string();
		}
		digits.push_back(static_cast<char>(std::tolower(c)));
	}

	if (digits.size() != 32)
	{
		return std::string();
	}

	std::string out;
	out.reserve(36);
	for (size_t i = 0; i < digits.size(); ++i)
	{
		if (i == 8 || i == 12 || i == 16 || i == 20)
		{
			out.push_back('-');
		}
		out.push_back(digits[i]);
	}
	return out;
}

std::vector<PhysicalDeviceInfo> enumerate_vulkan_physical_devices()
{
	// the process's instance: its renderers', or one made for this and let go when it returns
	VulkanInstancePtr const shared = VulkanInstance::acquire(std::vector<std::string>());
	VkInstance const instance = shared->get();
	// what makes deviceUUID and the PCI address readable on a Vulkan 1.0 instance; without it the enumeration reports less
	bool const has_properties2 = shared->has_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
		&& shared->has_extension(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);

	std::vector<PhysicalDeviceInfo> result;
	{
		uint32_t device_count = 0;
		if (vkEnumeratePhysicalDevices(instance, &device_count, nullptr) != VK_SUCCESS)
		{
			throw std::runtime_error("vkEnumeratePhysicalDevices failed");
		}

		std::vector<VkPhysicalDevice> devices(device_count);
		if (device_count > 0)
		{
			if (vkEnumeratePhysicalDevices(instance, &device_count, &devices[0]) != VK_SUCCESS)
			{
				throw std::runtime_error("vkEnumeratePhysicalDevices failed");
			}
		}

		PFN_vkGetPhysicalDeviceProperties2KHR get_properties2 = nullptr;
		if (has_properties2)
		{
			get_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
				vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
		}

		for (auto &device : devices)
		{
			PhysicalDeviceInfo info;

			VkPhysicalDeviceProperties properties;
			std::memset(&properties, 0, sizeof(properties));
			vkGetPhysicalDeviceProperties(device, &properties);
			info.name = properties.deviceName;
			info.discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

			if (get_properties2 != nullptr)
			{
				VkPhysicalDevicePCIBusInfoPropertiesEXT pci;
				std::memset(&pci, 0, sizeof(pci));
				pci.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;

				VkPhysicalDeviceIDPropertiesKHR id;
				std::memset(&id, 0, sizeof(id));
				id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR;
				id.pNext = &pci;

				VkPhysicalDeviceProperties2KHR properties2;
				std::memset(&properties2, 0, sizeof(properties2));
				properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
				properties2.pNext = &id;

				get_properties2(device, &properties2);

				if (vulkan_device_uuid_is_set(id.deviceUUID))
				{
					info.uuid = format_vulkan_device_uuid(id.deviceUUID);
				}

				// Zeroed unless the device actually advertises
				// VK_EXT_pci_bus_info, in which case a real address never has
				// every field zero.
				if (pci.pciBus != 0 || pci.pciDevice != 0 || pci.pciDomain != 0)
				{
					std::ostringstream address;
					address << std::hex << std::nouppercase;
					address.fill('0');
					address.width(4);
					address << pci.pciDomain << ':';
					address.width(2);
					address << pci.pciBus << ':';
					address.width(2);
					address << pci.pciDevice << '.' << pci.pciFunction;
					info.pci_bus = address.str();
				}
			}

			result.push_back(info);
		}
	}
	return result;
}

}  // namespace edge
