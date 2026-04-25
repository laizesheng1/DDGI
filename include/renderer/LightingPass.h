#pragma once

#include <array>

#include "camera.hpp"
#include "ddgi/DDGIVolume.h"
#include "renderer/GBufferPass.h"

namespace renderer {

class LightingPass {
private:
    vkm::VKMDevice* device{nullptr};
    vk::DescriptorPool descriptorPoolHandle{VK_NULL_HANDLE};
    vk::DescriptorSetLayout descriptorSetLayoutHandle{VK_NULL_HANDLE};
    vk::DescriptorSet descriptorSetHandle{VK_NULL_HANDLE};
    vk::PipelineLayout pipelineLayoutHandle{VK_NULL_HANDLE};
    vk::Pipeline pipelineHandle{VK_NULL_HANDLE};
    std::array<vk::ImageView, 5> cachedImageViews{};

public:
    /**
     * Create the fullscreen deferred-lighting pipeline. The pipeline layout
     * expects set 0 = GBuffer textures and set 1 = DDGI resources.
     */
    void create(vkm::VKMDevice* device,
                vk::PipelineCache pipelineCache,
                vk::RenderPass swapchainRenderPass,
                vk::DescriptorSetLayout ddgiSetLayout);

    /**
     * Destroy descriptors before the pipeline layout that references them.
     */
    void destroy();

    /**
     * Draw the fullscreen lighting pass using GBuffer inputs plus DDGI atlases.
     * enableDdgi toggles indirect diffuse contribution without falling back to
     * the legacy forward path, which makes A/B comparison easier in one frame.
     */
    void record(vk::CommandBuffer commandBuffer,
                const GBufferPass& gbufferPass,
                const Camera& camera,
                const ddgi::DDGIVolume& volume,
                vk::Extent2D framebufferExtent,
                bool enableDdgi);

    [[nodiscard]] bool isCreated() const { return pipelineHandle != VK_NULL_HANDLE && pipelineLayoutHandle != VK_NULL_HANDLE; }
};

} // namespace renderer
