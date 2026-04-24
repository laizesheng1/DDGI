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
    uint32_t materialIndex{0u};
    glm::vec3 minBounds{0.0f};
    glm::vec3 maxBounds{0.0f};
    glm::vec3 centroid{0.0f};
};

class Scene {
private:
    vkmglTF::Model gltfModel{};
    std::vector<SceneMesh> meshList;
    std::string loadedFilePath{};

public:
    /**
     * Load a glTF scene and flatten its render primitives into SceneMesh entries.
     * device and transferQueue are forwarded to the existing glTF loader and
     * must remain valid while GPU upload commands execute.
     */
    void loadFromFile(vkm::VKMDevice* device, const std::string& filename, vk::Queue transferQueue);

    /**
     * Clear flattened scene metadata. The glTF model lifetime remains owned by
     * this Scene instance and is released by its destructor.
     */
    void clear();

    [[nodiscard]] vkmglTF::Model& model() { return gltfModel; }
    [[nodiscard]] const vkmglTF::Model& model() const { return gltfModel; }
    [[nodiscard]] const std::vector<SceneMesh>& meshes() const { return meshList; }
    /**
     * Return the glTF file path used to populate this Scene.
     * SceneGpuData reuses it to rebuild CPU-side compact geometry for RT.
     */
    [[nodiscard]] const std::string& sourcePath() const { return loadedFilePath; }

};

} // namespace scene
