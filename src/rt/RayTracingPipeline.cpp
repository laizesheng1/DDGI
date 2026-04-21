#include "rt/RayTracingPipeline.h"

namespace rt {

void RayTracingPipeline::create(vkm::VKMDevice* inDevice, vk::PipelineCache)
{
    device = inDevice;
}

void RayTracingPipeline::destroy()
{
    if (device == nullptr) {
        return;
    }

    auto logicalDevice = device->logicalDevice;
    if (rtPipeline) logicalDevice.destroyPipeline(rtPipeline);
    if (pipelineLayout) logicalDevice.destroyPipelineLayout(pipelineLayout);
    rtPipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    device = nullptr;
}

} // namespace rt
