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
    glm::vec4 tangentAndSign{1.0f, 0.0f, 0.0f, 1.0f};
};

/**
 * Compact mesh range used by hit shaders to map instance/primitive ids back to
 * the global compact index and vertex-attribute buffers.
 */
struct SceneRtMeshGpuData {
    glm::uvec4 firstIndexFirstVertexMaterialFlags{0u};
};

/**
 * Material record for DDGI tracing. Textures are bound as per-material arrays
 * in the RT scene descriptor set, while this buffer stores scalar factors,
 * alpha policy, and KHR_texture_transform scale/offset values. Keeping the
 * material record separate from vulkan_base descriptors lets the DDGI hit
 * shaders evolve without changing the shared glTF renderer.
 */
struct SceneRtMaterialGpuData {
    glm::vec4 baseColorAndAlphaCutoff{1.0f, 1.0f, 1.0f, 0.5f};
    glm::vec4 emissiveAndFlags{0.0f};
    glm::vec4 metallicRoughnessAndFlags{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 baseColorTextureTransform{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 normalTextureTransform{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 metallicRoughnessTextureTransform{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 emissiveTextureTransform{1.0f, 1.0f, 0.0f, 0.0f};
};

enum SceneLightType : uint32_t {
    SceneLightDirectional = 0u,
    SceneLightPoint = 1u,
    SceneLightSpot = 2u
};

struct SceneLightGpuData {
    glm::vec4 positionAndType{0.0f, 0.0f, 0.0f, static_cast<float>(SceneLightDirectional)};
    glm::vec4 directionAndRange{0.35f, 0.85f, 0.25f, 0.0f};
    glm::vec4 colorAndIntensity{1.0f, 0.95f, 0.85f, 2.0f};
    glm::vec4 spotAngles{0.0f, -1.0f, 0.0f, 0.0f};
};

struct SceneLightingGpuData {
    glm::uvec4 lightCounts{0u};
    glm::vec4 ambientColorAndExposure{0.06f, 0.065f, 0.07f, 1.0f};
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
    vkm::Buffer lightBuffer{};
    vkm::Buffer lightingInfoBuffer{};
    vkm::Texture2D fallbackWhiteTexture{};
    vkm::Texture2D fallbackBlackTexture{};
    vkm::Texture2D fallbackNormalTexture{};
    std::vector<glm::vec3> compactPositions{};
    std::vector<uint32_t> compactIndices{};
    std::vector<SceneRtVertexAttributeGpuData> compactVertexAttributes{};
    std::vector<SceneRtMeshGpuData> compactMeshGpuData{};
    std::vector<SceneRtMaterialGpuData> compactMaterialGpuData{};
    std::vector<SceneLightGpuData> lightGpuData{};
    SceneLightingGpuData lightingGpuData{};
    std::vector<vk::DescriptorImageInfo> baseColorTextureDescriptors{};
    std::vector<vk::DescriptorImageInfo> normalTextureDescriptors{};
    std::vector<vk::DescriptorImageInfo> metallicRoughnessTextureDescriptors{};
    std::vector<vk::DescriptorImageInfo> emissiveTextureDescriptors{};
    vk::DescriptorBufferInfo positionBufferInfo{};
    vk::DescriptorBufferInfo indexBufferInfo{};
    vk::DescriptorBufferInfo vertexAttributeBufferInfo{};
    vk::DescriptorBufferInfo meshBufferInfo{};
    vk::DescriptorBufferInfo materialBufferInfo{};
    vk::DescriptorBufferInfo lightBufferInfo{};
    vk::DescriptorBufferInfo lightingInfoBufferInfo{};
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
    [[nodiscard]] vk::DescriptorBufferInfo lightDescriptor() const { return lightBufferInfo; }
    [[nodiscard]] vk::DescriptorBufferInfo lightingInfoDescriptor() const { return lightingInfoBufferInfo; }
    [[nodiscard]] const std::vector<vk::DescriptorImageInfo>& baseColorTextureDescriptorArray() const { return baseColorTextureDescriptors; }
    [[nodiscard]] const std::vector<vk::DescriptorImageInfo>& normalTextureDescriptorArray() const { return normalTextureDescriptors; }
    [[nodiscard]] const std::vector<vk::DescriptorImageInfo>& metallicRoughnessTextureDescriptorArray() const { return metallicRoughnessTextureDescriptors; }
    [[nodiscard]] const std::vector<vk::DescriptorImageInfo>& emissiveTextureDescriptorArray() const { return emissiveTextureDescriptors; }
    [[nodiscard]] const vkm::Buffer& vertexBuffer() const { return rtPositionBuffer; }
    [[nodiscard]] const vkm::Buffer& indexBuffer() const { return rtIndexBuffer; }
    [[nodiscard]] const vkm::Buffer& vertexAttributeBuffer() const { return rtVertexAttributeBuffer; }
    [[nodiscard]] const vkm::Buffer& meshBuffer() const { return rtMeshBuffer; }
    [[nodiscard]] const vkm::Buffer& materialBuffer() const { return rtMaterialBuffer; }
    [[nodiscard]] const vkm::Buffer& lightsBuffer() const { return lightBuffer; }
    [[nodiscard]] const vkm::Buffer& lightingInfoBufferObject() const { return lightingInfoBuffer; }
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
    [[nodiscard]] const std::vector<SceneLightGpuData>& lights() const { return lightGpuData; }
};

} // namespace scene
