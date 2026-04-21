#include "scene/Scene.h"

namespace scene {

void Scene::loadFromFile(vkm::VKMDevice* device, const std::string& filename, vk::Queue transferQueue)
{
    gltfModel = vkmglTF::Model(device);
    gltfModel.loadFromFile(filename, transferQueue, vkmglTF::FileLoadingFlags::PreTransformVertices);
    meshList.clear();
    // TODO: Flatten glTF primitives into SceneMesh entries for BVH and GPU scene buffers.
}

void Scene::clear()
{
    meshList.clear();
}

} // namespace scene
