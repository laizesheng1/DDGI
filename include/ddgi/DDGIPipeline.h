#pragma once

#include "VKMDevice.h"
#include "vulkan/vulkan.hpp"

namespace ddgi {

class DDGIPipeline {
public:
    void create(vkm::VKMDevice* device, vk::PipelineCache pipelineCache);
    void destroy();

    [[nodiscard]] vk::PipelineLayout layout() const { return pipelineLayout; }

private:
    vkm::VKMDevice* device{nullptr};
    vk::PipelineLayout pipelineLayout{VK_NULL_HANDLE};
    vk::Pipeline tracePipeline{VK_NULL_HANDLE};
    vk::Pipeline updateIrradiancePipeline{VK_NULL_HANDLE};
    vk::Pipeline updateDepthPipeline{VK_NULL_HANDLE};
    vk::Pipeline updateDepthSquaredPipeline{VK_NULL_HANDLE};
};

} // namespace ddgi
