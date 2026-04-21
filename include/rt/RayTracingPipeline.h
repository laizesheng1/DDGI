#pragma once

#include "VKMDevice.h"
#include "vulkan/vulkan.hpp"

namespace rt {

class RayTracingPipeline {
public:
    void create(vkm::VKMDevice* device, vk::PipelineCache pipelineCache);
    void destroy();

    [[nodiscard]] vk::Pipeline pipeline() const { return rtPipeline; }
    [[nodiscard]] vk::PipelineLayout layout() const { return pipelineLayout; }

private:
    vkm::VKMDevice* device{nullptr};
    vk::PipelineLayout pipelineLayout{VK_NULL_HANDLE};
    vk::Pipeline rtPipeline{VK_NULL_HANDLE};
};

} // namespace rt
