#include "ddgi/DDGIPipeline.h"

namespace ddgi {

void DDGIPipeline::create(vkm::VKMDevice* inDevice, vk::PipelineCache)
{
    device = inDevice;
}

void DDGIPipeline::destroy()
{
    if (device == nullptr) {
        return;
    }

    auto logicalDevice = device->logicalDevice;
    if (tracePipeline) logicalDevice.destroyPipeline(tracePipeline);
    if (updateIrradiancePipeline) logicalDevice.destroyPipeline(updateIrradiancePipeline);
    if (updateDepthPipeline) logicalDevice.destroyPipeline(updateDepthPipeline);
    if (updateDepthSquaredPipeline) logicalDevice.destroyPipeline(updateDepthSquaredPipeline);
    if (pipelineLayout) logicalDevice.destroyPipelineLayout(pipelineLayout);

    tracePipeline = VK_NULL_HANDLE;
    updateIrradiancePipeline = VK_NULL_HANDLE;
    updateDepthPipeline = VK_NULL_HANDLE;
    updateDepthSquaredPipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    device = nullptr;
}

} // namespace ddgi
