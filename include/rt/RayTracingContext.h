#pragma once

#include <string>
#include <vector>

#include "VKMDevice.h"
#include "rt/AccelerationStructure.h"
#include "rt/SoftwareBvh.h"
#include "scene/Scene.h"
#include "scene/SceneGpuData.h"
#include "vulkan/vulkan.hpp"

namespace rt {

struct RayTracingSupport {
    bool supported{false};
    std::string reason{};
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
    vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{};
    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
};

/**
 * Scene handles needed by DDGI ray tracing.
 * scene always points at the currently loaded Scene. TLAS becomes non-null once
 * SceneGpuData and AccelerationStructureBuilder finish a successful build.
 */
struct RayTracingScene {
    const scene::Scene* scene{nullptr};
    const scene::SceneGpuData* gpuData{nullptr};
    vk::AccelerationStructureKHR topLevelAccelerationStructure{VK_NULL_HANDLE};
};

class RayTracingContext {
private:
    vkm::VKMDevice* device{nullptr};
    const scene::Scene* currentScene{nullptr};
    std::vector<const char*> requiredExtensions{};
    RayTracingSupport support{};
    SoftwareBvh softwareBvh{};
    scene::SceneGpuData sceneGpuData{};
    AccelerationStructureBuilder accelerationStructures{};

public:
    /**
     * Return device extensions required by the Vulkan KHR ray tracing path.
     */
    static const std::vector<const char*>& requiredExtensionNames();

    /**
     * Check extension, feature, and property support on a physical device.
     * supportedExtensions should come from VKMDevice::supportedExtensions.
     */
    static RayTracingSupport querySupport(vk::PhysicalDevice physicalDevice, const std::vector<std::string>& supportedExtensions);

    /**
     * Cache ray tracing support data for the selected logical device.
     */
    void create(vkm::VKMDevice* device);

    /**
     * Release cached scene references, RT geometry buffers, and BLAS/TLAS.
     */
    void destroy();

    /**
     * Attach a loaded scene, upload RT-compatible compact buffers, and build
     * BLAS/TLAS on transferQueue when the device supports KHR ray tracing.
     * transferQueue is reused for staging uploads and one-shot AS builds so the
     * resulting TLAS is ready before frame recording begins.
     */
    void buildScene(const scene::Scene& scene, vk::Queue transferQueue);

    [[nodiscard]] bool isSupported() const { return support.supported; }
    [[nodiscard]] const RayTracingSupport& deviceSupport() const { return support; }
    [[nodiscard]] const SoftwareBvh& cpuBvh() const { return softwareBvh; }
    [[nodiscard]] const scene::SceneGpuData& gpuSceneData() const { return sceneGpuData; }
    [[nodiscard]] RayTracingScene sceneBinding() const;
    [[nodiscard]] const std::vector<const char*>& requiredDeviceExtensions() const;
};

} // namespace rt
