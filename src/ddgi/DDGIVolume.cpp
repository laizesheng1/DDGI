#include "ddgi/DDGIVolume.h"

#include <algorithm>
#include <array>

namespace ddgi {
namespace {

uint32_t dispatchGroups(uint32_t texelCount, uint32_t localSize)
{
    return (texelCount + localSize - 1u) / localSize;
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

    vk::DescriptorPoolSize poolSize{};
    poolSize.setType(vk::DescriptorType::eAccelerationStructureKHR).setDescriptorCount(1);

    vk::DescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.setMaxSets(1).setPoolSizeCount(1).setPPoolSizes(&poolSize);
    VK_CHECK_RESULT(device.logicalDevice.createDescriptorPool(&poolCreateInfo, nullptr, &descriptorPool));

    vk::DescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.setDescriptorPool(descriptorPool).setDescriptorSetCount(1).setPSetLayouts(&sceneSetLayout);
    VK_CHECK_RESULT(device.logicalDevice.allocateDescriptorSets(&allocateInfo, &descriptorSet));
}

void updateRtSceneDescriptor(vk::Device logicalDevice,
                             vk::DescriptorSet descriptorSet,
                             vk::AccelerationStructureKHR topLevelAccelerationStructure)
{
    if (descriptorSet == VK_NULL_HANDLE || topLevelAccelerationStructure == VK_NULL_HANDLE) {
        return;
    }

    vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructureWrite{};
    accelerationStructureWrite.setAccelerationStructures(topLevelAccelerationStructure);

    vk::WriteDescriptorSet writeDescriptorSet{};
    writeDescriptorSet.setDstSet(descriptorSet)
        .setDstBinding(0)
        .setDescriptorCount(1)
        .setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
        .setPNext(&accelerationStructureWrite);
    logicalDevice.updateDescriptorSets(writeDescriptorSet, {});
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
    constants.biasAndDebug = glm::vec4(desc.normalBias, desc.viewBias, desc.sdfProbePushDistance, 0.0f);
    constants.atlasLayout = glm::uvec4(
        resourceSet.textures().probeTileColumns,
        resourceSet.textures().probeTileRows,
        desc.irradianceOctSize,
        desc.depthOctSize);
    resourceSet.updateConstants(constants);
}

void DDGIVolume::traceProbeRays(vk::CommandBuffer commandBuffer, const rt::RayTracingScene& scene)
{
    if (!resourceSet.isCreated() ||
        pipelineSet.trace() == VK_NULL_HANDLE ||
        scene.topLevelAccelerationStructure == VK_NULL_HANDLE ||
        rtSceneDescriptorSet == VK_NULL_HANDLE ||
        !traceShaderBindingTable.isCreated()) {
        return;
    }

    resourceSet.recordInitialLayoutTransitions(commandBuffer);
    recordComputeToRayTracingBarrier(commandBuffer, resourceSet.probeOffsets());
    recordComputeToRayTracingBarrier(commandBuffer, resourceSet.probeStates());
    if (boundTopLevelAccelerationStructure != scene.topLevelAccelerationStructure) {
        updateRtSceneDescriptor(device->logicalDevice, rtSceneDescriptorSet, scene.topLevelAccelerationStructure);
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

    const uint32_t totalRayCount = resourceSet.totalProbeCount() * desc.raysPerProbe;
    const rt::ShaderBindingTableRegions& sbtRegions = traceShaderBindingTable.stridedRegions();
    // The trace launch is 1D because ddgi_trace.rgen maps LaunchID.x directly
    // to (probeIndex, rayIndex). Later gather stages consume probeRayData as a
    // flat array with the same layout.
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

    if (pipelineSet.classify() != VK_NULL_HANDLE) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineSet.classify());
        commandBuffer.dispatch(dispatchGroups(resourceSet.totalProbeCount(), 64u), 1u, 1u);
        recordStorageBufferShaderBarrier(commandBuffer, resourceSet.probeStates());
    }

    if (pipelineSet.relocate() != VK_NULL_HANDLE) {
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
    if (!resourceSet.isCreated() || pipelineSet.sdfProbeUpdate() == VK_NULL_HANDLE) {
        return;
    }

    resourceSet.recordInitialLayoutTransitions(commandBuffer);
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

} // namespace ddgi
