#pragma once

#include "VKMDevice.h"
#include "vulkan/vulkan.hpp"

namespace ddgi {

class DDGIPipeline {
private:
    vkm::VKMDevice* device{nullptr};
    vk::DescriptorSetLayout rtSceneSetLayout{VK_NULL_HANDLE};
    vk::PipelineLayout pipelineLayout{VK_NULL_HANDLE};
    vk::Pipeline tracePipeline{VK_NULL_HANDLE};
    vk::Pipeline updateIrradiancePipeline{VK_NULL_HANDLE};
    vk::Pipeline updateDepthPipeline{VK_NULL_HANDLE};
    vk::Pipeline updateDepthSquaredPipeline{VK_NULL_HANDLE};
    vk::Pipeline copyBordersPipeline{VK_NULL_HANDLE};
    vk::Pipeline relocatePipeline{VK_NULL_HANDLE};
    vk::Pipeline classifyPipeline{VK_NULL_HANDLE};
    vk::Pipeline sdfProbeUpdatePipeline{VK_NULL_HANDLE};

public:
    /**
     * Create DDGI pipeline layout, compute pipelines, and the optional RT
     * pipeline. ddgiSetLayout is set 0 and must match DDGIResources bindings.
     */
    void create(vkm::VKMDevice* device, vk::PipelineCache pipelineCache, vk::DescriptorSetLayout ddgiSetLayout);

    /**
     * Destroy all pipelines and layouts owned by this DDGI pipeline set.
     */
    void destroy();

    [[nodiscard]] bool hasComputePipelines() const { return updateIrradiancePipeline && updateDepthPipeline && updateDepthSquaredPipeline; }
    [[nodiscard]] vk::DescriptorSetLayout rayTracingSceneSetLayout() const { return rtSceneSetLayout; }
    [[nodiscard]] vk::PipelineLayout layout() const { return pipelineLayout; }
    [[nodiscard]] vk::Pipeline trace() const { return tracePipeline; }
    [[nodiscard]] vk::Pipeline updateIrradiance() const { return updateIrradiancePipeline; }
    [[nodiscard]] vk::Pipeline updateDepth() const { return updateDepthPipeline; }
    [[nodiscard]] vk::Pipeline updateDepthSquared() const { return updateDepthSquaredPipeline; }
    [[nodiscard]] vk::Pipeline copyBorders() const { return copyBordersPipeline; }
    [[nodiscard]] vk::Pipeline relocate() const { return relocatePipeline; }
    [[nodiscard]] vk::Pipeline classify() const { return classifyPipeline; }
    [[nodiscard]] vk::Pipeline sdfProbeUpdate() const { return sdfProbeUpdatePipeline; }
};

} // namespace ddgi
