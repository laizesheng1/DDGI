#include "rt/RayTracingContext.h"

#include <algorithm>

namespace rt {
namespace {

bool hasExtension(const std::vector<std::string>& supportedExtensions, const char* requiredExtension)
{
    return std::find(supportedExtensions.begin(), supportedExtensions.end(), requiredExtension) != supportedExtensions.end();
}

} // namespace

const std::vector<const char*>& RayTracingContext::requiredExtensionNames()
{
    static const std::vector<const char*> requiredExtensions{
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };
    return requiredExtensions;
}

RayTracingSupport RayTracingContext::querySupport(vk::PhysicalDevice physicalDevice,
                                                  const std::vector<std::string>& supportedExtensions)
{
    RayTracingSupport result{};
    if (!physicalDevice) {
        result.reason = "physicalDevice is null";
        return result;
    }

    for (const char* requiredExtension : requiredExtensionNames()) {
        if (!hasExtension(supportedExtensions, requiredExtension)) {
            result.reason = std::string("missing device extension ") + requiredExtension;
            return result;
        }
    }

    vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;
    rayTracingPipelineFeatures.pNext = &bufferDeviceAddressFeatures;

    vk::PhysicalDeviceFeatures2 features2{};
    features2.pNext = &accelerationStructureFeatures;
    physicalDevice.getFeatures2(&features2);

    result.accelerationStructureFeatures = accelerationStructureFeatures;
    result.rayTracingPipelineFeatures = rayTracingPipelineFeatures;
    result.bufferDeviceAddressFeatures = bufferDeviceAddressFeatures;
    result.accelerationStructureFeatures.pNext = nullptr;
    result.rayTracingPipelineFeatures.pNext = nullptr;
    result.bufferDeviceAddressFeatures.pNext = nullptr;

    if (!result.accelerationStructureFeatures.accelerationStructure) {
        result.reason = "VK_KHR_acceleration_structure feature is not supported";
        return result;
    }
    if (!result.rayTracingPipelineFeatures.rayTracingPipeline) {
        result.reason = "VK_KHR_ray_tracing_pipeline feature is not supported";
        return result;
    }
    if (!result.bufferDeviceAddressFeatures.bufferDeviceAddress) {
        result.reason = "bufferDeviceAddress feature is not supported";
        return result;
    }

    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
    vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{};
    rayTracingPipelineProperties.pNext = &accelerationStructureProperties;

    vk::PhysicalDeviceProperties2 properties2{};
    properties2.pNext = &rayTracingPipelineProperties;
    physicalDevice.getProperties2(&properties2);

    result.rayTracingPipelineProperties = rayTracingPipelineProperties;
    result.accelerationStructureProperties = accelerationStructureProperties;
    result.rayTracingPipelineProperties.pNext = nullptr;
    result.accelerationStructureProperties.pNext = nullptr;

    result.supported = true;
    result.reason = "supported";
    return result;
}

void RayTracingContext::create(vkm::VKMDevice* inDevice)
{
    if (inDevice == nullptr) {
        OutputMessage("[RT] RayTracingContext::create received a null device\n");
        return;
    }

    device = inDevice;
    requiredExtensions = requiredExtensionNames();
    support = querySupport(device->physicalDevice, device->supportedExtensions);
    accelerationStructures.create(device);
    if (!support.supported) {
        OutputMessage("[RT] Ray tracing unavailable: {}\n", support.reason);
        return;
    }

    // KHR ray tracing entry points are device-level commands. Initializing the
    // default dispatcher here keeps the RT module self-contained and avoids
    // adding DDGI-specific initialization to vulkan_base.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device->logicalDevice);
    OutputMessage("[RT] Ray tracing supported. SBT handle size={} alignment={}\n",
                  support.rayTracingPipelineProperties.shaderGroupHandleSize,
                  support.rayTracingPipelineProperties.shaderGroupBaseAlignment);
}

void RayTracingContext::destroy()
{
    accelerationStructures.destroy();
    sceneGpuData.destroy();
    currentScene = nullptr;
    softwareBvh.clear();
    support = {};
    device = nullptr;
}

void RayTracingContext::buildScene(const scene::Scene& inScene, vk::Queue transferQueue)
{
    currentScene = &inScene;
    softwareBvh.build(inScene);
    if (!support.supported) {
        return;
    }

    sceneGpuData.create(device, inScene, transferQueue);
    if (!sceneGpuData.isCreated()) {
        return;
    }

    accelerationStructures.buildScene(sceneGpuData, transferQueue);
}

RayTracingScene RayTracingContext::sceneBinding() const
{
    return RayTracingScene{
        .scene = currentScene,
        .gpuData = sceneGpuData.isCreated() ? &sceneGpuData : nullptr,
        .topLevelAccelerationStructure = accelerationStructures.hasTopLevel()
            ? accelerationStructures.topLevel().handle
            : VK_NULL_HANDLE
    };
}

const std::vector<const char*>& RayTracingContext::requiredDeviceExtensions() const
{
    return requiredExtensions;
}

} // namespace rt
