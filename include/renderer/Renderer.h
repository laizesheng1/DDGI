#pragma once

#include "camera.hpp"
#include "ddgi/DDGIVolume.h"
#include "scene/Scene.h"

namespace renderer {

class Renderer {
private:
    vkm::VKMDevice* device{nullptr};
    vk::PipelineLayout forwardPipelineLayout{VK_NULL_HANDLE};
    vk::Pipeline forwardPipeline{VK_NULL_HANDLE};

public:
    /**
     * Create the fallback forward scene pipeline used before the full GBuffer
     * renderer is available. The glTF material descriptor layout must already
     * exist, so load the scene before calling this function.
     */
    void create(vkm::VKMDevice* device, vk::PipelineCache pipelineCache, vk::RenderPass renderPass);

    /**
     * Destroy graphics pipeline state owned by the renderer.
     */
    void destroy();

    /**
     * Draw the loaded glTF scene with a textured forward pass.
     * camera provides the per-frame view-projection matrix as a push constant.
     */
    void drawScene(vk::CommandBuffer commandBuffer,
                   scene::Scene& scene,
                   const Camera& camera,
                   vk::Extent2D framebufferExtent,
                   const ddgi::DDGIVolume* volume);
};

} // namespace renderer
