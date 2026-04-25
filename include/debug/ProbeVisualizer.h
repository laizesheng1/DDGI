#pragma once

#include "camera.hpp"
#include "ddgi/DDGIVolume.h"

namespace debug {

class ProbeVisualizer {
private:
    vkm::VKMDevice* device{nullptr};
    vk::PipelineLayout pipelineLayoutHandle{VK_NULL_HANDLE};
    vk::Pipeline pipelineHandle{VK_NULL_HANDLE};

public:
    /**
     * Create the instanced probe marker pipeline used on top of the lit scene.
     */
    void create(vkm::VKMDevice* device, vk::RenderPass renderPass, vk::PipelineCache pipelineCache);

    /**
     * Release graphics state used by the probe overlay.
     */
    void destroy();

    /**
     * Draw one small billboard marker per probe. The current version uses one
     * color for all probes and focuses on position validation first.
     */
    void draw(vk::CommandBuffer commandBuffer,
              const ddgi::DDGIVolume& volume,
              const Camera& camera,
              vk::Extent2D framebufferExtent);
};

} // namespace debug
