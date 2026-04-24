#include "scene/SceneGpuData.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "tiny_gltf.h"

namespace scene {
namespace {

struct CompactGeometryData {
    std::vector<glm::vec3> positions{};
    std::vector<uint32_t> indices{};
    std::vector<SceneMesh> meshes{};
};

bool loadImageDataFuncEmpty(tinygltf::Image*,
                            int,
                            std::string*,
                            std::string*,
                            int,
                            int,
                            const unsigned char*,
                            int,
                            void*)
{
    return true;
}

glm::mat4 makeNodeLocalMatrix(const tinygltf::Node& node)
{
    glm::vec3 translation{0.0f};
    if (node.translation.size() == 3u) {
        translation = glm::make_vec3(node.translation.data());
    }

    glm::mat4 rotation{1.0f};
    if (node.rotation.size() == 4u) {
        rotation = glm::mat4_cast(glm::make_quat(node.rotation.data()));
        //rotation = glm::mat4(glm::make_quat(node.rotation.data()));
    }

    glm::vec3 scale{1.0f};
    if (node.scale.size() == 3u) {
        scale = glm::make_vec3(node.scale.data());
    }

    glm::mat4 matrix{1.0f};
    if (node.matrix.size() == 16u) {
        matrix = glm::make_mat4x4(node.matrix.data());
    }

    return glm::translate(glm::mat4(1.0f), translation) *
        rotation *
        glm::scale(glm::mat4(1.0f), scale) *
        matrix;
}

glm::vec2 readVec2(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t vertexIndex)
{
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    const size_t stride = accessor.ByteStride(bufferView);
    const size_t byteOffset = bufferView.byteOffset + accessor.byteOffset + stride * vertexIndex;
    glm::vec2 value{0.0f};
    std::memcpy(glm::value_ptr(value), buffer.data.data() + byteOffset, sizeof(glm::vec2));
    return value;
}

glm::vec3 readVec3(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t vertexIndex)
{
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    const size_t stride = accessor.ByteStride(bufferView);
    const size_t byteOffset = bufferView.byteOffset + accessor.byteOffset + stride * vertexIndex;
    glm::vec3 value{0.0f};
    std::memcpy(glm::value_ptr(value), buffer.data.data() + byteOffset, sizeof(glm::vec3));
    return value;
}

glm::vec4 readVec4(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t vertexIndex)
{
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    const size_t stride = accessor.ByteStride(bufferView);
    const size_t byteOffset = bufferView.byteOffset + accessor.byteOffset + stride * vertexIndex;
    glm::vec4 value{0.0f};
    std::memcpy(glm::value_ptr(value), buffer.data.data() + byteOffset, sizeof(glm::vec4));
    return value;
}

glm::vec4 readColor(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t vertexIndex)
{
    if (accessor.type == TINYGLTF_TYPE_VEC3) {
        return glm::vec4(readVec3(model, accessor, vertexIndex), 1.0f);
    }
    return readVec4(model, accessor, vertexIndex);
}

std::vector<uint32_t> readIndices(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
{
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    const unsigned char* sourceData = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

    std::vector<uint32_t> indices(accessor.count);
    switch (accessor.componentType) {
    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
        for (size_t index = 0; index < accessor.count; ++index) {
            indices[index] = reinterpret_cast<const uint32_t*>(sourceData)[index];
        }
        break;
    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
        for (size_t index = 0; index < accessor.count; ++index) {
            indices[index] = reinterpret_cast<const uint16_t*>(sourceData)[index];
        }
        break;
    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
        for (size_t index = 0; index < accessor.count; ++index) {
            indices[index] = reinterpret_cast<const uint8_t*>(sourceData)[index];
        }
        break;
    default:
        OutputMessage("[SceneGpuData] Unsupported glTF index component type {}\n", accessor.componentType);
        indices.clear();
        break;
    }
    return indices;
}

void appendNodeGeometry(const tinygltf::Model& model,
                        uint32_t nodeIndex,
                        const glm::mat4& parentWorldFromLocal,
                        CompactGeometryData& output)
{
    const tinygltf::Node& node = model.nodes[nodeIndex];
    const glm::mat4 worldFromLocal = parentWorldFromLocal * makeNodeLocalMatrix(node);

    // Match vulkan_base/glTFModel.cpp traversal so primitive ordering stays
    // consistent with Scene::meshList and future debug output.
    for (int childNodeIndex : node.children) {
        appendNodeGeometry(model, static_cast<uint32_t>(childNodeIndex), worldFromLocal, output);
    }

    if (node.mesh < 0) {
        return;
    }

    const tinygltf::Mesh& mesh = model.meshes[node.mesh];
    for (const tinygltf::Primitive& primitive : mesh.primitives) {
        if (primitive.indices < 0 || primitive.mode != TINYGLTF_MODE_TRIANGLES) {
            continue;
        }
        const auto positionIt = primitive.attributes.find("POSITION");
        if (positionIt == primitive.attributes.end()) {
            continue;
        }

        const uint32_t firstVertex = static_cast<uint32_t>(output.positions.size());
        const uint32_t firstIndex = static_cast<uint32_t>(output.indices.size());

        const tinygltf::Accessor& positionAccessor = model.accessors[positionIt->second];
        glm::vec3 worldMin{FLT_MAX};
        glm::vec3 worldMax{-FLT_MAX};
        for (size_t vertexIndex = 0; vertexIndex < positionAccessor.count; ++vertexIndex) {
            const glm::vec3 positionLocal = readVec3(model, positionAccessor, vertexIndex);
            const glm::vec3 positionWorld = glm::vec3(worldFromLocal * glm::vec4(positionLocal, 1.0f));
            worldMin = glm::min(worldMin, positionWorld);
            worldMax = glm::max(worldMax, positionWorld);
            output.positions.push_back(positionWorld);
        }

        // Keep compact RT indices local to this primitive's vertex range. BLAS
        // then uses firstVertex as the vertex base, which avoids mixing global
        // indices with per-primitive vertex addresses.
        const std::vector<uint32_t> primitiveIndices = readIndices(model, model.accessors[primitive.indices]);
        if (primitiveIndices.empty()) {
            output.positions.resize(firstVertex);
            continue;
        }

        const auto maxIndexIt = std::max_element(primitiveIndices.begin(), primitiveIndices.end());
        if (maxIndexIt != primitiveIndices.end() && *maxIndexIt >= positionAccessor.count) {
            OutputMessage(
                "[SceneGpuData] Primitive local index overflow: maxIndex={} vertexCount={} material={}\n",
                *maxIndexIt,
                static_cast<uint32_t>(positionAccessor.count),
                primitive.material >= 0 ? primitive.material : -1);
            output.positions.resize(firstVertex);
            continue;
        }
        output.indices.insert(output.indices.end(), primitiveIndices.begin(), primitiveIndices.end());

        SceneMesh sceneMesh{};
        sceneMesh.firstIndex = firstIndex;
        sceneMesh.indexCount = static_cast<uint32_t>(primitiveIndices.size());
        sceneMesh.firstVertex = firstVertex;
        sceneMesh.vertexCount = static_cast<uint32_t>(positionAccessor.count);
        sceneMesh.materialIndex = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material) : 0u;
        sceneMesh.minBounds = worldMin;
        sceneMesh.maxBounds = worldMax;
        sceneMesh.centroid = (worldMin + worldMax) * 0.5f;
        output.meshes.push_back(sceneMesh);
    }
}

CompactGeometryData loadCompactGeometryFromGltf(const std::string& filename)
{
    CompactGeometryData output{};
    tinygltf::TinyGLTF gltfContext;
    gltfContext.SetImageLoader(loadImageDataFuncEmpty, nullptr);            //cpu先不读Image

    tinygltf::Model gltfModel;
    std::string warning{};
    std::string error{};
    const bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename);
    if (!warning.empty()) {
        OutputMessage("[SceneGpuData] glTF warning: {}\n", warning);
    }
    if (!fileLoaded) {
        OutputMessage("[SceneGpuData] Failed to parse {}: {}\n", filename, error);
        return output;
    }
    if (gltfModel.scenes.empty()) {
        OutputMessage("[SceneGpuData] glTF file {} has no scenes\n", filename);
        return output;
    }

    const int defaultSceneIndex = gltfModel.defaultScene >= 0 ? gltfModel.defaultScene : 0;
    const tinygltf::Scene& defaultScene = gltfModel.scenes[defaultSceneIndex];
    for (int rootNodeIndex : defaultScene.nodes) {
        appendNodeGeometry(gltfModel, static_cast<uint32_t>(rootNodeIndex), glm::mat4(1.0f), output);
    }
    return output;
}

void prepareBufferForCreate(vkm::Buffer& buffer,
                            vk::BufferUsageFlags usageFlags,
                            vk::MemoryPropertyFlags memoryPropertyFlags)
{
    buffer.usageFlags = usageFlags;
    buffer.memoryPropertyFlags = memoryPropertyFlags;
}

vk::DeviceAddress getBufferDeviceAddress(vk::Device logicalDevice, vk::Buffer buffer)
{
    if (buffer == VK_NULL_HANDLE) {
        return 0;
    }

    vk::BufferDeviceAddressInfo addressInfo{};
    addressInfo.setBuffer(buffer);
    return logicalDevice.getBufferAddress(&addressInfo);
}

void createUploadBuffer(vkm::VKMDevice& device,
                        vk::Queue transferQueue,
                        vkm::Buffer& destinationBuffer,
                        const void* initialData,
                        vk::DeviceSize sizeBytes,
                        vk::BufferUsageFlags destinationUsage)
{
    vkm::Buffer stagingBuffer{};
    prepareBufferForCreate(
        stagingBuffer,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    stagingBuffer.createBuffer(
        sizeBytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        const_cast<void*>(initialData),
        &device);

    const vk::BufferUsageFlags destinationUsageFlags = destinationUsage | vk::BufferUsageFlagBits::eTransferDst;
    prepareBufferForCreate(destinationBuffer, destinationUsageFlags, vk::MemoryPropertyFlagBits::eDeviceLocal);
    destinationBuffer.createBuffer(
        sizeBytes,
        destinationUsageFlags,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        nullptr,
        &device);
    destinationBuffer.setupDescriptor(sizeBytes);

    device.copyBuffer(&stagingBuffer, &destinationBuffer, transferQueue);
    stagingBuffer.destroy();
}

} // namespace

void SceneGpuData::create(vkm::VKMDevice* inDevice, const Scene& scene, vk::Queue transferQueue)
{
    destroy();
    if (inDevice == nullptr) {
        OutputMessage("[SceneGpuData] create received a null device\n");
        return;
    }
    if (scene.sourcePath().empty()) {
        OutputMessage("[SceneGpuData] create requires a loaded scene source path\n");
        return;
    }

    device = inDevice;
    CompactGeometryData compactGeometry = loadCompactGeometryFromGltf(scene.sourcePath());
    compactPositions = compactGeometry.positions;
    compactIndices = compactGeometry.indices;
    compactMeshes = compactGeometry.meshes;
    if (compactMeshes.size() != scene.meshes().size()) {
        OutputMessage(
            "[SceneGpuData] Primitive range count mismatch: scene={} compact={}\n",
            static_cast<uint32_t>(scene.meshes().size()),
            static_cast<uint32_t>(compactMeshes.size()));
    }
    vertexCount = static_cast<uint32_t>(compactPositions.size());
    indexCount = static_cast<uint32_t>(compactIndices.size());
    if (vertexCount == 0u || indexCount == 0u) {
        OutputMessage("[SceneGpuData] Compact glTF geometry is empty for {}\n", scene.sourcePath());
        destroy();
        return;
    }

    createUploadBuffer(
        *device,
        transferQueue,
        rtPositionBuffer,
        compactPositions.data(),
        static_cast<vk::DeviceSize>(compactPositions.size() * sizeof(glm::vec3)),
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eShaderDeviceAddress |
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eVertexBuffer);
    createUploadBuffer(
        *device,
        transferQueue,
        rtIndexBuffer,
        compactIndices.data(),
        static_cast<vk::DeviceSize>(compactIndices.size() * sizeof(uint32_t)),
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eShaderDeviceAddress |
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eIndexBuffer);

    positionBufferDeviceAddress = getBufferDeviceAddress(device->logicalDevice, rtPositionBuffer.buffer);
    indexBufferDeviceAddress = getBufferDeviceAddress(device->logicalDevice, rtIndexBuffer.buffer);
    rtPositionBuffer.deviceAddress = positionBufferDeviceAddress;
    rtIndexBuffer.deviceAddress = indexBufferDeviceAddress;

    positionBufferInfo.setBuffer(rtPositionBuffer.buffer)
        .setOffset(0)
        .setRange(static_cast<vk::DeviceSize>(vertexCount) * sizeof(glm::vec3));
    indexBufferInfo.setBuffer(rtIndexBuffer.buffer)
        .setOffset(0)
        .setRange(static_cast<vk::DeviceSize>(indexCount) * sizeof(uint32_t));

    created = positionBufferInfo.buffer != VK_NULL_HANDLE &&
        indexBufferInfo.buffer != VK_NULL_HANDLE &&
        positionBufferDeviceAddress != 0 &&
        indexBufferDeviceAddress != 0 &&
        !compactMeshes.empty();
    if (!created) {
        OutputMessage("[SceneGpuData] Failed to create RT compact buffers for {}\n", scene.sourcePath());
        destroy();
        return;
    }

    OutputMessage(
        "[SceneGpuData] RT geometry uploaded: {} vertices, {} indices, {} primitive ranges\n",
        vertexCount,
        indexCount,
        static_cast<uint32_t>(compactMeshes.size()));
}

void SceneGpuData::destroy()
{
    rtIndexBuffer.destroy();
    rtPositionBuffer.destroy();
    compactPositions.clear();
    compactIndices.clear();
    compactMeshes.clear();
    positionBufferInfo = vk::DescriptorBufferInfo{};
    indexBufferInfo = vk::DescriptorBufferInfo{};
    positionBufferDeviceAddress = 0;
    indexBufferDeviceAddress = 0;
    vertexCount = 0u;
    indexCount = 0u;
    created = false;
    device = nullptr;
}

} // namespace scene


