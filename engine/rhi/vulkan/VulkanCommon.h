#pragma once

// Nova Engine — Vulkan backend common definitions.
//
// All backend translation units include this header instead of <vulkan.h>
// directly so the platform surface extension, VMA include and validation
// helpers stay consistent.
//
// Target: Vulkan 1.3 core (dynamic rendering + synchronization2) on Android
// 12+; the loader must report 1.3 or device creation fails with a clear log.

#if defined(__ANDROID__) || defined(NOVA_HOST_ANDROID)
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#endif

#include <vulkan/vulkan.h>

#include "core/Log.h"
#include "core/Types.h"
#include "rhi/RhiTypes.h"

namespace nova::rhi::vkutil {

inline const char* vkResultName(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
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
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VK_ERROR_<unknown>";
    }
}

/// Log a failed VkResult; returns false (so callers can `return checkVk(...)`).
inline bool checkVk(VkResult result, const char* what) {
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        return true;
    }
    NOVA_LOG_ERROR("vulkan: %s failed: %s (%d)", what, vkResultName(result),
                   static_cast<int>(result));
    return false;
}

// ------------------------------------------------------------- RHI mappings

inline VkFormat toVkFormat(Format format) {
    switch (format) {
        case Format::RGBA8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::BGRA8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::RGBA16_SFloat: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::R32_SFloat: return VK_FORMAT_R32_SFLOAT;
        case Format::RG32_SFloat: return VK_FORMAT_R32G32_SFLOAT;
        case Format::RGB32_SFloat: return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::RGBA32_SFloat: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::D32_SFloat: return VK_FORMAT_D32_SFLOAT;
        case Format::Unknown: return VK_FORMAT_UNDEFINED;
    }
    return VK_FORMAT_UNDEFINED;
}

inline Format fromVkFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNorm;
        case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8_UNorm;
        case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8_SRGB;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::RGBA16_SFloat;
        case VK_FORMAT_R32_SFLOAT: return Format::R32_SFloat;
        case VK_FORMAT_R32G32_SFLOAT: return Format::RG32_SFloat;
        case VK_FORMAT_R32G32B32_SFLOAT: return Format::RGB32_SFloat;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::RGBA32_SFloat;
        case VK_FORMAT_D32_SFLOAT: return Format::D32_SFloat;
        default: return Format::Unknown;
    }
}

inline VkPrimitiveTopology toVkTopology(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

inline VkCullModeFlags toVkCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None: return VK_CULL_MODE_NONE;
        case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    }
    return VK_CULL_MODE_NONE;
}

inline VkCompareOp toVkCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return VK_COMPARE_OP_NEVER;
        case CompareOp::Less: return VK_COMPARE_OP_LESS;
        case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
        case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
        case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

inline VkPresentModeKHR toVkPresentMode(PresentMode mode) {
    switch (mode) {
        case PresentMode::Fifo: return VK_PRESENT_MODE_FIFO_KHR;
        case PresentMode::Mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
        case PresentMode::Immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

inline VkAttachmentLoadOp toVkLoadOp(LoadOp op) {
    switch (op) {
        case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

inline VkAttachmentStoreOp toVkStoreOp(StoreOp op) {
    switch (op) {
        case StoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

}  // namespace nova::rhi::vkutil
