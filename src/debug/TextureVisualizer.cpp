#include "debug/TextureVisualizer.h"

namespace debug {

void TextureVisualizer::create(vkm::VKMDevice* inDevice, vk::RenderPass, vk::PipelineCache)
{
    device = inDevice;
}

void TextureVisualizer::destroy()
{
    device = nullptr;
}

void TextureVisualizer::draw(vk::CommandBuffer, const ddgi::DDGIVolume&, ddgi::DebugTexture)
{
    // TODO: Draw irradiance, depth, or depth-squared atlas as a screen-space panel.
}

} // namespace debug
