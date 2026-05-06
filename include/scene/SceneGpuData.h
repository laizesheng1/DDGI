#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Buffer.h"
#include "VKMDevice.h"
#include "glTFModel.h"
#include "scene/Scene.h"

namespace scene {

/**
 * Per-vertex attributes consumed by the DDGI closest-hit shader.
 * Positions stay in a separate vec3 buffer because the BLAS build path already
 * uses that tightly packed layout; this buffer only carries shading data.
 */
struct SceneRtVertexAttributeGpuData {
    glm::vec4 normalAndMaterial{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 uvAndFlags{0.0f};
};

/**
 * Compact mesh range used by hit shaders to map instance/primitive ids back to
 * the global compact index and vertex-attribute buffers.
 */
struct SceneRtMeshGpuData {
    glm::uvec4 firstIndexFirstVertexMaterialFlags{0u};
};

/**
 * Minimal material record for DDGI tracing. Texture sampling is deliberately
 * postponed until bindless/material descriptor plumbing exists, but base color
 * and emissive factors already make probe radiance match scene authoring much
 * better than a fixed debug color.
 */
struct SceneRtMaterialGpuData {
    glm::vec4 baseColorAndAlphaCutoff{1.0f, 1.0f, 1.0f, 0.5f};
    glm::vec4 emissiveAndFlags{0.0f};
};

/**
 * Compact GPU geometry buffers used by the Vulkan ray tracing path.
 * The buffers are rebuilt inside the scene module so they can carry the usage
 * flags required for device-address queries and acceleration-structure builds
 * without modifying vulkan_base's glTF loader.
 */
class SceneGpuData {
private:
    vkm::VKMDevice* device{nullptr};
    vkm::Buffer rtPositionBuffer{};
    vkm::Buffer rtIndexBuffer{};
    vkm::Buffer rtVertexAttributeBuffer{};
    vkm::Buffer rtMeshBuffer{};
    vkm::Buffer rtMaterialBuffer{};
    std::vector<glm::vec3> compactPositions{};
    std::vector<uint32_t> compactIndices{};
    std::vector<SceneRtVertexAttributeGpuData> compactVertexAttributes{};
    std::vector<SceneRtMeshGpuData> compactMeshGpuData{};
    std::vector<SceneRtMaterialGpuData> compactMaterialGpuData{};
    vk::DescriptorBufferInfo positionBufferInfo{};
    vk::DescriptorBufferInfo indexBufferInfo{};
    vk::DescriptorBufferInfo vertexAttributeBufferInfo{};
    vk::DescriptorBufferInfo meshBufferInfo{};
    vk::DescriptorBufferInfo materialBufferInfo{};
    vk::DeviceAddress positionBufferDeviceAddress{0};
    vk::DeviceAddress indexBufferDeviceAddress{0};
    uint32_t vertexCount{0u};
    uint32_t indexCount{0u};
    bool created{false};
    std::vector<SceneMesh> compactMeshes{};

public:
    /**
     * Rebuild CPU-side glTF geometry into RT-compatible GPU buffers.
     * scene.sourcePath() is reparsed so the resulting buffers can expose device
     * addresses and acceleration-structure build input usage.
     * transferQueue submits the staging-buffer copy into the final device-local
     * RT vertex/index buffers.
     */
    void create(vkm::VKMDevice* device, const Scene& scene, vk::Queue transferQueue);

    /**
     * Release RT-only geometry buffers and cached mesh ranges.
     */
    void destroy();

    [[nodiscard]] bool isCreated() const { return created; }
    [[nodiscard]] vk::DescriptorBufferInfo vertexDescriptor() const { return positionBufferInfo; }
    [[nodiscard]] vk::DescriptorBufferInfo indexDescriptor() const { return indexBufferInfo; }
    [[nodiscard]] vk::DescriptorBufferInfo vertexAttributeDescriptor() const { return vertexAttributeBufferInfo; }
    [[nodiscard]] vk::DescriptorBufferInfo meshDescriptor() const { return meshBufferInfo; }
    [[nodiscard]] vk::DescriptorBufferInfo materialDescriptor() const { return materialBufferInfo; }
    [[nodiscard]] const vkm::Buffer& vertexBuffer() const { return rtPositionBuffer; }
    [[nodiscard]] const vkm::Buffer& indexBuffer() const { return rtIndexBuffer; }
    [[nodiscard]] const vkm::Buffer& vertexAttributeBuffer() const { return rtVertexAttributeBuffer; }
    [[nodiscard]] const vkm::Buffer& meshBuffer() const { return rtMeshBuffer; }
    [[nodiscard]] const vkm::Buffer& materialBuffer() const { return rtMaterialBuffer; }
    [[nodiscard]] vk::DeviceAddress vertexAddress() const { return positionBufferDeviceAddress; }
    [[nodiscard]] vk::DeviceAddress indexAddress() const { return indexBufferDeviceAddress; }
    [[nodiscard]] uint32_t verticesCount() const { return vertexCount; }
    [[nodiscard]] uint32_t indicesCount() const { return indexCount; }
    /**
     * Return compact CPU positions laid out per primitive in the same order as
     * meshes(). The RT builder can upload per-mesh scratch geometry from this
     * array when narrowing down build issues.
     */
    [[nodiscard]] const std::vector<glm::vec3>& positions() const { return compactPositions; }
    /**
     * Return compact CPU indices that match positions() and meshes().
     * Indices stay local to each primitive's vertex slice.
     */
    [[nodiscard]] const std::vector<uint32_t>& indices() const { return compactIndices; }
    /**
     * Return compact mesh ranges that match the RT vertex/index buffers.
     * Each entry maps one glTF primitive to a BLAS build range.
     */
    [[nodiscard]] const std::vector<SceneMesh>& meshes() const { return compactMeshes; }
};

} // namespace scene
