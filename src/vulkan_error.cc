// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vulkan_error.h"

#include <sstream>

namespace edge
{

char const *vk_result_name(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS: return "VK_SUCCESS";
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_EVENT_SET: return "VK_EVENT_SET";
	case VK_EVENT_RESET: return "VK_EVENT_RESET";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
#ifdef VK_THREAD_IDLE_KHR
	case VK_THREAD_IDLE_KHR: return "VK_THREAD_IDLE_KHR";
#endif
#ifdef VK_THREAD_DONE_KHR
	case VK_THREAD_DONE_KHR: return "VK_THREAD_DONE_KHR";
#endif
#ifdef VK_OPERATION_DEFERRED_KHR
	case VK_OPERATION_DEFERRED_KHR: return "VK_OPERATION_DEFERRED_KHR";
#endif
#ifdef VK_OPERATION_NOT_DEFERRED_KHR
	case VK_OPERATION_NOT_DEFERRED_KHR: return "VK_OPERATION_NOT_DEFERRED_KHR";
#endif
#ifdef VK_PIPELINE_COMPILE_REQUIRED
	case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
#endif
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
	case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
#ifdef VK_ERROR_OUT_OF_POOL_MEMORY
	case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
#endif
#ifdef VK_ERROR_INVALID_EXTERNAL_HANDLE
	case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
#endif
#ifdef VK_ERROR_FRAGMENTATION
	case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
#endif
#ifdef VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS
	case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
#endif
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
#ifdef VK_ERROR_INCOMPATIBLE_DISPLAY_KHR
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
#endif
#ifdef VK_ERROR_VALIDATION_FAILED_EXT
	case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
#endif
#ifdef VK_ERROR_INVALID_SHADER_NV
	case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
#endif
#ifdef VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT
	case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
		return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
#endif
#ifdef VK_ERROR_NOT_PERMITTED_KHR
	case VK_ERROR_NOT_PERMITTED_KHR: return "VK_ERROR_NOT_PERMITTED_KHR";
#endif
#ifdef VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
	case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
#endif
#ifdef VK_ERROR_COMPRESSION_EXHAUSTED_EXT
	case VK_ERROR_COMPRESSION_EXHAUSTED_EXT: return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
#endif
	default: return "VK_RESULT_UNKNOWN";
	}
}

char const *vk_result_description(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS: return "Command completed successfully.";
	case VK_NOT_READY: return "A fence or query has not yet completed.";
	case VK_TIMEOUT: return "A wait operation timed out.";
	case VK_EVENT_SET: return "An event is signaled.";
	case VK_EVENT_RESET: return "An event is unsignaled.";
	case VK_INCOMPLETE: return "A return array was too small for all results.";
	case VK_SUBOPTIMAL_KHR: return "Swapchain no longer matches surface exactly, but can still present.";
#ifdef VK_THREAD_IDLE_KHR
	case VK_THREAD_IDLE_KHR: return "A deferred operation thread has no work.";
#endif
#ifdef VK_THREAD_DONE_KHR
	case VK_THREAD_DONE_KHR: return "A deferred operation thread finished all work.";
#endif
#ifdef VK_OPERATION_DEFERRED_KHR
	case VK_OPERATION_DEFERRED_KHR: return "A deferred operation was enqueued and will complete later.";
#endif
#ifdef VK_OPERATION_NOT_DEFERRED_KHR
	case VK_OPERATION_NOT_DEFERRED_KHR: return "A deferred operation completed immediately.";
#endif
#ifdef VK_PIPELINE_COMPILE_REQUIRED
	case VK_PIPELINE_COMPILE_REQUIRED: return "Pipeline needs compilation before execution.";
#endif
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "Host memory allocation failed.";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "Device memory allocation failed.";
	case VK_ERROR_INITIALIZATION_FAILED: return "Initialization of a Vulkan object failed.";
	case VK_ERROR_DEVICE_LOST: return "The Vulkan device was lost.";
	case VK_ERROR_MEMORY_MAP_FAILED: return "Mapping device memory failed.";
	case VK_ERROR_LAYER_NOT_PRESENT: return "A requested Vulkan layer is unavailable.";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "A requested Vulkan extension is unavailable.";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "A requested Vulkan feature is unsupported.";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "No compatible Vulkan driver or ICD was found.";
	case VK_ERROR_TOO_MANY_OBJECTS: return "The implementation limit for object count was exceeded.";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "The requested pixel/format is unsupported.";
	case VK_ERROR_FRAGMENTED_POOL: return "Descriptor or command pool is fragmented.";
	case VK_ERROR_UNKNOWN: return "An unknown Vulkan error occurred.";
#ifdef VK_ERROR_OUT_OF_POOL_MEMORY
	case VK_ERROR_OUT_OF_POOL_MEMORY: return "Descriptor pool allocation failed due to insufficient pool memory.";
#endif
#ifdef VK_ERROR_INVALID_EXTERNAL_HANDLE
	case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "A provided external handle is invalid.";
#endif
#ifdef VK_ERROR_FRAGMENTATION
	case VK_ERROR_FRAGMENTATION: return "A fragmentation-related allocation failure occurred.";
#endif
#ifdef VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS
	case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
		return "An invalid opaque capture address was used.";
#endif
	case VK_ERROR_SURFACE_LOST_KHR: return "The presentation surface is no longer available.";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "The native window is already in use.";
	case VK_ERROR_OUT_OF_DATE_KHR: return "The swapchain is out of date and must be recreated.";
#ifdef VK_ERROR_INCOMPATIBLE_DISPLAY_KHR
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "The display is incompatible with this surface.";
#endif
#ifdef VK_ERROR_VALIDATION_FAILED_EXT
	case VK_ERROR_VALIDATION_FAILED_EXT: return "Validation layer checks failed.";
#endif
#ifdef VK_ERROR_INVALID_SHADER_NV
	case VK_ERROR_INVALID_SHADER_NV: return "A shader module is invalid for this driver/device.";
#endif
#ifdef VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT
	case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
		return "Invalid DRM format modifier plane layout.";
#endif
#ifdef VK_ERROR_NOT_PERMITTED_KHR
	case VK_ERROR_NOT_PERMITTED_KHR: return "The operation is not permitted.";
#endif
#ifdef VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
	case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
		return "Exclusive fullscreen mode was lost.";
#endif
#ifdef VK_ERROR_COMPRESSION_EXHAUSTED_EXT
	case VK_ERROR_COMPRESSION_EXHAUSTED_EXT:
		return "Compression resources are exhausted.";
#endif
	default: return "Unknown VkResult code.";
	}
}

std::string format_vulkan_error_message(char const *operation, VkResult result)
{
	std::ostringstream stream;
	stream << (operation ? operation : "Vulkan operation")
		<< " failed with " << vk_result_name(result)
		<< " (" << static_cast<int>(result) << "): "
		<< vk_result_description(result);
	return stream.str();
}

vulkan_error::vulkan_error(char const *operation, VkResult result):
	std::runtime_error(format_vulkan_error_message(operation, result)),
	result_(result),
	operation_(operation ? operation : "")
{
}

void check_vk_result(VkResult result, char const *operation)
{
	if (result != VK_SUCCESS)
	{
		throw vulkan_error(operation, result);
	}
}

}
