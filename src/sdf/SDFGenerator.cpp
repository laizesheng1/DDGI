#include "sdf/SDFGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <cstring>
#include <vector>

namespace sdf {
namespace {

enum SDFGeneratorBinding : uint32_t {
    Constants = 0,
    SurfaceSeed = 1,
    SeedPing = 2,
    SeedPong = 3,
    FinalDistance = 4,
    Positions = 5,
    Indices = 6,
    Meshes = 7
};

uint32_t dispatchGroups(uint32_t value, uint32_t localSize)
{
    return (value + localSize - 1u) / localSize;
}

uint32_t voxelIndex(const glm::uvec3& coord, const glm::uvec3& resolution)
{
    return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
}

float maxComponent(const glm::vec3& value)
{
    return std::max(value.x, std::max(value.y, value.z));
}

glm::uvec3 clampVoxelCoord(const glm::ivec3& coord, const glm::uvec3& resolution)
{
    return glm::uvec3(glm::clamp(coord, glm::ivec3(0), glm::ivec3(resolution) - glm::ivec3(1)));
}

uint32_t totalTriangleCount(const scene::SceneGpuData& sceneGpuData)
{
    uint32_t triangleCount = 0u;
    for (const scene::SceneMesh& mesh : sceneGpuData.meshes()) {
        triangleCount += mesh.indexCount / 3u;
    }
    return triangleCount;
}

uint32_t largestPowerOfTwo(uint32_t value)
{
    uint32_t result = 1u;
    while ((result << 1u) <= value) {
        result <<= 1u;
    }
    return result;
}

std::string shaderPath(const char* relativePath)
{
#if defined(VKM_SHADERS_DIR)
    return std::string(VKM_SHADERS_DIR) + "glsl/" + relativePath;
#else
    return std::string("./../shaders/glsl/") + relativePath;
#endif
}

vk::ShaderModule loadShaderModule(vk::Device logicalDevice, const std::string& path)
{
    std::ifstream shaderFile(path, std::ios::binary | std::ios::ate);
    if (!shaderFile.is_open()) {
        OutputMessage("[SDF] Failed to open shader: {}\n", path);
        return VK_NULL_HANDLE;
    }

    const std::streamsize fileSize = shaderFile.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        OutputMessage("[SDF] Invalid SPIR-V size for shader {}: {} bytes\n", path, static_cast<int64_t>(fileSize));
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirvWords(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    shaderFile.seekg(0, std::ios::beg);
    shaderFile.read(reinterpret_cast<char*>(spirvWords.data()), fileSize);
    if (spirvWords.empty() || spirvWords.front() != 0x07230203u) {
        OutputMessage("[SDF] Shader is not valid SPIR-V: {}\n", path);
        return VK_NULL_HANDLE;
    }

    vk::ShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.setCode(spirvWords);
    vk::ShaderModule shaderModule{VK_NULL_HANDLE};
    VK_CHECK_RESULT(logicalDevice.createShaderModule(&moduleCreateInfo, nullptr, &shaderModule));
    return shaderModule;
}

vk::PipelineShaderStageCreateInfo makeStage(vk::ShaderModule shaderModule)
{
    vk::PipelineShaderStageCreateInfo shaderStage{};
    shaderStage.setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(shaderModule)
        .setPName("main");
    return shaderStage;
}

vk::Pipeline createComputePipeline(vkm::VKMDevice& device,
                                   vk::PipelineCache pipelineCache,
                                   vk::PipelineLayout pipelineLayout,
                                   const char* shaderRelativePath)
{
    vk::ShaderModule shaderModule = loadShaderModule(device.logicalDevice, shaderPath(shaderRelativePath));
    if (shaderModule == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    vk::ComputePipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.setStage(makeStage(shaderModule))
        .setLayout(pipelineLayout);

    vk::Pipeline pipeline{VK_NULL_HANDLE};
    VK_CHECK_RESULT(device.logicalDevice.createComputePipelines(pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
    device.logicalDevice.destroyShaderModule(shaderModule);
    return pipeline;
}

vk::DescriptorSetLayoutBinding makeBinding(uint32_t binding,
                                           vk::DescriptorType descriptorType,
                                           vk::ShaderStageFlags stages)
{
    vk::DescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.setBinding(binding)
        .setDescriptorType(descriptorType)
        .setDescriptorCount(1)
        .setStageFlags(stages);
    return layoutBinding;
}

float pointTriangleDistanceSquared(const glm::vec3& point,
                                   const glm::vec3& a,
                                   const glm::vec3& b,
                                   const glm::vec3& c)
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = point - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return glm::dot(ap, ap);
    }

    const glm::vec3 bp = point - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return glm::dot(bp, bp);
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        const glm::vec3 closest = a + v * ab;
        return glm::dot(point - closest, point - closest);
    }

    const glm::vec3 cp = point - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return glm::dot(cp, cp);
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        const glm::vec3 closest = a + w * ac;
        return glm::dot(point - closest, point - closest);
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        const glm::vec3 closest = b + w * (c - b);
        return glm::dot(point - closest, point - closest);
    }

    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    const glm::vec3 closest = a + ab * v + ac * w;
    return glm::dot(point - closest, point - closest);
}

} // namespace

void SDFGenerator::createDescriptorState()
{
    vk::ShaderStageFlags computeStage = vk::ShaderStageFlagBits::eCompute;
    std::array<vk::DescriptorSetLayoutBinding, 8> layoutBindings{
        makeBinding(Constants, vk::DescriptorType::eUniformBuffer, computeStage),
        makeBinding(SurfaceSeed, vk::DescriptorType::eStorageImage, computeStage),
        makeBinding(SeedPing, vk::DescriptorType::eStorageImage, computeStage),
        makeBinding(SeedPong, vk::DescriptorType::eStorageImage, computeStage),
        makeBinding(FinalDistance, vk::DescriptorType::eStorageImage, computeStage),
        makeBinding(Positions, vk::DescriptorType::eStorageBuffer, computeStage),
        makeBinding(Indices, vk::DescriptorType::eStorageBuffer, computeStage),
        makeBinding(Meshes, vk::DescriptorType::eStorageBuffer, computeStage),
    };

    vk::DescriptorSetLayoutCreateInfo layoutCreateInfo{};
    layoutCreateInfo.setBindings(layoutBindings);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(&layoutCreateInfo, nullptr, &descriptorSetLayout));

    std::array<vk::DescriptorPoolSize, 3> poolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1u},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 4u},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 3u},
    };
    vk::DescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.setMaxSets(1).setPoolSizes(poolSizes);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorPool(&poolCreateInfo, nullptr, &descriptorPool));

    vk::DescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.setDescriptorPool(descriptorPool)
        .setDescriptorSetCount(1)
        .setPSetLayouts(&descriptorSetLayout);
    VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&allocateInfo, &descriptorSet));

    constantsBuffer.usageFlags = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst;
    constantsBuffer.memoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    constantsBuffer.createBuffer(
        sizeof(GpuConstants),
        constantsBuffer.usageFlags,
        constantsBuffer.memoryPropertyFlags,
        nullptr,
        device);
    constantsBuffer.setupDescriptor(sizeof(GpuConstants));
}

void SDFGenerator::createPipelineState(vk::PipelineCache pipelineCache)
{
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(GpuConstants));

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setSetLayoutCount(1)
        .setPSetLayouts(&descriptorSetLayout)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&pushConstantRange);
    VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(&pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

    clearPipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "sdf/sdf_clear.comp.spv");
    voxelizePipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "sdf/sdf_voxelize_surface.comp.spv");
    jumpFloodPipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "sdf/sdf_jump_flood.comp.spv");
    finalizePipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "sdf/sdf_finalize.comp.spv");
}

void SDFGenerator::updateDescriptorSet(const scene::SceneGpuData& sceneGpuData, const SDFVolume& outputVolume)
{
    const std::array<vk::DescriptorImageInfo, 4> imageInfos{
        outputVolume.seedImage(SDFSeedImage::Surface).storageDescriptor,
        outputVolume.seedImage(SDFSeedImage::Ping).storageDescriptor,
        outputVolume.seedImage(SDFSeedImage::Pong).storageDescriptor,
        outputVolume.finalDistanceStorageDescriptor(),
    };
    const std::array<vk::DescriptorBufferInfo, 4> bufferInfos{
        constantsBuffer.descriptor,
        sceneGpuData.vertexDescriptor(),
        sceneGpuData.indexDescriptor(),
        sceneGpuData.meshDescriptor(),
    };

    std::array<vk::WriteDescriptorSet, 8> writes{};
    writes[0].setDstSet(descriptorSet)
        .setDstBinding(Constants)
        .setDescriptorCount(1)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setPBufferInfo(&bufferInfos[0]);
    for (uint32_t imageIndex = 0u; imageIndex < imageInfos.size(); ++imageIndex) {
        writes[1u + imageIndex].setDstSet(descriptorSet)
            .setDstBinding(1u + imageIndex)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setPImageInfo(&imageInfos[imageIndex]);
    }
    for (uint32_t bufferIndex = 1u; bufferIndex < bufferInfos.size(); ++bufferIndex) {
        writes[4u + bufferIndex].setDstSet(descriptorSet)
            .setDstBinding(4u + bufferIndex)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setPBufferInfo(&bufferInfos[bufferIndex]);
    }
    device->logicalDevice.updateDescriptorSets(writes, {});
}

void SDFGenerator::uploadConstants(const GpuConstants& constants)
{
    if (constantsBuffer.buffer == VK_NULL_HANDLE) {
        return;
    }
    VK_CHECK_RESULT(constantsBuffer.map(sizeof(GpuConstants)));
    std::memcpy(constantsBuffer.mapped, &constants, sizeof(GpuConstants));
    constantsBuffer.unmap();
}

void SDFGenerator::recordImageBarrier(vk::CommandBuffer commandBuffer, const SDFVolume& volume) const
{
    std::array<vk::ImageMemoryBarrier, 4> barriers{};
    const std::array<vk::Image, 4> images{
        volume.seedImage(SDFSeedImage::Surface).image,
        volume.seedImage(SDFSeedImage::Ping).image,
        volume.seedImage(SDFSeedImage::Pong).image,
        volume.finalDistanceImage().image,
    };
    vk::ImageSubresourceRange range{};
    range.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
    for (size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        barriers[imageIndex].setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(images[imageIndex])
            .setSubresourceRange(range);
    }

    // Every pass can read a seed image that an earlier pass wrote. Keeping this
    // barrier narrow to compute shader access makes the ping-pong ordering
    // explicit without forcing an unnecessary graphics-queue dependency.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {},
        {},
        {},
        barriers);
}

void SDFGenerator::create(vkm::VKMDevice* inDevice, vk::PipelineCache pipelineCache)
{
    destroy();
    if (inDevice == nullptr) {
        OutputMessage("[SDF] SDFGenerator::create received a null device\n");
        return;
    }

    device = inDevice;
    createDescriptorState();
    createPipelineState(pipelineCache);
    created =
        descriptorSet != VK_NULL_HANDLE &&
        clearPipeline != VK_NULL_HANDLE &&
        voxelizePipeline != VK_NULL_HANDLE &&
        jumpFloodPipeline != VK_NULL_HANDLE &&
        finalizePipeline != VK_NULL_HANDLE;
    if (!created) {
        OutputMessage("[SDF] GPU generator is incomplete; CPU fallback remains available\n");
    }
}

void SDFGenerator::destroy()
{
    if (device == nullptr) {
        return;
    }

    auto logicalDevice = device->logicalDevice;
    if (finalizePipeline) logicalDevice.destroyPipeline(finalizePipeline);
    if (jumpFloodPipeline) logicalDevice.destroyPipeline(jumpFloodPipeline);
    if (voxelizePipeline) logicalDevice.destroyPipeline(voxelizePipeline);
    if (clearPipeline) logicalDevice.destroyPipeline(clearPipeline);
    if (pipelineLayout) logicalDevice.destroyPipelineLayout(pipelineLayout);
    constantsBuffer.destroy();
    if (descriptorPool) logicalDevice.destroyDescriptorPool(descriptorPool);
    if (descriptorSetLayout) logicalDevice.destroyDescriptorSetLayout(descriptorSetLayout);

    finalizePipeline = VK_NULL_HANDLE;
    jumpFloodPipeline = VK_NULL_HANDLE;
    voxelizePipeline = VK_NULL_HANDLE;
    clearPipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorSet = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
    created = false;
    device = nullptr;
}

void SDFGenerator::generateFromSceneGPU(const scene::Scene& scene,
                                        const scene::SceneGpuData& sceneGpuData,
                                        SDFVolume& outputVolume,
                                        vk::Queue computeQueue)
{
    if (!outputVolume.isCreated()) {
        OutputMessage("[SDF] GPU generation skipped: output volume is not created\n");
        return;
    }
    if (!created || device == nullptr || !sceneGpuData.isCreated()) {
        OutputMessage("[SDF] GPU generation unavailable; falling back to CPU SDF\n");
        generateFromSceneCPU(scene, sceneGpuData, outputVolume, computeQueue);
        return;
    }

    const SDFVolumeDesc& desc = outputVolume.description();
    const uint32_t triangleCount = totalTriangleCount(sceneGpuData);
    const uint32_t maxResolution = std::max(desc.resolution.x, std::max(desc.resolution.y, desc.resolution.z));
    const uint32_t firstStep = std::max(1u, largestPowerOfTwo(maxResolution) / 2u);
    uint32_t iterationCount = 0u;
    for (uint32_t step = firstStep; step >= 1u; step >>= 1u) {
        ++iterationCount;
        if (step == 1u) {
            break;
        }
    }
    OutputMessage("[SDF] GPU JFA generation: resolution={}x{}x{} voxelSize=({}, {}, {}) triangles={} iterations={}\n",
                  desc.resolution.x,
                  desc.resolution.y,
                  desc.resolution.z,
                  desc.voxelSize.x,
                  desc.voxelSize.y,
                  desc.voxelSize.z,
                  triangleCount,
                  iterationCount);

    updateDescriptorSet(sceneGpuData, outputVolume);

    vk::CommandBuffer commandBuffer = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
    outputVolume.recordTransitionAllToGeneral(commandBuffer);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});

    GpuConstants constants{};
    constants.originAndMaxDistance = glm::vec4(desc.origin, desc.maxDistance);
    constants.voxelSizeAndSurfaceThreshold = glm::vec4(desc.voxelSize, desc.narrowBandDistance);
    constants.resolutionAndTriangleCount = glm::uvec4(desc.resolution, triangleCount);
    constants.meshCountAndStep = glm::uvec4(static_cast<uint32_t>(sceneGpuData.meshes().size()), 0u, 0u, 0u);

    commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(GpuConstants), &constants);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, clearPipeline);
    commandBuffer.dispatch(dispatchGroups(desc.resolution.x, 4u),
                           dispatchGroups(desc.resolution.y, 4u),
                           dispatchGroups(desc.resolution.z, 4u));
    recordImageBarrier(commandBuffer, outputVolume);

    if (triangleCount > 0u) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, voxelizePipeline);
        commandBuffer.dispatch(dispatchGroups(triangleCount, 64u), 1u, 1u);
        recordImageBarrier(commandBuffer, outputVolume);
    }

    uint32_t sourceImage = 0u;
    uint32_t destImage = 1u;
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, jumpFloodPipeline);
    for (uint32_t step = firstStep; step >= 1u; step >>= 1u) {
        constants.meshCountAndStep = glm::uvec4(
            static_cast<uint32_t>(sceneGpuData.meshes().size()),
            step,
            sourceImage,
            destImage);
        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(GpuConstants), &constants);
        commandBuffer.dispatch(dispatchGroups(desc.resolution.x, 4u),
                               dispatchGroups(desc.resolution.y, 4u),
                               dispatchGroups(desc.resolution.z, 4u));
        recordImageBarrier(commandBuffer, outputVolume);
        sourceImage = destImage;
        destImage = destImage == 1u ? 2u : 1u;
        if (step == 1u) {
            break;
        }
    }

    constants.meshCountAndStep = glm::uvec4(
        static_cast<uint32_t>(sceneGpuData.meshes().size()),
        0u,
        sourceImage,
        0u);
    commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(GpuConstants), &constants);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, finalizePipeline);
    commandBuffer.dispatch(dispatchGroups(desc.resolution.x, 4u),
                           dispatchGroups(desc.resolution.y, 4u),
                           dispatchGroups(desc.resolution.z, 4u));
    outputVolume.recordFinalDistanceShaderReadBarrier(commandBuffer);

    device->flushCommandBuffer(commandBuffer, computeQueue, true);
}

void SDFGenerator::generateFromSceneCPU(const scene::Scene&,
                                        const scene::SceneGpuData& sceneGpuData,
                                        SDFVolume& outputVolume,
                                        vk::Queue transferQueue)
{
    const SDFVolumeDesc& desc = outputVolume.description();
    const uint32_t totalVoxelCount = desc.resolution.x * desc.resolution.y * desc.resolution.z;
    std::vector<float> distances(totalVoxelCount, desc.maxDistance);

    if (!sceneGpuData.isCreated() ||
        sceneGpuData.meshes().empty() ||
        sceneGpuData.positions().empty() ||
        sceneGpuData.indices().empty() ||
        totalVoxelCount == 0u) {
        outputVolume.uploadDistanceDataCPU(distances, transferQueue);
        return;
    }

    const std::vector<glm::vec3>& positions = sceneGpuData.positions();
    const std::vector<uint32_t>& indices = sceneGpuData.indices();
    const std::vector<scene::SceneMesh>& meshes = sceneGpuData.meshes();
    const float narrowBand = std::max(desc.narrowBandDistance, maxComponent(desc.voxelSize) * 4.0f);
    uint32_t processedTriangleCount = 0u;

    for (const scene::SceneMesh& mesh : meshes) {
        for (uint32_t localTriangle = 0u; localTriangle + 2u < mesh.indexCount; localTriangle += 3u) {
            const uint32_t indexBase = mesh.firstIndex + localTriangle;
            const uint32_t ia = mesh.firstVertex + indices[indexBase + 0u];
            const uint32_t ib = mesh.firstVertex + indices[indexBase + 1u];
            const uint32_t ic = mesh.firstVertex + indices[indexBase + 2u];
            if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size()) {
                continue;
            }

            const glm::vec3 a = positions[ia];
            const glm::vec3 b = positions[ib];
            const glm::vec3 c = positions[ic];
            const glm::vec3 triMin = glm::min(a, glm::min(b, c)) - glm::vec3(narrowBand);
            const glm::vec3 triMax = glm::max(a, glm::max(b, c)) + glm::vec3(narrowBand);
            const glm::uvec3 minCoord = clampVoxelCoord(glm::ivec3(glm::floor((triMin - desc.origin) / desc.voxelSize)), desc.resolution);
            const glm::uvec3 maxCoord = clampVoxelCoord(glm::ivec3(glm::ceil((triMax - desc.origin) / desc.voxelSize)), desc.resolution);

            for (uint32_t z = minCoord.z; z <= maxCoord.z; ++z) {
                for (uint32_t y = minCoord.y; y <= maxCoord.y; ++y) {
                    for (uint32_t x = minCoord.x; x <= maxCoord.x; ++x) {
                        const glm::uvec3 coord{x, y, z};
                        const glm::vec3 worldPosition =
                            desc.origin +
                            (glm::vec3(coord) + glm::vec3(0.5f)) * desc.voxelSize;
                        const float distance = std::sqrt(pointTriangleDistanceSquared(worldPosition, a, b, c));
                        const uint32_t flatIndex = voxelIndex(coord, desc.resolution);
                        distances[flatIndex] = std::min(distances[flatIndex], distance);
                    }
                }
            }
            ++processedTriangleCount;
        }
    }

    outputVolume.uploadDistanceDataCPU(distances, transferQueue);
    OutputMessage("[SDF] CPU fallback generated unsigned triangle distance field from {} triangles\n", processedTriangleCount);
}

} // namespace sdf
