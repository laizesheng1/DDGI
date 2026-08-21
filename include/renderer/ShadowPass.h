#pragma once

#include "Texture.h"
#include "scene/Scene.h"
#include "scene/SceneGpuData.h"

namespace renderer {

struct ShadowMapData {
    glm::mat4 lightViewProjection{1.0f};
    glm::vec4 params{0.0f}; // x enabled, y texel size, z normal bias, w depth bias
};

class ShadowPass {
private:
    vkm::VKMDevice* device{nullptr};
    vk::RenderPass renderPassHandle{VK_NULL_HANDLE};
    vk::Framebuffer framebufferHandle{VK_NULL_HANDLE};
    vk::PipelineLayout pipelineLayoutHandle{VK_NULL_HANDLE};
    vk::Pipeline pipelineHandle{VK_NULL_HANDLE};
    vk::Extent2D shadowExtent{2048u, 2048u};
    vk::Format depthFormat{vk::Format::eD32Sfloat};
    vkm::Texture2D depthTexture{};
    ShadowMapData shadowData{};

public:
    void create(vkm::VKMDevice* device, vk::PipelineCache pipelineCache);
    void destroy();

    void record(vk::CommandBuffer commandBuffer,
                scene::Scene& scene,
                const scene::SceneGpuData& sceneGpuData);

    [[nodiscard]] bool isCreated() const { return pipelineHandle != VK_NULL_HANDLE && framebufferHandle != VK_NULL_HANDLE; }
    [[nodiscard]] const vkm::Texture2D& depth() const { return depthTexture; }
    [[nodiscard]] const ShadowMapData& data() const { return shadowData; }
};

} // namespace renderer
