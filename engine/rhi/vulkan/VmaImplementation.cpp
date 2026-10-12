// Nova Engine — the single translation unit that instantiates VulkanMemoryAllocator.
//
// VMA is a vendored single-header library: exactly one TU must define
// VMA_IMPLEMENTATION before including it, otherwise every vmaCreateAllocator /
// vmaCreateBuffer call is an undefined symbol at link time. It lives in its own
// TU so the (large) implementation is compiled once and never re-parsed by the
// backend sources that only need the declarations.
//
// Why this was invisible on the host: nova_engine is a static library, so an
// object file is only pulled into an executable when something references it.
// The host test binary never references the Vulkan backend, and the host-check
// JNI target is an OBJECT library, so the missing symbols only appeared when the
// NDK linked libnova.so with `-Wl,--no-undefined`.
//
// Configuration:
//  * VMA_STATIC_VULKAN_FUNCTIONS 0 — VMA must not reference Vulkan entry points
//    directly. Android's libvulkan.so exports only what the platform API level
//    supports, so a static reference to a 1.1+ entry point can fail to link a
//    device build even when the driver supports it.
//  * VMA_DYNAMIC_VULKAN_FUNCTIONS 1 — VMA resolves everything through
//    vkGetInstanceProcAddr / vkGetDeviceProcAddr instead (both are Vulkan 1.0
//    and always exported).
//  * VMA_VULKAN_VERSION 1003000 — the backend targets Vulkan 1.3, matching
//    VmaAllocatorCreateInfo::vulkanApiVersion in VulkanDevice.cpp.
//
// Thread-safety: VMA's allocator is created and destroyed on the render thread;
// allocation calls are internally synchronized (VMA uses its own mutexes unless
// the externally-synchronized flag is set, which this engine does not use).

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1003000
#define VMA_IMPLEMENTATION

#include <vk_mem_alloc.h>
