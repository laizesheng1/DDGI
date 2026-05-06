#include "ddgi/DDGIVolume.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace ddgi {
namespace {

uint32_t dispatchGroups(uint32_t texelCount, uint32_t localSize)
{
    return (texelCount + localSize - 1u) / localSize;
}

uint32_t probeUpdatePhaseCount(const DDGIVolumeDesc& desc)
{
    return std::max(1u, desc.probeUpdatePhaseCount);
}

uint32_t currentProbeUpdatePhase(const DDGIVolumeDesc& desc, uint32_t frameIndex)
{
    const uint32_t phaseCount = probeUpdatePhaseCount(desc);
    return phaseCount > 0u ? frameIndex % phaseCount : 0u;
}

uint32_t probesInUpdatePhase(const DDGIVolumeDesc& desc, uint32_t frameIndex)
{
    const uint32_t probeCount = desc.probeCounts.x * desc.probeCounts.y * desc.probeCounts.z;
    const uint32_t phaseCount = probeUpdatePhaseCount(desc);
    const uint32_t phaseIndex = currentProbeUpdatePhase(desc, frameIndex);
    if (phaseIndex >= probeCount) {
        return 0u;
    }
    return ((probeCount - 1u - phaseIndex) / phaseCount) + 1u;
}

uint32_t updateFlags(const DDGIVolumeDesc& desc)
{
    uint32_t flags = 0u;
    if (desc.relocationEnabled) {
        flags |= static_cast<uint32_t>(RelocationEnabled);
    }
    if (desc.classificationEnabled) {
        flags |= static_cast<uint32_t>(ClassificationEnabled);
    }
    return flags;
}

glm::uvec3 probeCoordinateFromIndex(uint32_t probeIndex, const glm::uvec3& probeCounts)
{
    const uint32_t countX = std::max(1u, probeCounts.x);
    const uint32_t countY = std::max(1u, probeCounts.y);
    const uint32_t xyCount = countX * countY;
    return glm::uvec3(
        probeIndex % countX,
        (probeIndex / countX) % countY,
        probeIndex / xyCount);
}

void recordStorageBufferShaderBarrier(vk::CommandBuffer commandBuffer, const vkm::Buffer& buffer)
{
    if (buffer.buffer == VK_NULL_HANDLE) {
        return;
    }

    std::array<vk::BufferMemoryBarrier, 1> barriers{};
    barriers[0].setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setBuffer(buffer.buffer)
        .setOffset(0)
        .setSize(VK_WHOLE_SIZE);

    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {},
        {},
        barriers,
        {});
}

void recordComputeToRayTracingBarrier(vk::CommandBuffer commandBuffer, const vkm::Buffer& buffer)
{
    if (buffer.buffer == VK_NULL_HANDLE) {
        return;
    }

    std::array<vk::BufferMemoryBarrier, 1> barriers{};
    barriers[0].setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setBuffer(buffer.buffer)
        .setOffset(0)
        .setSize(VK_WHOLE_SIZE);

    // Probe relocation/SDF updates write offsets and states in compute. The RT
    // raygen shader immediately consumes those buffers to place probe origins.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        {},
        {},
        barriers,
        {});
}

void destroyRtSceneDescriptors(vkm::VKMDevice* device, vk::DescriptorPool& descriptorPool, vk::DescriptorSet& descriptorSet)
{
    if (device == nullptr) {
        descriptorPool = VK_NULL_HANDLE;
        descriptorSet = VK_NULL_HANDLE;
        return;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        device->logicalDevice.destroyDescriptorPool(descriptorPool);
    }
    descriptorPool = VK_NULL_HANDLE;
    descriptorSet = VK_NULL_HANDLE;
}

void createRtSceneDescriptors(vkm::VKMDevice& device,
                              vk::DescriptorSetLayout sceneSetLayout,
                              vk::DescriptorPool& descriptorPool,
                              vk::DescriptorSet& descriptorSet)
{
    if (sceneSetLayout == VK_NULL_HANDLE) {
        return;
    }

    std::array<vk::DescriptorPoolSize, 2> poolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eAccelerationStructureKHR, 1u},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 4u},
    };

    vk::DescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.setMaxSets(1).setPoolSizes(poolSizes);
    VK_CHECK_RESULT(device.logicalDevice.createDescriptorPool(&poolCreateInfo, nullptr, &descriptorPool));

    vk::DescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.setDescriptorPool(descriptorPool).setDescriptorSetCount(1).setPSetLayouts(&sceneSetLayout);
    VK_CHECK_RESULT(device.logicalDevice.allocateDescriptorSets(&allocateInfo, &descriptorSet));
}

void updateRtSceneDescriptor(vk::Device logicalDevice,
                             vk::DescriptorSet descriptorSet,
                             const rt::RayTracingScene& scene)
{
    if (descriptorSet == VK_NULL_HANDLE ||
        scene.topLevelAccelerationStructure == VK_NULL_HANDLE ||
        scene.gpuData == nullptr ||
        !scene.gpuData->isCreated()) {
        return;
    }

    vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructureWrite{};
    accelerationStructureWrite.setAccelerationStructures(scene.topLevelAccelerationStructure);

    std::array<vk::DescriptorBufferInfo, 4> bufferInfos{
        scene.gpuData->vertexAttributeDescriptor(),
        scene.gpuData->indexDescriptor(),
        scene.gpuData->meshDescriptor(),
        scene.gpuData->materialDescriptor(),
    };

    std::array<vk::WriteDescriptorSet, 5> descriptorWrites{};
    descriptorWrites[0].setDstSet(descriptorSet)
        .setDstBinding(0)
        .setDescriptorCount(1)
        .setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
        .setPNext(&accelerationStructureWrite);

    for (uint32_t writeIndex = 1u; writeIndex < descriptorWrites.size(); ++writeIndex) {
        descriptorWrites[writeIndex].setDstSet(descriptorSet)
            .setDstBinding(writeIndex)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setPBufferInfo(&bufferInfos[writeIndex - 1u]);
    }

    // The RT scene descriptor set is updated only when the TLAS changes. These
    // storage buffers are immutable for the loaded scene, so the closest-hit
    // shader can reconstruct material factors without touching vulkan_base's
    // glTF descriptors or requiring bindless textures.
    logicalDevice.updateDescriptorSets(descriptorWrites, {});
}

} // namespace

void DDGIVolume::create(vkm::VKMDevice* inDevice,
                        vk::PipelineCache pipelineCache,
                        const DDGIVolumeDesc& inDesc)
{
    destroy();
    device = inDevice;
    desc = inDesc;
    resourceSet.create(device, desc);
    if (!resourceSet.isCreated()) {
        return;
    }

    pipelineSet.create(device, pipelineCache, resourceSet.descriptorSetLayout());
    if (device == nullptr || pipelineSet.trace() == VK_NULL_HANDLE) {
        return;
    }

    const rt::RayTracingSupport support = rt::RayTracingContext::querySupport(device->physicalDevice, device->supportedExtensions);
    if (!support.supported) {
        return;
    }

    traceShaderBindingTable.create(device, pipelineSet.trace(), support.rayTracingPipelineProperties);
    createRtSceneDescriptors(
        *device,
        pipelineSet.rayTracingSceneSetLayout(),
        rtSceneDescriptorPool,
        rtSceneDescriptorSet);
    boundTopLevelAccelerationStructure = VK_NULL_HANDLE;
}

void DDGIVolume::destroy()
{
    traceShaderBindingTable.destroy();
    destroyRtSceneDescriptors(device, rtSceneDescriptorPool, rtSceneDescriptorSet);
    pipelineSet.destroy();
    resourceSet.destroy();
    boundTopLevelAccelerationStructure = VK_NULL_HANDLE;
    clearRequested = false;
    device = nullptr;
}

void DDGIVolume::updateConstants(const Camera& camera, uint32_t frameIndex)
{
    constants.view = camera.matrices.view;
    constants.projection = camera.matrices.perspective;
    constants.cameraPosition = glm::vec4(camera.position, 1.0f);
    constants.volumeOriginAndRays = glm::vec4(desc.origin, static_cast<float>(desc.raysPerProbe));
    constants.probeSpacingAndHysteresis = glm::vec4(desc.probeSpacing, desc.hysteresis);
    constants.probeCounts = glm::uvec4(desc.probeCounts, frameIndex);
    constants.biasAndDebug = glm::vec4(
        desc.normalBias,
        desc.viewBias,
        desc.sdfProbePushDistance,
        static_cast<float>(probeUpdatePhaseCount(desc)));
    constants.atlasLayout = glm::uvec4(
        resourceSet.textures().probeTileColumns,
        resourceSet.textures().probeTileRows,
        desc.irradianceOctSize,
        desc.depthOctSize);
    constants.traceParams = glm::vec4(
        desc.maxRayDistance,
        desc.irradianceGamma,
        desc.distanceExponent,
        0.0f);
    constants.stabilityParams = glm::vec4(
        desc.probeChangeThreshold,
        desc.probeBrightnessThreshold,
        desc.probeBackfaceThreshold,
        desc.probeMinFrontfaceDistance);
    constants.updateParams = glm::uvec4(
        std::min(desc.fixedRayCount, desc.raysPerProbe),
        probeUpdatePhaseCount(desc),
        updateFlags(desc),
        clearRequested ? 1u : 0u);
    constants.scrollAnchorAndMovement = glm::vec4(
        desc.scrollAnchor,
        desc.movementType == DDGIMovementType::Scrolling ? 1.0f : 0.0f);
    resourceSet.updateConstants(constants);
}

void DDGIVolume::requestClearProbes()
{
    clearRequested = true;
}

void DDGIVolume::traceProbeRays(vk::CommandBuffer commandBuffer, const rt::RayTracingScene& scene)
{
    if (!resourceSet.isCreated() ||
        pipelineSet.trace() == VK_NULL_HANDLE ||
        scene.topLevelAccelerationStructure == VK_NULL_HANDLE ||
        scene.gpuData == nullptr ||
        !scene.gpuData->isCreated() ||
        rtSceneDescriptorSet == VK_NULL_HANDLE ||
        !traceShaderBindingTable.isCreated()) {
        return;
    }

    resourceSet.recordInitialLayoutTransitions(commandBuffer);
    recordComputeToRayTracingBarrier(commandBuffer, resourceSet.probeOffsets());
    recordComputeToRayTracingBarrier(commandBuffer, resourceSet.probeStates());
    if (boundTopLevelAccelerationStructure != scene.topLevelAccelerationStructure) {
        updateRtSceneDescriptor(device->logicalDevice, rtSceneDescriptorSet, scene);
        boundTopLevelAccelerationStructure = scene.topLevelAccelerationStructure;
    }

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, pipelineSet.trace());
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eRayTracingKHR,
        pipelineSet.layout(),
        0,
        resourceSet.descriptorSet(),
        {});
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eRayTracingKHR,
        pipelineSet.layout(),
        1,
        rtSceneDescriptorSet,
        {});

    const uint32_t phaseProbeCount = probesInUpdatePhase(desc, constants.probeCounts.w);
    const uint32_t totalRayCount = phaseProbeCount * desc.raysPerProbe;
    if (totalRayCount == 0u) {
        return;
    }
    const rt::ShaderBindingTableRegions& sbtRegions = traceShaderBindingTable.stridedRegions();
    // The trace launch is 1D and only covers the probes assigned to the
    // current update phase. The raygen shader expands the phase-local launch
    // index back to the full probe index before writing probeRayData, so
    // lighting and atlas gather still see one stable flat array layout.
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdTraceRaysKHR(
        static_cast<VkCommandBuffer>(commandBuffer),
        reinterpret_cast<const VkStridedDeviceAddressRegionKHR*>(&sbtRegions.raygen),
        reinterpret_cast<const VkStridedDeviceAddressRegionKHR*>(&sbtRegions.miss),
        reinterpret_cast<const VkStridedDeviceAddressRegionKHR*>(&sbtRegions.hit),
        reinterpret_cast<const VkStridedDeviceAddressRegionKHR*>(&sbtRegions.callable),
        totalRayCount,
        1u,
        1u);
}

void DDGIVolume::updateProbes(vk::CommandBuffer commandBuffer)
{
    if (!resourceSet.isCreated() || !pipelineSet.hasComputePipelines()) {
        return;
    }

    resourceSet.recordInitialLayoutTransitions(commandBuffer);
    resourceSet.recordProbeRayReadBarrier(commandBuffer);

    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipelineSet.layout(),
        0,
        resourceSet.descriptorSet(),
        {});

    if (desc.classificationEnabled && pipelineSet.classify() != VK_NULL_HANDLE) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineSet.classify());
        commandBuffer.dispatch(dispatchGroups(resourceSet.totalProbeCount(), 64u), 1u, 1u);
        recordStorageBufferShaderBarrier(commandBuffer, resourceSet.probeStates());
    }

    if (desc.relocationEnabled && pipelineSet.relocate() != VK_NULL_HANDLE) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineSet.relocate());
        commandBuffer.dispatch(dispatchGroups(resourceSet.totalProbeCount(), 64u), 1u, 1u);
        recordStorageBufferShaderBarrier(commandBuffer, resourceSet.probeOffsets());
    }

    const DDGITextureSet& textureSet = resourceSet.textures();
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineSet.updateIrradiance());
    commandBuffer.dispatch(dispatchGroups(textureSet.irradianceExtent.width, 8u),
                           dispatchGroups(textureSet.irradianceExtent.height, 8u),
                           1u);
    resourceSet.recordAtlasWriteBarrier(commandBuffer);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineSet.updateDepth());
    commandBuffer.dispatch(dispatchGroups(textureSet.depthExtent.width, 8u),
                           dispatchGroups(textureSet.depthExtent.height, 8u),
                           1u);
    resourceSet.recordAtlasWriteBarrier(commandBuffer);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineSet.updateDepthSquared());
    commandBuffer.dispatch(dispatchGroups(textureSet.depthSquaredExtent.width, 8u),
                           dispatchGroups(textureSet.depthSquaredExtent.height, 8u),
                           1u);
    resourceSet.recordAtlasWriteBarrier(commandBuffer);

    if (pipelineSet.copyBorders() != VK_NULL_HANDLE) {
        const uint32_t copyWidth = std::max(textureSet.irradianceExtent.width, textureSet.depthExtent.width);
        const uint32_t copyHeight = std::max(textureSet.irradianceExtent.height, textureSet.depthExtent.height);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineSet.copyBorders());
        commandBuffer.dispatch(dispatchGroups(copyWidth, 8u), dispatchGroups(copyHeight, 8u), 1u);
        resourceSet.recordAtlasWriteBarrier(commandBuffer);
    }
}

void DDGIVolume::updateProbesFromSDF(vk::CommandBuffer commandBuffer, const sdf::SDFVolume&)
{
    if (!resourceSet.isCreated()) {
        return;
    }

    resourceSet.recordInitialLayoutTransitions(commandBuffer);

    if (clearRequested) {
        // Clearing happens before any per-frame DDGI shader uses the atlases.
        // This prevents old irradiance/depth from being mixed back through
        // hysteresis after the user changes temporal parameters or layout.
        resourceSet.resetProbeBuffers(desc);
        resourceSet.recordClearAtlases(commandBuffer);
        clearRequested = false;
    }

    if (pipelineSet.sdfProbeUpdate() == VK_NULL_HANDLE) {
        return;
    }

    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipelineSet.layout(),
        0,
        resourceSet.descriptorSet(),
        {});
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineSet.sdfProbeUpdate());
    commandBuffer.dispatch(dispatchGroups(resourceSet.totalProbeCount(), 64u), 1u, 1u);
    recordStorageBufferShaderBarrier(commandBuffer, resourceSet.probeOffsets());
    recordStorageBufferShaderBarrier(commandBuffer, resourceSet.probeStates());
}

void DDGIVolume::bindForLighting(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout, uint32_t setIndex) const
{
    if (!resourceSet.isCreated()) {
        return;
    }

    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        setIndex,
        resourceSet.descriptorSet(),
        {});
}

bool DDGIVolume::readProbeDebugData(std::vector<glm::vec3>& averageRadiance,
                                    std::vector<glm::vec3>& localOffsets,
                                    std::vector<uint32_t>& states) const
{
    if (!resourceSet.isCreated() || device == nullptr) {
        return false;
    }

    const uint32_t probeCount = resourceSet.totalProbeCount();
    const uint32_t raysPerProbe = desc.raysPerProbe;
    averageRadiance.assign(probeCount, glm::vec3(0.0f));
    localOffsets.assign(probeCount, glm::vec3(0.0f));
    states.assign(probeCount, 0u);

    const vkm::Buffer& probeRayDataBuffer = resourceSet.probeRayData();
    const vkm::Buffer& probeOffsetsBuffer = resourceSet.probeOffsets();
    const vkm::Buffer& probeStatesBuffer = resourceSet.probeStates();
    if (probeRayDataBuffer.memory == VK_NULL_HANDLE ||
        probeOffsetsBuffer.memory == VK_NULL_HANDLE ||
        probeStatesBuffer.memory == VK_NULL_HANDLE) {
        return false;
    }

    void* probeRayMapped = nullptr;
    void* probeOffsetsMapped = nullptr;
    void* probeStatesMapped = nullptr;
    VK_CHECK_RESULT(device->logicalDevice.mapMemory(
        probeRayDataBuffer.memory,
        0,
        probeRayDataBuffer.size,
        {},
        &probeRayMapped));
    VK_CHECK_RESULT(device->logicalDevice.mapMemory(
        probeOffsetsBuffer.memory,
        0,
        probeOffsetsBuffer.size,
        {},
        &probeOffsetsMapped));
    VK_CHECK_RESULT(device->logicalDevice.mapMemory(
        probeStatesBuffer.memory,
        0,
        probeStatesBuffer.size,
        {},
        &probeStatesMapped));

    const auto* rayData = reinterpret_cast<const DDGIProbeRayGpuData*>(probeRayMapped);
    const auto* offsets = reinterpret_cast<const glm::vec4*>(probeOffsetsMapped);
    const auto* stateData = reinterpret_cast<const uint32_t*>(probeStatesMapped);

    for (uint32_t probeIndex = 0; probeIndex < probeCount; ++probeIndex) {
        glm::vec3 radianceSum{0.0f};
        uint32_t validRayCount = 0u;
        const uint32_t rayBaseIndex = probeIndex * raysPerProbe;
        for (uint32_t rayIndex = 0; rayIndex < raysPerProbe; ++rayIndex) {
            const DDGIProbeRayGpuData& ray = rayData[rayBaseIndex + rayIndex];
            if (ray.radianceAndDistance.w <= 0.0f) {
                continue;
            }
            radianceSum += glm::max(glm::vec3(ray.radianceAndDistance), glm::vec3(0.0f));
            ++validRayCount;
        }
        averageRadiance[probeIndex] = validRayCount > 0u
            ? radianceSum / static_cast<float>(validRayCount)
            : glm::vec3(0.0f);
        localOffsets[probeIndex] = glm::vec3(offsets[probeIndex]);
        states[probeIndex] = stateData[probeIndex];
    }

    device->logicalDevice.unmapMemory(probeStatesBuffer.memory);
    device->logicalDevice.unmapMemory(probeOffsetsBuffer.memory);
    device->logicalDevice.unmapMemory(probeRayDataBuffer.memory);
    return true;
}

bool DDGIVolume::writeProbeOffset(uint32_t probeIndex, const glm::vec3& localOffset)
{
    const vkm::Buffer& probeOffsetsBuffer = resourceSet.probeOffsets();
    if (!resourceSet.isCreated() ||
        probeIndex >= resourceSet.totalProbeCount() ||
        device == nullptr ||
        probeOffsetsBuffer.memory == VK_NULL_HANDLE) {
        return false;
    }

    void* mapped = nullptr;
    const vk::DeviceSize byteOffset = static_cast<vk::DeviceSize>(probeIndex) * sizeof(glm::vec4);
    VK_CHECK_RESULT(device->logicalDevice.mapMemory(
        probeOffsetsBuffer.memory,
        byteOffset,
        sizeof(glm::vec4),
        {},
        &mapped));
    glm::vec4 packedOffset(localOffset, 0.0f);
    std::memcpy(mapped, &packedOffset, sizeof(glm::vec4));
    device->logicalDevice.unmapMemory(probeOffsetsBuffer.memory);
    return true;
}

glm::vec3 DDGIVolume::probeWorldPosition(uint32_t probeIndex, const glm::vec3* localOffset) const
{
    const glm::uvec3 probeCoord = probeCoordinateFromIndex(probeIndex, desc.probeCounts);
    glm::vec3 worldPosition = desc.origin + glm::vec3(probeCoord) * desc.probeSpacing;
    if (localOffset != nullptr) {
        worldPosition += *localOffset;
    }
    return worldPosition;
}

} // namespace ddgi
