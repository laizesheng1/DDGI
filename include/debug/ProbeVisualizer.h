#pragma once

#include "ddgi/DDGIVolume.h"

namespace debug {

class ProbeVisualizer {
public:
    void create(vkm::VKMDevice* device, vk::RenderPass renderPass, vk::PipelineCache pipelineCache);
    void destroy();
    void draw(vk::CommandBuffer commandBuffer, const ddgi::DDGIVolume& volume);

private:
    vkm::VKMDevice* device{nullptr};
};

} // namespace debug
