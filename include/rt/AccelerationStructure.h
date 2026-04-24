#pragma once

#include <vector>

#include "Buffer.h"
#include "VKMDevice.h"
#include "scene/SceneGpuData.h"
#include "vulkan/vulkan.hpp"

namespace rt {

struct AccelerationStructure {
    vk::AccelerationStructureKHR handle{VK_NULL_HANDLE};
    vkm::Buffer buffer{};
    vk::DeviceAddress deviceAddress{0};
    vk::AccelerationStructureTypeKHR type{vk::AccelerationStructureTypeKHR::eTopLevel};
};

class AccelerationStructureBuilder {
private:
    vkm::VKMDevice* device{nullptr};
    std::vector<AccelerationStructure> bottomLevels{};
    AccelerationStructure tlas{};

public:
    /**
     * Cache the logical device used for later BLAS/TLAS allocation.
     */
    void create(vkm::VKMDevice* device);

    /**
     * Destroy TLAS, BLAS buffers, and cached device addresses.
     */
    void destroy();

    /**
     * Release the currently built scene acceleration structures.
     */
    void resetScene();

    /**
     * Build one BLAS per compact SceneMesh and an identity-transform TLAS.
     * sceneGpuData supplies RT vertex/index device addresses and primitive
     * ranges. transferQueue submits the one-shot AS build command buffer.
     */
    void buildScene(const scene::SceneGpuData& sceneGpuData, vk::Queue transferQueue);

    [[nodiscard]] bool hasTopLevel() const { return tlas.handle != VK_NULL_HANDLE; }
    [[nodiscard]] const AccelerationStructure& topLevel() const { return tlas; }
    [[nodiscard]] const std::vector<AccelerationStructure>& bottomLevelStructures() const { return bottomLevels; }
};

} // namespace rt
