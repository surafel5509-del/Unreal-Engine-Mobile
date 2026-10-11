#pragma once

// Nova Engine — Vulkan backend entry point (internal backend header).

#include "rhi/IDevice.h"

namespace nova::rhi {

/// Create a Vulkan 1.3 device: instance, debug messenger (when validation is
/// requested and available), physical/logical device, queues and VMA. No
/// surface is created here — attach one with setSurface().
/// Returns nullptr (and logs the reason) when the loader/GPU does not
/// provide Vulkan 1.3 with dynamic rendering + synchronization2.
nova::Unique<IDevice> createVulkanDevice(const DeviceDesc& desc);

}  // namespace nova::rhi
