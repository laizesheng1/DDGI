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
     * whichever debug render pass owns the final color attachment.
     */
    void create(vkm::VKMDevice* device, vk::RenderPass renderPass, vk::PipelineCache pipelineCache);

    /**
     * Destroy per-texture descriptor sets before the descriptor pool and layout.
     */
    void destroy();

    /**
     * Draw one atlas into the caller-provided viewport/scissor rectangle. The
     * visualizer only knows how to sample and shade the atlas texture; window
     * ownership, swapchain lifetime, and per-frame command submission stay in
     * the outer debug window module.
     */
    void draw(vk::CommandBuffer commandBuffer,
              const ddgi::DDGIVolume& volume,
              ddgi::DebugTexture texture,
              const vk::Viewport& viewport,
              const vk::Rect2D& scissor);
};

} // namespace debug
