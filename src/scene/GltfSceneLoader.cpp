#include "scene/GltfSceneLoader.h"

namespace scene {

Scene GltfSceneLoader::load(vkm::VKMDevice* device, const std::string& filename, vk::Queue transferQueue) const
{
    Scene output;
    output.loadFromFile(device, filename, transferQueue);
    return output;
}

} // namespace scene
