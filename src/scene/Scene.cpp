#include "scene/Scene.h"

#include <algorithm>
#include <array>
#include <cfloat>

namespace scene {
namespace {

glm::vec3 transformPoint(const glm::mat4& transform, const glm::vec3& point)
{
    const glm::vec4 transformed = transform * glm::vec4(point, 1.0f);
    return glm::vec3(transformed) / std::max(transformed.w, 1.0e-6f);
}

void calculateWorldBounds(const glm::mat4& worldFromLocal,
                          const glm::vec3& localMin,
                          const glm::vec3& localMax,
                          glm::vec3& worldMin,
                          glm::vec3& worldMax)
{
    const std::array<glm::vec3, 8> corners{
        glm::vec3{localMin.x, localMin.y, localMin.z},
        glm::vec3{localMax.x, localMin.y, localMin.z},
        glm::vec3{localMin.x, localMax.y, localMin.z},
        glm::vec3{localMax.x, localMax.y, localMin.z},
        glm::vec3{localMin.x, localMin.y, localMax.z},
        glm::vec3{localMax.x, localMin.y, localMax.z},
        glm::vec3{localMin.x, localMax.y, localMax.z},
        glm::vec3{localMax.x, localMax.y, localMax.z},
    };

    worldMin = glm::vec3(FLT_MAX);
    worldMax = glm::vec3(-FLT_MAX);
    for (const glm::vec3& corner : corners) {
        const glm::vec3 worldCorner = transformPoint(worldFromLocal, corner);
        worldMin = glm::min(worldMin, worldCorner);
        worldMax = glm::max(worldMax, worldCorner);
    }
}

uint32_t materialIndexOf(const vkmglTF::Model& model, const vkmglTF::Material& material)
{
    for (uint32_t materialIndex = 0; materialIndex < static_cast<uint32_t>(model.materials.size()); ++materialIndex) {
        if (&model.materials[materialIndex] == &material) {
            return materialIndex;
        }
    }
    return 0u;
}

void appendNodeMeshes(const vkmglTF::Model& model, vkmglTF::Node& node, std::vector<SceneMesh>& meshes)
{
    if (node.mesh != nullptr) {
        const glm::mat4 worldFromLocal = node.getMatrix();
        for (const vkmglTF::Primitive* primitive : node.mesh->primitives) {
            if (primitive == nullptr || primitive->indexCount == 0u || primitive->vertexCount == 0u) {
                continue;
            }

            glm::vec3 worldMin{};
            glm::vec3 worldMax{};
            calculateWorldBounds(worldFromLocal, primitive->dimensions.min, primitive->dimensions.max, worldMin, worldMax);

            SceneMesh mesh{};
            mesh.firstIndex = primitive->firstIndex;
            mesh.indexCount = primitive->indexCount;
            mesh.firstVertex = primitive->firstVertex;
            mesh.vertexCount = primitive->vertexCount;
            mesh.materialIndex = materialIndexOf(model, primitive->material);
            mesh.minBounds = worldMin;
            mesh.maxBounds = worldMax;
            mesh.centroid = (worldMin + worldMax) * 0.5f;
            meshes.push_back(mesh);
        }
    }
}

} // namespace

void Scene::loadFromFile(vkm::VKMDevice* device, const std::string& filename, vk::Queue transferQueue)
{
    loadedFilePath = filename;
    gltfModel = vkmglTF::Model(device);
    // Keep glTF geometry in its native Y-up world convention. 
    const uint32_t gltfLoadingFlags = vkmglTF::FileLoadingFlags::FlipY | vkmglTF::FileLoadingFlags::PreTransformVertices;
    gltfModel.loadFromFile(filename, transferQueue, gltfLoadingFlags);

    meshList.clear();
    meshList.reserve(gltfModel.linearNodes.size());
    for (vkmglTF::Node* node : gltfModel.linearNodes) {
        if (node != nullptr) {
            appendNodeMeshes(gltfModel, *node, meshList);
        }
    }

    OutputMessage(
        "[Scene] Bounds min=({:.2f}, {:.2f}, {:.2f}) max=({:.2f}, {:.2f}, {:.2f}) center=({:.2f}, {:.2f}, {:.2f}) radius={:.2f}\n",
        gltfModel.dimensions.min.x,
        gltfModel.dimensions.min.y,
        gltfModel.dimensions.min.z,
        gltfModel.dimensions.max.x,
        gltfModel.dimensions.max.y,
        gltfModel.dimensions.max.z,
        gltfModel.dimensions.center.x,
        gltfModel.dimensions.center.y,
        gltfModel.dimensions.center.z,
        gltfModel.dimensions.radius);
}

void Scene::clear()
{
    meshList.clear();
    loadedFilePath.clear();
}

} // namespace scene
