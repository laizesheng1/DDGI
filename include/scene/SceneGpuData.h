#pragma once

#include <vector>

#include <glm/vec3.hpp>

#include "Buffer.h"
#include "VKMDevice.h"
#include "glTFModel.h"
#include "scene/Scene.h"

namespace scene {

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
    std::vector<glm::vec3> compactPositions{};
    std::vector<uint32_t> compactIndices{};
    vk::DescriptorBufferInfo positionBufferInfo{};
    vk::DescriptorBufferInfo indexBufferInfo{};
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
    [[nodiscard]] const vkm::Buffer& vertexBuffer() const { return rtPositionBuffer; }
    [[nodiscard]] const vkm::Buffer& indexBuffer() const { return rtIndexBuffer; }
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
