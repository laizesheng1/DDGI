#pragma once

#include "Buffer.h"
#include "VKMDevice.h"
#include "scene/Scene.h"

namespace scene {

class SceneGpuData {
public:
    void create(vkm::VKMDevice* device, const Scene& scene);
    void destroy();

private:
    vkm::VKMDevice* device{nullptr};
    vkm::Buffer vertices{};
    vkm::Buffer indices{};
    vkm::Buffer materials{};
};

} // namespace scene
