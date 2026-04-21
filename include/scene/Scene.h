#pragma once

#include <string>
#include <vector>

#include "glTFModel.h"

namespace scene {

struct SceneMesh {
    uint32_t firstIndex{0u};
    uint32_t indexCount{0u};
    uint32_t firstVertex{0u};
    uint32_t vertexCount{0u};
};

class Scene {
public:
    void loadFromFile(vkm::VKMDevice* device, const std::string& filename, vk::Queue transferQueue);
    void clear();

    [[nodiscard]] const vkmglTF::Model& model() const { return gltfModel; }
    [[nodiscard]] const std::vector<SceneMesh>& meshes() const { return meshList; }

private:
    vkmglTF::Model gltfModel{};
    std::vector<SceneMesh> meshList;
};

} // namespace scene
