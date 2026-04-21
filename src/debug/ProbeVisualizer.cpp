#include "debug/ProbeVisualizer.h"

namespace debug {

void ProbeVisualizer::create(vkm::VKMDevice* inDevice, vk::RenderPass, vk::PipelineCache)
{
    device = inDevice;
}

void ProbeVisualizer::destroy()
{
    device = nullptr;
}

void ProbeVisualizer::draw(vk::CommandBuffer, const ddgi::DDGIVolume&)
{
    // TODO: Draw one debug marker per probe, colored by active/inactive state.
}

} // namespace debug
