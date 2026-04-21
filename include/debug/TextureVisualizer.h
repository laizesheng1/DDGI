#pragma once

#include "ddgi/DDGITypes.h"
#include "ddgi/DDGIVolume.h"

namespace debug {

class TextureVisualizer {
public:
    void create(vkm::VKMDevice* device, vk::RenderPass renderPass, vk::PipelineCache pipelineCache);
    void destroy();
    void draw(vk::CommandBuffer commandBuffer, const ddgi::DDGIVolume& volume, ddgi::DebugTexture texture);

private:
    vkm::VKMDevice* device{nullptr};
};

} // namespace debug
