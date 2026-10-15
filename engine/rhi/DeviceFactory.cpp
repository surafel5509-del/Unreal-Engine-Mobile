#include "rhi/IDevice.h"

#include "core/Log.h"

#if defined(NOVA_VULKAN_COMPILED)
#include "rhi/vulkan/VulkanDevice.h"
#endif

#if defined(NOVA_GLES_COMPILED)
#include "rhi/gles/GlesDevice.h"
#endif

namespace nova::rhi {

nova::Unique<IDevice> createDevice(const DeviceDesc& desc) {
#if defined(NOVA_GLES_COMPILED)
    return createGlesDevice(desc);
#elif defined(NOVA_VULKAN_COMPILED)
    return createVulkanDevice(desc);
#else
    NOVA_LOG_ERROR("rhi: no backend compiled into this build "
                   "(NOVA_ENABLE_RHI_VULKAN=OFF or host build without NOVA_HOST_CHECK)");
    (void)desc;
    return nullptr;
#endif
}

}  // namespace nova::rhi
