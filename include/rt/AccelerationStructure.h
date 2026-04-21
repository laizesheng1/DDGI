#pragma once

#include "VKMDevice.h"
#include "vulkan/vulkan.hpp"

namespace rt {

struct AccelerationStructure {
    vk::AccelerationStructureKHR handle{VK_NULL_HANDLE};
    vk::Buffer buffer{VK_NULL_HANDLE};
    vk::DeviceMemory memory{VK_NULL_HANDLE};
    vk::DeviceAddress deviceAddress{0};
};

class AccelerationStructureBuilder {
public:
    void create(vkm::VKMDevice* device);
    void destroy();

    [[nodiscard]] const AccelerationStructure& topLevel() const { return tlas; }

private:
    vkm::VKMDevice* device{nullptr};
    AccelerationStructure tlas{};
};

} // namespace rt
