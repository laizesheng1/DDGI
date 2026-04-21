#pragma once

#include "Buffer.h"
#include "VKMDevice.h"

namespace rt {

class ShaderBindingTable {
public:
    void create(vkm::VKMDevice* device);
    void destroy();

private:
    vkm::VKMDevice* device{nullptr};
    vkm::Buffer raygen{};
    vkm::Buffer miss{};
    vkm::Buffer hit{};
};

} // namespace rt
