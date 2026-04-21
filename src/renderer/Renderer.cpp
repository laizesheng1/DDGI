#include "renderer/Renderer.h"

namespace renderer {

void Renderer::create(vkm::VKMDevice* inDevice, vk::PipelineCache, vk::RenderPass)
{
    device = inDevice;
}

void Renderer::destroy()
{
    device = nullptr;
}

void Renderer::drawScene(vk::CommandBuffer, const scene::Scene&, const ddgi::DDGIVolume*)
{
    // TODO: Draw the scene and sample DDGI in the lighting pass.
}

} // namespace renderer
