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
#include "VKM_Tools.h"

namespace scene {
namespace {

constexpr uint32_t kMaterialFlagBaseColorTexture = 1u << 2u;
constexpr uint32_t kMaterialFlagNormalTexture = 1u << 3u;
constexpr uint32_t kMaterialFlagMetallicRoughnessTexture = 1u << 4u;
constexpr uint32_t kMaterialFlagEmissiveTexture = 1u << 5u;
constexpr bool kCompactRtSceneUsesFlipY = true;         //加载场景是否FlipY

struct CompactGeometryData {
    std::vector<glm::vec3> positions{};
    std::vector<uint32_t> indices{};
    std::vector<SceneRtVertexAttributeGpuData> vertexAttributes{};
    std::vector<SceneRtMeshGpuData> meshGpuData{};
    std::vector<SceneRtMaterialGpuData> materialGpuData{};
    std::vector<SceneMesh> meshes{};
    std::vector<SceneLightGpuData> lights{};
    uint32_t reversedWindingPrimitiveCount{0u};
};

glm::vec4 readTextureTransform(const tinygltf::ExtensionMap& extensions)
{
    glm::vec2 scale{1.0f, 1.0f};
    glm::vec2 offset{0.0f, 0.0f};
    const auto extensionIt = extensions.find("KHR_texture_transform");
    if (extensionIt == extensions.end() || !extensionIt->second.IsObject()) {
        return glm::vec4(scale, offset);
    }

    const tinygltf::Value& transform = extensionIt->second;
    const tinygltf::Value& scaleValue = transform.Get("scale");
    if (scaleValue.IsArray() && scaleValue.ArrayLen() >= 2u) {
        scale.x = static_cast<float>(scaleValue.Get(0).GetNumberAsDouble());
        scale.y = static_cast<float>(scaleValue.Get(1).GetNumberAsDouble());
    }

    const tinygltf::Value& offsetValue = transform.Get("offset");
    if (offsetValue.IsArray() && offsetValue.ArrayLen() >= 2u) {
        offset.x = static_cast<float>(offsetValue.Get(0).GetNumberAsDouble());
        offset.y = static_cast<float>(offsetValue.Get(1).GetNumberAsDouble());
    }

    // RTXGI stores only the shading inputs needed by probe tracing. Rotation is
    // uncommon in the sample assets and would require carrying a full 2x3
    // transform per texture, so this compact path preserves the scale/offset
    // terms already exposed by tinygltf and leaves room to widen the record.
    return glm::vec4(scale, offset);
}

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

glm::vec3 transformVector(const glm::mat4& transform, const glm::vec3& direction)
{
    const glm::vec3 transformed = glm::mat3(transform) * direction;
    const float length = glm::length(transformed);
    return length > 1.0e-5f ? transformed / length : direction;
}

SceneLightGpuData fallbackDirectionalLight()
{
    SceneLightGpuData light{};
    light.positionAndType = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(SceneLightDirectional));
    // Many sample Sponza glTF files do not carry KHR_lights_punctual. Use a
    // clear upper-front directional key light so the PBR path is visibly lit
    // even before DDGI has converged. The shader stores directional lights as
    // "surface-to-light" vectors, not the physical ray travel direction.
    light.directionAndRange = glm::vec4(glm::normalize(glm::vec3(-0.45f, 0.90f, -0.30f)), 0.0f);
    light.colorAndIntensity = glm::vec4(1.0f, 0.96f, 0.88f, 5.0f);
    light.spotAngles = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    return light;
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

glm::vec4 readVec4(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t vertexIndex);

glm::vec3 safeReadNormal(const tinygltf::Model& model,
                         const tinygltf::Primitive& primitive,
                         size_t vertexIndex,
                         const glm::mat3& normalFromLocal)
{
    const auto normalIt = primitive.attributes.find("NORMAL");
    if (normalIt == primitive.attributes.end()) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    const tinygltf::Accessor& normalAccessor = model.accessors[normalIt->second];
    const glm::vec3 normalLocal = readVec3(model, normalAccessor, vertexIndex);
    const glm::vec3 normalWorld = normalFromLocal * normalLocal;
    const float normalLength = glm::length(normalWorld);
    return normalLength > 1.0e-5f ? normalWorld / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec4 safeReadTangent(const tinygltf::Model& model,
                          const tinygltf::Primitive& primitive,
                          size_t vertexIndex,
                          const glm::mat3& normalFromLocal,
                          const glm::vec3& normalWorld)
{
    const auto tangentIt = primitive.attributes.find("TANGENT");
    if (tangentIt != primitive.attributes.end()) {
        const tinygltf::Accessor& tangentAccessor = model.accessors[tangentIt->second];
        const glm::vec4 tangentLocal = readVec4(model, tangentAccessor, vertexIndex);
        glm::vec3 tangentWorld = normalFromLocal * glm::vec3(tangentLocal);
        tangentWorld = tangentWorld - normalWorld * glm::dot(tangentWorld, normalWorld);
        const float tangentLength = glm::length(tangentWorld);
        if (tangentLength > 1.0e-5f) {
            return glm::vec4(tangentWorld / tangentLength, tangentLocal.w >= 0.0f ? 1.0f : -1.0f);
        }
    }

    const glm::vec3 helperAxis = std::abs(normalWorld.y) < 0.95f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 fallbackTangent = glm::normalize(glm::cross(helperAxis, normalWorld));
    return glm::vec4(fallbackTangent, 1.0f);
}

glm::vec2 safeReadTexCoord(const tinygltf::Model& model,
                           const tinygltf::Primitive& primitive,
                           size_t vertexIndex)
{
    const auto texCoordIt = primitive.attributes.find("TEXCOORD_0");
    if (texCoordIt == primitive.attributes.end()) {
        return glm::vec2(0.0f);
    }

    return readVec2(model, model.accessors[texCoordIt->second], vertexIndex);
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
    const glm::mat3 normalFromLocal = glm::transpose(glm::inverse(glm::mat3(worldFromLocal)));
    const float sceneHandedness = glm::determinant(glm::mat3(worldFromLocal)) *
        (kCompactRtSceneUsesFlipY ? -1.0f : 1.0f);
    const bool reversesHandedness = sceneHandedness < 0.0f;

    const auto lightExtensionIt = node.extensions.find("KHR_lights_punctual");
    if (lightExtensionIt != node.extensions.end() && lightExtensionIt->second.IsObject()) {
        const tinygltf::Value& lightExtension = lightExtensionIt->second;
        const tinygltf::Value& lightIndexValue = lightExtension.Get("light");
        if (lightIndexValue.IsInt()) {
            const int lightIndex = lightIndexValue.Get<int>();
            if (lightIndex >= 0 && lightIndex < static_cast<int>(model.lights.size())) {
                const tinygltf::Light& sourceLight = model.lights[lightIndex];
                SceneLightGpuData light{};
                const glm::vec3 positionWorld = glm::vec3(worldFromLocal * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                // glTF punctual lights point down local -Z. The shader uses a
                // direction from the shaded point toward the light for direct
                // illumination, so store the opposite vector for directional
                // lights and keep the physical -Z direction for spot cones.
                const glm::vec3 lightForwardWorld = transformVector(worldFromLocal, glm::vec3(0.0f, 0.0f, -1.0f));
                glm::vec3 color(1.0f);
                if (sourceLight.color.size() >= 3u) {
                    color = glm::vec3(
                        static_cast<float>(sourceLight.color[0]),
                        static_cast<float>(sourceLight.color[1]),
                        static_cast<float>(sourceLight.color[2]));
                }
                uint32_t lightType = SceneLightPoint;
                if (sourceLight.type == "directional") {
                    lightType = SceneLightDirectional;
                } else if (sourceLight.type == "spot") {
                    lightType = SceneLightSpot;
                }
                light.positionAndType = glm::vec4(positionWorld, static_cast<float>(lightType));
                light.directionAndRange = glm::vec4(
                    lightType == SceneLightDirectional ? -lightForwardWorld : lightForwardWorld,
                    static_cast<float>(sourceLight.range));
                light.colorAndIntensity = glm::vec4(color, static_cast<float>(sourceLight.intensity));
                const float inner = static_cast<float>(sourceLight.spot.innerConeAngle);
                const float outer = static_cast<float>(sourceLight.spot.outerConeAngle);
                light.spotAngles = glm::vec4(std::cos(inner), std::cos(outer), 0.0f, 0.0f);
                if (kCompactRtSceneUsesFlipY) {
                    light.positionAndType.y *= -1.0f;
                    light.directionAndRange.y *= -1.0f;
                }
                output.lights.push_back(light);
            }
        }
    }

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
            glm::vec3 positionWorld = glm::vec3(worldFromLocal * glm::vec4(positionLocal, 1.0f));
            glm::vec3 normalWorld = safeReadNormal(model, primitive, vertexIndex, normalFromLocal);
            glm::vec4 tangentWorld = safeReadTangent(model, primitive, vertexIndex, normalFromLocal, normalWorld);
            if (kCompactRtSceneUsesFlipY) {
                // Scene::loadFromFile renders the visible glTF with
                // FileLoadingFlags::FlipY | PreTransformVertices. DDGI's RT
                // path reparses the same glTF into compact buffers, so it must
                // apply the exact same post-transform Y reflection. Without
                // this, probes trace against a vertically mirrored TLAS while
                // debug spheres and lighting are placed in the rendered scene.
                positionWorld.y *= -1.0f;
                normalWorld.y *= -1.0f;
                tangentWorld.y *= -1.0f;
            }
            const glm::vec2 texCoord = safeReadTexCoord(model, primitive, vertexIndex);
            worldMin = glm::min(worldMin, positionWorld);
            worldMax = glm::max(worldMax, positionWorld);
            output.positions.push_back(positionWorld);

            SceneRtVertexAttributeGpuData vertexAttribute{};
            vertexAttribute.normalAndMaterial = glm::vec4(
                normalWorld,
                primitive.material >= 0 ? static_cast<float>(primitive.material) : 0.0f);
            vertexAttribute.uvAndFlags = glm::vec4(texCoord, 0.0f, 0.0f);
            vertexAttribute.tangentAndSign = tangentWorld;
            output.vertexAttributes.push_back(vertexAttribute);
        }

        // Keep compact RT indices local to this primitive's vertex range. BLAS
        // then uses firstVertex as the vertex base, which avoids mixing global
        // indices with per-primitive vertex addresses.
        const std::vector<uint32_t> primitiveIndices = readIndices(model, model.accessors[primitive.indices]);
        if (primitiveIndices.empty()) {
            output.positions.resize(firstVertex);
            output.vertexAttributes.resize(firstVertex);
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
            output.vertexAttributes.resize(firstVertex);
            continue;
        }
        if (reversesHandedness) {
            output.reversedWindingPrimitiveCount += 1u;
            // glTF nodes may use negative scale/mirrored matrices to reuse
            // architecture pieces. We bake node transforms into the compact RT
            // vertex buffer, so a negative determinant flips triangle winding
            // unless the index order is reversed as well. Vulkan ray tracing
            // derives gl_HitKindEXT from geometric winding, and DDGI strict
            // classification relies on that front/back result. Leaving mirrored
            // primitives unrepaired makes probes in empty space report backfaces
            // and probes inside geometry report frontfaces.
            for (size_t triangleIndex = 0; triangleIndex + 2u < primitiveIndices.size(); triangleIndex += 3u) {
                output.indices.push_back(primitiveIndices[triangleIndex + 0u]);
                output.indices.push_back(primitiveIndices[triangleIndex + 2u]);
                output.indices.push_back(primitiveIndices[triangleIndex + 1u]);
            }
        } else {
            output.indices.insert(output.indices.end(), primitiveIndices.begin(), primitiveIndices.end());
        }

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

        SceneRtMeshGpuData meshGpuData{};
        meshGpuData.firstIndexFirstVertexMaterialFlags = glm::uvec4(
            sceneMesh.firstIndex,
            sceneMesh.firstVertex,
            sceneMesh.materialIndex,
            0u);
        output.meshGpuData.push_back(meshGpuData);
    }
}

std::vector<SceneRtMaterialGpuData> loadMaterialFactors(const tinygltf::Model& model)
{
    std::vector<SceneRtMaterialGpuData> materials{};
    materials.reserve(std::max<size_t>(model.materials.size(), 1u));

    for (const tinygltf::Material& sourceMaterial : model.materials) {
        SceneRtMaterialGpuData material{};

        const tinygltf::PbrMetallicRoughness& pbr = sourceMaterial.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() == 4u) {
            material.baseColorAndAlphaCutoff = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(sourceMaterial.alphaCutoff));
            material.metallicRoughnessAndFlags.z = static_cast<float>(pbr.baseColorFactor[3]);
        }

        if (sourceMaterial.emissiveFactor.size() == 3u) {
            material.emissiveAndFlags = glm::vec4(
                static_cast<float>(sourceMaterial.emissiveFactor[0]),
                static_cast<float>(sourceMaterial.emissiveFactor[1]),
                static_cast<float>(sourceMaterial.emissiveFactor[2]),
                0.0f);
        }
        material.metallicRoughnessAndFlags.x = static_cast<float>(pbr.metallicFactor);
        material.metallicRoughnessAndFlags.y = static_cast<float>(pbr.roughnessFactor);

        uint32_t materialFlags = 0u;
        if (sourceMaterial.alphaMode == "MASK") {
            materialFlags |= 1u;
        } else if (sourceMaterial.alphaMode == "BLEND") {
            materialFlags |= 2u;
        }
        if (pbr.baseColorTexture.index >= 0) {
            materialFlags |= kMaterialFlagBaseColorTexture;
            material.baseColorTextureTransform = readTextureTransform(pbr.baseColorTexture.extensions);
        }
        if (pbr.metallicRoughnessTexture.index >= 0) {
            materialFlags |= kMaterialFlagMetallicRoughnessTexture;
            material.metallicRoughnessTextureTransform = readTextureTransform(pbr.metallicRoughnessTexture.extensions);
        }
        if (sourceMaterial.normalTexture.index >= 0) {
            materialFlags |= kMaterialFlagNormalTexture;
            material.normalTextureTransform = readTextureTransform(sourceMaterial.normalTexture.extensions);
        }
        if (sourceMaterial.emissiveTexture.index >= 0) {
            materialFlags |= kMaterialFlagEmissiveTexture;
            material.emissiveTextureTransform = readTextureTransform(sourceMaterial.emissiveTexture.extensions);
        }
        material.emissiveAndFlags.w = static_cast<float>(materialFlags);
        materials.push_back(material);
    }

    // Keep one default material at the end, matching vulkan_base::Model. This
    // prevents no-material primitives from indexing past the RT material buffer.
    materials.push_back(SceneRtMaterialGpuData{});
    return materials;
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

    output.materialGpuData = loadMaterialFactors(gltfModel);

    const int defaultSceneIndex = gltfModel.defaultScene >= 0 ? gltfModel.defaultScene : 0;
    const tinygltf::Scene& defaultScene = gltfModel.scenes[defaultSceneIndex];
    for (int rootNodeIndex : defaultScene.nodes) {
        appendNodeGeometry(gltfModel, static_cast<uint32_t>(rootNodeIndex), glm::mat4(1.0f), output);
    }
    if (output.lights.empty()) {
        output.lights.push_back(fallbackDirectionalLight());
        OutputMessage("[SceneGpuData] No KHR_lights_punctual lights found; using fallback directional light\n");
    } else {
        OutputMessage("[SceneGpuData] Loaded {} KHR_lights_punctual lights\n", static_cast<uint32_t>(output.lights.size()));
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

void createFallbackTexture(vkm::VKMDevice& device,
                           vk::Queue transferQueue,
                           vkm::Texture2D& texture,
                           const std::array<uint8_t, 4>& texel)
{
    texture.destroy();
    texture = vkm::Texture2D(&device);
    texture.width = 1u;
    texture.height = 1u;
    texture.mipLevels = 1u;
    texture.layerCount = 1u;

    vk::Buffer stagingBuffer{VK_NULL_HANDLE};
    vk::DeviceMemory stagingMemory{VK_NULL_HANDLE};
    std::array<uint8_t, 4> uploadTexel = texel;
    VK_CHECK_RESULT(device.createBuffer(
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        static_cast<vk::DeviceSize>(uploadTexel.size()),
        &stagingBuffer,
        &stagingMemory,
        uploadTexel.data()));

    vk::ImageCreateInfo imageCreateInfo{};
    texture.initImageCreateInfo(
        imageCreateInfo,
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
    VK_CHECK_RESULT(device.logicalDevice.createImage(&imageCreateInfo, nullptr, &texture.image));
    texture.allocImageDeviceMem();

    vk::ImageSubresourceRange range{};
    range.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
    vk::CommandBuffer commandBuffer = device.createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
    vkm::tools::setImageLayout(commandBuffer, texture.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, range);
    vk::BufferImageCopy copyRegion = vkm::tools::initBufferImageCopyInfo({1u, 1u, 1u});
    commandBuffer.copyBufferToImage(stagingBuffer, texture.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);
    vkm::tools::setImageLayout(commandBuffer, texture.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, range);
    device.flushCommandBuffer(commandBuffer, transferQueue, true);

    device.logicalDevice.destroyBuffer(stagingBuffer);
    device.logicalDevice.freeMemory(stagingMemory);
    texture.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    texture.CreateDefaultSampler();
    texture.CreateImageview(range, vk::Format::eR8G8B8A8Unorm);
    texture.updateDescriptor();
}

vk::DescriptorImageInfo textureOrFallback(const vkmglTF::Texture* texture, const vkm::Texture2D& fallbackTexture)
{
    if (texture != nullptr && texture->imageView != VK_NULL_HANDLE && texture->sampler != VK_NULL_HANDLE) {
        return texture->descriptorImageInfo;
    }
    return fallbackTexture.descriptorImageInfo;
}

void buildMaterialTextureDescriptors(const vkmglTF::Model& model,
                                     const vkm::Texture2D& fallbackWhite,
                                     const vkm::Texture2D& fallbackBlack,
                                     const vkm::Texture2D& fallbackNormal,
                                     std::vector<vk::DescriptorImageInfo>& baseColorDescriptors,
                                     std::vector<vk::DescriptorImageInfo>& normalDescriptors,
                                     std::vector<vk::DescriptorImageInfo>& metallicRoughnessDescriptors,
                                     std::vector<vk::DescriptorImageInfo>& emissiveDescriptors)
{
    const size_t materialCount = std::max<size_t>(model.materials.size(), 1u);
    baseColorDescriptors.assign(materialCount, fallbackWhite.descriptorImageInfo);
    normalDescriptors.assign(materialCount, fallbackNormal.descriptorImageInfo);
    metallicRoughnessDescriptors.assign(materialCount, fallbackWhite.descriptorImageInfo);
    emissiveDescriptors.assign(materialCount, fallbackBlack.descriptorImageInfo);

    for (size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex) {
        const vkmglTF::Material& material = model.materials[materialIndex];
        baseColorDescriptors[materialIndex] = textureOrFallback(material.baseColorTexture, fallbackWhite);
        normalDescriptors[materialIndex] = textureOrFallback(material.normalTexture, fallbackNormal);
        metallicRoughnessDescriptors[materialIndex] = textureOrFallback(material.metallicRoughnessTexture, fallbackWhite);
        emissiveDescriptors[materialIndex] = textureOrFallback(material.emissiveTexture, fallbackBlack);
    }
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
    createFallbackTexture(*device, transferQueue, fallbackWhiteTexture, std::array<uint8_t, 4>{255u, 255u, 255u, 255u});
    createFallbackTexture(*device, transferQueue, fallbackBlackTexture, std::array<uint8_t, 4>{0u, 0u, 0u, 255u});
    createFallbackTexture(*device, transferQueue, fallbackNormalTexture, std::array<uint8_t, 4>{128u, 128u, 255u, 255u});
    CompactGeometryData compactGeometry = loadCompactGeometryFromGltf(scene.sourcePath());
    compactPositions = compactGeometry.positions;
    compactIndices = compactGeometry.indices;
    compactVertexAttributes = compactGeometry.vertexAttributes;
    compactMeshGpuData = compactGeometry.meshGpuData;
    compactMaterialGpuData = compactGeometry.materialGpuData;
    compactMeshes = compactGeometry.meshes;
    lightGpuData = compactGeometry.lights.empty()
        ? std::vector<SceneLightGpuData>{fallbackDirectionalLight()}
        : compactGeometry.lights;
    lightingGpuData.lightCounts = glm::uvec4(static_cast<uint32_t>(lightGpuData.size()), 0u, 0u, 0u);
    buildMaterialTextureDescriptors(
        scene.model(),
        fallbackWhiteTexture,
        fallbackBlackTexture,
        fallbackNormalTexture,
        baseColorTextureDescriptors,
        normalTextureDescriptors,
        metallicRoughnessTextureDescriptors,
        emissiveTextureDescriptors);
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
    createUploadBuffer(
        *device,
        transferQueue,
        rtVertexAttributeBuffer,
        compactVertexAttributes.data(),
        static_cast<vk::DeviceSize>(compactVertexAttributes.size() * sizeof(SceneRtVertexAttributeGpuData)),
        vk::BufferUsageFlagBits::eStorageBuffer);
    createUploadBuffer(
        *device,
        transferQueue,
        rtMeshBuffer,
        compactMeshGpuData.data(),
        static_cast<vk::DeviceSize>(compactMeshGpuData.size() * sizeof(SceneRtMeshGpuData)),
        vk::BufferUsageFlagBits::eStorageBuffer);
    createUploadBuffer(
        *device,
        transferQueue,
        rtMaterialBuffer,
        compactMaterialGpuData.data(),
        static_cast<vk::DeviceSize>(compactMaterialGpuData.size() * sizeof(SceneRtMaterialGpuData)),
        vk::BufferUsageFlagBits::eStorageBuffer);
    createUploadBuffer(
        *device,
        transferQueue,
        lightBuffer,
        lightGpuData.data(),
        static_cast<vk::DeviceSize>(lightGpuData.size() * sizeof(SceneLightGpuData)),
        vk::BufferUsageFlagBits::eStorageBuffer);
    createUploadBuffer(
        *device,
        transferQueue,
        lightingInfoBuffer,
        &lightingGpuData,
        sizeof(SceneLightingGpuData),
        vk::BufferUsageFlagBits::eUniformBuffer);

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
    vertexAttributeBufferInfo.setBuffer(rtVertexAttributeBuffer.buffer)
        .setOffset(0)
        .setRange(static_cast<vk::DeviceSize>(compactVertexAttributes.size() * sizeof(SceneRtVertexAttributeGpuData)));
    meshBufferInfo.setBuffer(rtMeshBuffer.buffer)
        .setOffset(0)
        .setRange(static_cast<vk::DeviceSize>(compactMeshGpuData.size() * sizeof(SceneRtMeshGpuData)));
    materialBufferInfo.setBuffer(rtMaterialBuffer.buffer)
        .setOffset(0)
        .setRange(static_cast<vk::DeviceSize>(compactMaterialGpuData.size() * sizeof(SceneRtMaterialGpuData)));
    lightBufferInfo.setBuffer(lightBuffer.buffer)
        .setOffset(0)
        .setRange(static_cast<vk::DeviceSize>(lightGpuData.size() * sizeof(SceneLightGpuData)));
    lightingInfoBufferInfo.setBuffer(lightingInfoBuffer.buffer)
        .setOffset(0)
        .setRange(sizeof(SceneLightingGpuData));

    created = positionBufferInfo.buffer != VK_NULL_HANDLE &&
        indexBufferInfo.buffer != VK_NULL_HANDLE &&
        vertexAttributeBufferInfo.buffer != VK_NULL_HANDLE &&
        meshBufferInfo.buffer != VK_NULL_HANDLE &&
        materialBufferInfo.buffer != VK_NULL_HANDLE &&
        lightBufferInfo.buffer != VK_NULL_HANDLE &&
        lightingInfoBufferInfo.buffer != VK_NULL_HANDLE &&
        positionBufferDeviceAddress != 0 &&
        indexBufferDeviceAddress != 0 &&
        !compactMeshes.empty();
    if (!created) {
        OutputMessage("[SceneGpuData] Failed to create RT compact buffers for {}\n", scene.sourcePath());
        destroy();
        return;
    }

    OutputMessage(
        "[SceneGpuData] RT geometry uploaded: {} vertices, {} indices, {} primitive ranges, {} winding-corrected ranges, {} lights\n",
        vertexCount,
        indexCount,
        static_cast<uint32_t>(compactMeshes.size()),
        compactGeometry.reversedWindingPrimitiveCount,
        static_cast<uint32_t>(lightGpuData.size()));
}

void SceneGpuData::destroy()
{
    rtMaterialBuffer.destroy();
    lightingInfoBuffer.destroy();
    lightBuffer.destroy();
    rtMeshBuffer.destroy();
    rtVertexAttributeBuffer.destroy();
    rtIndexBuffer.destroy();
    rtPositionBuffer.destroy();
    fallbackNormalTexture.destroy();
    fallbackBlackTexture.destroy();
    fallbackWhiteTexture.destroy();
    compactPositions.clear();
    compactIndices.clear();
    compactVertexAttributes.clear();
    compactMeshGpuData.clear();
    compactMaterialGpuData.clear();
    lightGpuData.clear();
    lightingGpuData = {};
    baseColorTextureDescriptors.clear();
    normalTextureDescriptors.clear();
    metallicRoughnessTextureDescriptors.clear();
    emissiveTextureDescriptors.clear();
    compactMeshes.clear();
    positionBufferInfo = vk::DescriptorBufferInfo{};
    indexBufferInfo = vk::DescriptorBufferInfo{};
    vertexAttributeBufferInfo = vk::DescriptorBufferInfo{};
    meshBufferInfo = vk::DescriptorBufferInfo{};
    materialBufferInfo = vk::DescriptorBufferInfo{};
    lightBufferInfo = vk::DescriptorBufferInfo{};
    lightingInfoBufferInfo = vk::DescriptorBufferInfo{};
    positionBufferDeviceAddress = 0;
    indexBufferDeviceAddress = 0;
    vertexCount = 0u;
    indexCount = 0u;
    created = false;
    device = nullptr;
}

} // namespace scene
