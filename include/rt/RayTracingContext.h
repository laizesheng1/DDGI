#pragma once

#include <vector>

#include "VKMDevice.h"
#include "scene/Scene.h"
#include "vulkan/vulkan.hpp"

namespace rt {

struct RayTracingScene {
    const scene::Scene* scene{nullptr};
    vk::AccelerationStructureKHR topLevelAccelerationStructure{VK_NULL_HANDLE};
};

class RayTracingContext {
public:
    void create(vkm::VKMDevice* device);
    void destroy();
    void buildScene(const scene::Scene& scene);

    [[nodiscard]] RayTracingScene sceneBinding() const;
    [[nodiscard]] const std::vector<const char*>& requiredDeviceExtensions() const;

private:
    vkm::VKMDevice* device{nullptr};
    const scene::Scene* currentScene{nullptr};
    std::vector<const char*> requiredExtensions{};
};

} // namespace rt
