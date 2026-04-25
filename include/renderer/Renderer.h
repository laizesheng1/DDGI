#pragma once

#include "camera.hpp"
#include "ddgi/DDGIVolume.h"
#include "renderer/GBufferPass.h"
#include "renderer/LightingPass.h"
#include "scene/Scene.h"

namespace renderer {

class Renderer {
private:
    vkm::VKMDevice* device{nullptr};
    GBufferPass gbufferPass{};
    LightingPass lightingPass{};
    vk::PipelineLayout forwardPipelineLayout{VK_NULL_HANDLE};
    vk::Pipeline forwardPipeline{VK_NULL_HANDLE};
    vk::Extent2D framebufferExtent{};

public:
    /**
     * Create renderer-owned passes. The deferred path uses:
     * 1) GBufferPass into offscreen attachments
     * 2) LightingPass into the swapchain render pass
     * The forward pipeline stays available as a bring-up fallback.
     */
    void create(vkm::VKMDevice* device,
                vk::PipelineCache pipelineCache,
                vk::RenderPass renderPass,
                vk::Format depthFormat,
                vk::Extent2D framebufferExtent,
                vk::DescriptorSetLayout ddgiSetLayout);

    /**
     * Destroy deferred and forward renderer state in reverse dependency order.
     */
    void destroy();

    /**
     * Record the offscreen geometry pass before the swapchain render pass
     * begins. The deferred lighting pass later consumes the produced textures.
     */
    void recordGBuffer(vk::CommandBuffer commandBuffer,
                       scene::Scene& scene,
                       const Camera& camera,
                       vk::Extent2D framebufferExtent);

    /**
     * Draw the scene result into the currently active swapchain render pass.
     * When deferred resources are ready, this executes fullscreen lighting with
     * DDGI. Otherwise it falls back to the textured forward scene pass.
     */
    void drawScene(vk::CommandBuffer commandBuffer,
                   scene::Scene& scene,
                   const Camera& camera,
                   vk::Extent2D framebufferExtent,
                   const ddgi::DDGIVolume* volume,
                   bool enableDdgi);
};

} // namespace renderer
