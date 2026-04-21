#pragma once

#include "ddgi/DDGIVolume.h"
#include "scene/Scene.h"

namespace renderer {

class Renderer {
public:
    void create(vkm::VKMDevice* device, vk::PipelineCache pipelineCache, vk::RenderPass renderPass);
    void destroy();
    void drawScene(vk::CommandBuffer commandBuffer, const scene::Scene& scene, const ddgi::DDGIVolume* volume);

private:
    vkm::VKMDevice* device{nullptr};
};

} // namespace renderer
