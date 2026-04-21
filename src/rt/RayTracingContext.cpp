#include "rt/RayTracingContext.h"

namespace rt {

void RayTracingContext::create(vkm::VKMDevice* inDevice)
{
    device = inDevice;
    requiredExtensions = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
    };
}

void RayTracingContext::destroy()
{
    currentScene = nullptr;
    device = nullptr;
}

void RayTracingContext::buildScene(const scene::Scene& inScene)
{
    currentScene = &inScene;
    // TODO: Build BLAS per mesh and one TLAS over scene instances.
}

RayTracingScene RayTracingContext::sceneBinding() const
{
    return RayTracingScene{
        .scene = currentScene,
        .topLevelAccelerationStructure = VK_NULL_HANDLE
    };
}

const std::vector<const char*>& RayTracingContext::requiredDeviceExtensions() const
{
    return requiredExtensions;
}

} // namespace rt
