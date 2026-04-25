#pragma once

#include "Texture.h"
#include "camera.hpp"
#include "scene/Scene.h"

namespace renderer {

class GBufferPass {
private:
    vkm::VKMDevice* device{nullptr};
    vk::RenderPass renderPassHandle{VK_NULL_HANDLE};
    vk::Framebuffer framebufferHandle{VK_NULL_HANDLE};
    vk::PipelineLayout pipelineLayoutHandle{VK_NULL_HANDLE};
    vk::Pipeline pipelineHandle{VK_NULL_HANDLE};
    vk::Extent2D framebufferExtent{};
    vk::Format depthFormat{vk::Format::eUndefined};
    vk::ImageAspectFlags depthBarrierAspectMask{vk::ImageAspectFlagBits::eDepth};
    vkm::Texture2D worldPositionTexture{};
    vkm::Texture2D normalTexture{};
    vkm::Texture2D albedoTexture{};
    vkm::Texture2D materialTexture{};
    vkm::Texture2D depthTexture{};

public:
    /**
     * Create the offscreen GBuffer render pass, attachments, and graphics
     * pipeline. swapchain-sized resources are recreated when the extent changes.
     */
    void create(vkm::VKMDevice* device,
                vk::PipelineCache pipelineCache,
                vk::Extent2D framebufferExtent,
                vk::Format depthFormat);

    /**
     * Release offscreen attachments before the pass objects that reference them.
     */
    void destroy();

    /**
     * Record the geometry pass into offscreen GBuffer attachments.
     * camera provides the world->clip matrix used by the vertex shader.
     */
    void record(vk::CommandBuffer commandBuffer,
                scene::Scene& scene,
                const Camera& camera,
                vk::Extent2D framebufferExtent);

    [[nodiscard]] bool isCreated() const { return renderPassHandle != VK_NULL_HANDLE && framebufferHandle != VK_NULL_HANDLE; }
    [[nodiscard]] vk::RenderPass renderPass() const { return renderPassHandle; }
    [[nodiscard]] const vkm::Texture2D& worldPosition() const { return worldPositionTexture; }
    [[nodiscard]] const vkm::Texture2D& normal() const { return normalTexture; }
    [[nodiscard]] const vkm::Texture2D& albedo() const { return albedoTexture; }
    [[nodiscard]] const vkm::Texture2D& material() const { return materialTexture; }
    [[nodiscard]] const vkm::Texture2D& depth() const { return depthTexture; }
};

} // namespace renderer
