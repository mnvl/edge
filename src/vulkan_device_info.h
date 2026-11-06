// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace edge
{

// One Vulkan physical device, in the terms a caller can choose by.
//
// The uuid is the selection key rather than the name or the enumeration
// index.  Two identical cards report byte-identical names, and the index is
// PCI bus order -- which is to say, which slot a card is plugged into.  The
// uuid is stable across both, and for NVIDIA devices it is the same value
// NVML and nvidia-smi report, so `nvidia-smi --query-gpu=uuid` names a device
// this code can be handed.
struct PhysicalDeviceInfo {
	std::string name;
	std::string uuid;     // canonical 8-4-4-4-12 lowercase hex
	std::string pci_bus;  // "0000:05:00.0", or empty if the device does not say
	bool discrete;
};

// Every Vulkan device on this machine, through the process's instance
// (VulkanInstance): its renderers' when there are any, otherwise one made for
// this and let go, so it can be called before any renderer exists.
std::vector<PhysicalDeviceInfo> enumerate_vulkan_physical_devices();

// VkPhysicalDeviceIDProperties carries no validity flag for deviceUUID (only
// for the LUID), so "the driver did not fill this in" shows up as all zeros.
bool vulkan_device_uuid_is_set(uint8_t const *uuid);

// 16 raw bytes -> canonical 8-4-4-4-12 lowercase hex.
std::string format_vulkan_device_uuid(uint8_t const *uuid);

// Accepts what a human is likely to have to hand: the canonical form, the
// same thing uppercased, with or without dashes, and with the "GPU-" prefix
// nvidia-smi prints.  Returns the canonical form, or an empty string if the
// input is not 32 hex digits.
std::string normalize_vulkan_device_uuid(std::string const &text);

}  // namespace edge
