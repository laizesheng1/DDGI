#include "rt/AccelerationStructure.h"

namespace rt {

void AccelerationStructureBuilder::create(vkm::VKMDevice* inDevice)
{
    device = inDevice;
}

void AccelerationStructureBuilder::destroy()
{
    if (device == nullptr) {
        return;
    }

    auto logicalDevice = device->logicalDevice;
    if (tlas.handle) logicalDevice.destroyAccelerationStructureKHR(tlas.handle);
    if (tlas.buffer) logicalDevice.destroyBuffer(tlas.buffer);
    if (tlas.memory) logicalDevice.freeMemory(tlas.memory);
    tlas = {};
    device = nullptr;
}

} // namespace rt
