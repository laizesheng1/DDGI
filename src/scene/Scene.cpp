#include "scene/Scene.h"

#include <algorithm>
#include <array>
#include <cfloat>

namespace scene {
namespace {

constexpr bool kSceneUsesFlipY = true;

glm::vec3 transformPoint(const glm::mat4& transform, const glm::vec3& point)
{
    const glm::vec4 transformed = transform * glm::vec4(point, 1.0f);
    return glm::vec3(transformed) / std::max(transformed.w, 1.0e-6f);
}

void applyFlipYToBounds(glm::vec3& minBounds, glm::vec3& maxBounds)
{
    const float flippedMinY = -maxBounds.y;
    const float flippedMaxY = -minBounds.y;
    minBounds.y = flippedMinY;
    maxBounds.y = flippedMaxY;
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
            glm::vec3 localMin = primitive->dimensions.min;
            glm::vec3 localMax = primitive->dimensions.max;
            if (kSceneUsesFlipY) {
                // The project loads glTF with FlipY enabled. Primitive
                // dimensions stored in vulkan_base come from the original
                // accessor range, so we mirror the local Y bounds here before
                // mapping them into world space. Without this, the derived
                // scene AABB ends up mirrored vertically and DDGI probes can be
                // initialized below the visible floor.
                applyFlipYToBounds(localMin, localMax);
            }
            calculateWorldBounds(worldFromLocal, localMin, localMax, worldMin, worldMax);

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
    // The existing sample shaders and camera expect the same glTF load flags
    // that the rest of the project already uses, so the stable scene AABB is
    // taken from the loader after those transforms have been applied.
    const uint32_t gltfLoadingFlags = vkmglTF::FileLoadingFlags::FlipY | vkmglTF::FileLoadingFlags::PreTransformVertices;
    gltfModel.loadFromFile(filename, transferQueue, gltfLoadingFlags);

    meshList.clear();
    meshList.reserve(gltfModel.linearNodes.size());
    for (vkmglTF::Node* node : gltfModel.linearNodes) {
        if (node != nullptr) {
            appendNodeMeshes(gltfModel, *node, meshList);
        }
    }

    bounds = {};
    if (!meshList.empty()) {
        bounds.min = glm::vec3(FLT_MAX);
        bounds.max = glm::vec3(-FLT_MAX);
        for (const SceneMesh& mesh : meshList) {
            bounds.min = glm::min(bounds.min, mesh.minBounds);
            bounds.max = glm::max(bounds.max, mesh.maxBounds);
        }
        bounds.center = (bounds.min + bounds.max) * 0.5f;
        bounds.extent = bounds.max - bounds.min;
        bounds.radius = glm::distance(bounds.min, bounds.max) * 0.5f;
        bounds.valid = true;
    }

    OutputMessage(
        "[Scene] Bounds min=({:.2f}, {:.2f}, {:.2f}) max=({:.2f}, {:.2f}, {:.2f}) center=({:.2f}, {:.2f}, {:.2f}) radius={:.2f}\n",
        bounds.min.x,
        bounds.min.y,
        bounds.min.z,
        bounds.max.x,
        bounds.max.y,
        bounds.max.z,
        bounds.center.x,
        bounds.center.y,
        bounds.center.z,
        bounds.radius);
}

void Scene::clear()
{
    meshList.clear();
    loadedFilePath.clear();
    bounds = {};
}

} // namespace scene
