#pragma once

#include <string>

#include "scene/Scene.h"

namespace scene {

class GltfSceneLoader {
public:
    Scene load(vkm::VKMDevice* device, const std::string& filename, vk::Queue transferQueue) const;
};

} // namespace scene
