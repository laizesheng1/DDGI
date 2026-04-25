#pragma once

#include <array>

#include "ddgi/DDGITypes.h"
#include "ddgi/DDGIVolume.h"

namespace debug {

class TextureVisualizer {
private:
    vkm::VKMDevice* device{nullptr};
    vk::DescriptorPool descriptorPoolHandle{VK_NULL_HANDLE};
    vk::DescriptorSetLayout descriptorSetLayoutHandle{VK_NULL_HANDLE};
    std::array<vk::DescriptorSet, 3> descriptorSets{};
    std::array<vk::ImageView, 3> cachedImageViews{};
    vk::PipelineLayout pipelineLayoutHandle{VK_NULL_HANDLE};
    vk::Pipeline pipelineHandle{VK_NULL_HANDLE};

public:
    /**
     * Create a simple screen-space panel pipeline for showing DDGI atlases in
     * the swapchain render pass.
     */
    void create(vkm::VKMDevice* device, vk::RenderPass renderPass, vk::PipelineCache pipelineCache);

    /**
     * Destroy per-texture descriptor sets before the descriptor pool and layout.
     */
    void destroy();

    /**
     * Draw the selected atlas into a fixed overlay panel. The panel reuses one
     * immutable descriptor set per atlas so toggling views does not rewrite
     * descriptors every frame.
     */
    void draw(vk::CommandBuffer commandBuffer,
              const ddgi::DDGIVolume& volume,
              ddgi::DebugTexture texture,
              vk::Extent2D framebufferExtent);
};

} // namespace debug
