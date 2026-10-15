#pragma once
#include "rhi/IDevice.h"
namespace nova::rhi {
Unique<IDevice> createGlesDevice(const DeviceDesc& desc);
}
