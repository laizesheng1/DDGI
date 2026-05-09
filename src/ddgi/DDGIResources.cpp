#include "ddgi/DDGIResources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "rt/RayTracingContext.h"

namespace ddgi {
namespace {

constexpr vk::Format kIrradianceFormat = vk::Format::eR16G16B16A16Sfloat;
constexpr vk::Format kDepthFormat = vk::Format::eR32Sfloat;
constexpr vk::Format kDepthSquaredFormat = vk::Format::eR32Sfloat;

uint32_t calculateProbeCount(const DDGIVolumeDesc& desc)
{
    return desc.probeCounts.x * desc.probeCounts.y * desc.probeCounts.z;
}

vk::Extent2D calculateAtlasExtent(uint32_t probeCount, uint32_t tileSize, uint32_t tileColumns)
{
    const uint32_t tileRows = (probeCount + tileColumns - 1u) / tileColumns;
    return vk::Extent2D{tileColumns * tileSize, tileRows * tileSize};
}

bool validateVolumeDesc(vk::PhysicalDevice physicalDevice, const DDGIVolumeDesc& desc, uint32_t probeCount)
{
    const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    if (probeCount == 0u || desc.raysPerProbe == 0u) {
        OutputMessage("[DDGI] Invalid volume: probeCount={} raysPerProbe={}\n", probeCount, desc.raysPerProbe);
        return false;
    }
    if (desc.irradianceOctSize < 2u || desc.depthOctSize < 2u) {
        OutputMessage("[DDGI] Invalid atlas oct size: irradiance={} depth={}\n", desc.irradianceOctSize, desc.depthOctSize);
        return false;
    }
    if (desc.probeCounts.x == 0u || desc.probeCounts.y == 0u || desc.probeCounts.z == 0u) {
        OutputMessage("[DDGI] Invalid probe counts: {} {} {}\n", desc.probeCounts.x, desc.probeCounts.y, desc.probeCounts.z);
        return false;
    }

    const uint32_t tileColumns = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(probeCount)))));
    const vk::Extent2D irradianceExtent = calculateAtlasExtent(probeCount, desc.irradianceOctSize + 2u, tileColumns);
    const vk::Extent2D depthExtent = calculateAtlasExtent(probeCount, desc.depthOctSize + 2u, tileColumns);
    const uint32_t max2D = properties.limits.maxImageDimension2D;
    if (irradianceExtent.width > max2D || irradianceExtent.height > max2D ||
        depthExtent.width > max2D || depthExtent.height > max2D) {
        OutputMessage(
            "[DDGI] Atlas extent exceeds device limit {}: irradiance={}x{} moments={}x{}\n",
            max2D,
            irradianceExtent.width,
            irradianceExtent.height,
            depthExtent.width,
            depthExtent.height);
        return false;
    }

    const uint64_t rayRecords = static_cast<uint64_t>(probeCount) * static_cast<uint64_t>(desc.raysPerProbe);
    if (rayRecords > (1ull << 30ull)) {
        OutputMessage("[DDGI] Probe ray buffer would be too large: {} records\n", rayRecords);
        return false;
    }
    return true;
}

void prepareBufferForCreate(vkm::Buffer& buffer,
                            vk::BufferUsageFlags usageFlags,
                            vk::MemoryPropertyFlags memoryPropertyFlags)
{
    // Buffer::createBuffer reads these members while allocating. Set them here so
    // the DDGI module can reuse the existing wrapper without changing vulkan_base.
    buffer.usageFlags = usageFlags;
    buffer.memoryPropertyFlags = memoryPropertyFlags;
}

void createBuffer(vkm::VKMDevice& device,
                  vkm::Buffer& buffer,
                  vk::DeviceSize sizeBytes,
                  vk::BufferUsageFlags usageFlags,
                  vk::MemoryPropertyFlags memoryPropertyFlags,
                  void* initialData)
{
    prepareBufferForCreate(buffer, usageFlags, memoryPropertyFlags);
    buffer.createBuffer(sizeBytes, usageFlags, memoryPropertyFlags, initialData, &device);
    buffer.setupDescriptor(sizeBytes);
}

void createStorageTexture(vkm::VKMDevice& device,
                          vkm::Texture2D& texture,
                          vk::Extent2D extent,
                          vk::Format format,
                          const char* debugLabel)
{
    texture = vkm::Texture2D(&device);
    texture.width = extent.width;
    texture.height = extent.height;
    texture.mipLevels = 1;
    texture.layerCount = 1;

    vk::ImageCreateInfo imageCreateInfo{};
    texture.initImageCreateInfo(
        imageCreateInfo,
        format,
        vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferSrc |
            vk::ImageUsageFlagBits::eTransferDst);

    VK_CHECK_RESULT(device.logicalDevice.createImage(&imageCreateInfo, nullptr, &texture.image));
    texture.allocImageDeviceMem(vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
    texture.CreateImageview(subresourceRange, format, vk::ImageViewType::e2D);

    vk::SamplerCreateInfo samplerCreateInfo{};
    samplerCreateInfo.setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatOpaqueBlack);
    VK_CHECK_RESULT(device.logicalDevice.createSampler(&samplerCreateInfo, nullptr, &texture.sampler));

    texture.imageLayout = vk::ImageLayout::eGeneral;
    texture.updateDescriptor();
    OutputMessage("[DDGI] Created {} atlas {}x{}\n", debugLabel, extent.width, extent.height);
}

vk::DescriptorSetLayoutBinding makeBinding(DDGIResourceBinding binding,
                                           vk::DescriptorType descriptorType,
                                           vk::ShaderStageFlags stageFlags)
{
    vk::DescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.setBinding(static_cast<uint32_t>(binding))
        .setDescriptorType(descriptorType)
        .setDescriptorCount(1)
        .setStageFlags(stageFlags);
    return layoutBinding;
}

std::vector<DDGIProbeRayGpuData> createClearedProbeRayData(uint32_t probeCount, uint32_t raysPerProbe)
{
    std::vector<DDGIProbeRayGpuData> rayData(static_cast<size_t>(probeCount) * raysPerProbe);
    for (DDGIProbeRayGpuData& ray : rayData) {
        // A very large distance means "miss / not initialized" for the atlas
        // update passes. Keeping the direction finite avoids NaNs in debug code
        // that averages directions or radiance after a clear.
        ray.radianceAndDistance = glm::vec4(0.0f, 0.0f, 0.0f, 1.0e27f);
        ray.directionAndDistanceSquared = glm::vec4(0.0f, 1.0f, 0.0f, 1.0e27f);
        ray.normalAndFlags = glm::vec4(0.0f);
    }
    return rayData;
}

} // namespace

void DDGIResources::create(vkm::VKMDevice* inDevice, const DDGIVolumeDesc& desc)
{
    if (inDevice == nullptr) {
        OutputMessage("[DDGI] DDGIResources::create received a null device\n");
        return;
    }

    destroy();
    device = inDevice;
    probeCount = calculateProbeCount(desc);
    if (!validateVolumeDesc(device->physicalDevice, desc, probeCount)) {
        device = nullptr;
        return;
    }

    textureSet.irradianceTileSize = desc.irradianceOctSize + 2u;
    textureSet.depthTileSize = desc.depthOctSize + 2u;
    textureSet.probeTileColumns = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(probeCount)))));
    textureSet.probeTileRows = (probeCount + textureSet.probeTileColumns - 1u) / textureSet.probeTileColumns;
    textureSet.irradianceExtent = calculateAtlasExtent(probeCount, textureSet.irradianceTileSize, textureSet.probeTileColumns);
    textureSet.depthExtent = calculateAtlasExtent(probeCount, textureSet.depthTileSize, textureSet.probeTileColumns);
    textureSet.depthSquaredExtent = textureSet.depthExtent;

    createStorageTexture(*device, textureSet.irradiance, textureSet.irradianceExtent, kIrradianceFormat, "irradiance");
    createStorageTexture(*device, textureSet.depth, textureSet.depthExtent, kDepthFormat, "depth");
    createStorageTexture(*device, textureSet.depthSquared, textureSet.depthSquaredExtent, kDepthSquaredFormat, "depthSquared");

    probeRayDataBytes = static_cast<vk::DeviceSize>(probeCount) *
        static_cast<vk::DeviceSize>(desc.raysPerProbe) *
        sizeof(DDGIProbeRayGpuData);
    probeOffsetsBytes = static_cast<vk::DeviceSize>(probeCount) * sizeof(glm::vec4);
    probeStatesBytes = static_cast<vk::DeviceSize>(probeCount) * sizeof(uint32_t);
    std::vector<DDGIProbeRayGpuData> initialRayData = createClearedProbeRayData(probeCount, desc.raysPerProbe);
    std::vector<glm::vec4> initialProbeOffsets(probeCount, glm::vec4(0.0f));
    std::vector<uint32_t> initialProbeStates(probeCount, 0u);

    createBuffer(*device,
                 constantsBuffer,
                 sizeof(DDGIFrameConstants),
                 vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 nullptr);
    createBuffer(*device,
                 probeRayDataBuffer,
                 probeRayDataBytes,
                 vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 initialRayData.data());
    createBuffer(*device,
                 probeOffsetsBuffer,
                 probeOffsetsBytes,
                 vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 initialProbeOffsets.data());
    createBuffer(*device,
                 probeStatesBuffer,
                 probeStatesBytes,
                 vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                 initialProbeStates.data());

    vk::ShaderStageFlags ddgiStages = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment;
    const rt::RayTracingSupport rayTracingSupport = rt::RayTracingContext::querySupport(device->physicalDevice, device->supportedExtensions);
    if (rayTracingSupport.supported) {
        ddgiStages |= vk::ShaderStageFlagBits::eRaygenKHR |
            vk::ShaderStageFlagBits::eClosestHitKHR |
            vk::ShaderStageFlagBits::eMissKHR;
    }

    std::array<vk::DescriptorSetLayoutBinding, 7> layoutBindings{
        makeBinding(DDGIResourceBinding::Constants, vk::DescriptorType::eUniformBuffer, ddgiStages),
        makeBinding(DDGIResourceBinding::ProbeRayData, vk::DescriptorType::eStorageBuffer, ddgiStages),
        makeBinding(DDGIResourceBinding::ProbeOffsets, vk::DescriptorType::eStorageBuffer, ddgiStages),
        makeBinding(DDGIResourceBinding::ProbeStates, vk::DescriptorType::eStorageBuffer, ddgiStages),
        makeBinding(DDGIResourceBinding::IrradianceAtlas, vk::DescriptorType::eStorageImage, ddgiStages),
        makeBinding(DDGIResourceBinding::DepthAtlas, vk::DescriptorType::eStorageImage, ddgiStages),
        makeBinding(DDGIResourceBinding::DepthSquaredAtlas, vk::DescriptorType::eStorageImage, ddgiStages),
    };

    vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
    descriptorSetLayoutCreateInfo.setBindings(layoutBindings);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(
        &descriptorSetLayoutCreateInfo,
        nullptr,
        &descriptorSetLayoutHandle));

    std::array<vk::DescriptorPoolSize, 3> poolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 3},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 3},
    };
    vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo{};
    descriptorPoolCreateInfo.setMaxSets(1).setPoolSizes(poolSizes);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorPool(&descriptorPoolCreateInfo, nullptr, &descriptorPool));

    vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo{};
    descriptorSetAllocateInfo.setDescriptorPool(descriptorPool)
        .setDescriptorSetCount(1)
        .setPSetLayouts(&descriptorSetLayoutHandle);
    VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&descriptorSetAllocateInfo, &descriptorSetHandle));

    std::array<vk::WriteDescriptorSet, 7> descriptorWrites{
        vkm::initializers::writeDescriptorSet(descriptorSetHandle, vk::DescriptorType::eUniformBuffer, static_cast<uint32_t>(DDGIResourceBinding::Constants), &constantsBuffer.descriptor),
        vkm::initializers::writeDescriptorSet(descriptorSetHandle, vk::DescriptorType::eStorageBuffer, static_cast<uint32_t>(DDGIResourceBinding::ProbeRayData), &probeRayDataBuffer.descriptor),
        vkm::initializers::writeDescriptorSet(descriptorSetHandle, vk::DescriptorType::eStorageBuffer, static_cast<uint32_t>(DDGIResourceBinding::ProbeOffsets), &probeOffsetsBuffer.descriptor),
        vkm::initializers::writeDescriptorSet(descriptorSetHandle, vk::DescriptorType::eStorageBuffer, static_cast<uint32_t>(DDGIResourceBinding::ProbeStates), &probeStatesBuffer.descriptor),
        vkm::initializers::writeDescriptorSet(descriptorSetHandle, vk::DescriptorType::eStorageImage, static_cast<uint32_t>(DDGIResourceBinding::IrradianceAtlas), &textureSet.irradiance.descriptorImageInfo),
        vkm::initializers::writeDescriptorSet(descriptorSetHandle, vk::DescriptorType::eStorageImage, static_cast<uint32_t>(DDGIResourceBinding::DepthAtlas), &textureSet.depth.descriptorImageInfo),
        vkm::initializers::writeDescriptorSet(descriptorSetHandle, vk::DescriptorType::eStorageImage, static_cast<uint32_t>(DDGIResourceBinding::DepthSquaredAtlas), &textureSet.depthSquared.descriptorImageInfo),
    };
    device->logicalDevice.updateDescriptorSets(descriptorWrites, {});

    needsInitialLayoutTransition = true;
    created = true;
}

void DDGIResources::destroy()
{
    if (device == nullptr) {
        return;
    }

    auto logicalDevice = device->logicalDevice;
    if (descriptorPool) {
        logicalDevice.destroyDescriptorPool(descriptorPool);
    }
    if (descriptorSetLayoutHandle) {
        logicalDevice.destroyDescriptorSetLayout(descriptorSetLayoutHandle);
    }

    probeStatesBuffer.destroy();
    probeOffsetsBuffer.destroy();
    probeRayDataBuffer.destroy();
    constantsBuffer.destroy();
    textureSet.depthSquared.destroy();
    textureSet.depth.destroy();
    textureSet.irradiance.destroy();

    textureSet = {};
    descriptorPool = VK_NULL_HANDLE;
    descriptorSetLayoutHandle = VK_NULL_HANDLE;
    descriptorSetHandle = VK_NULL_HANDLE;
    probeRayDataBytes = 0;
    probeOffsetsBytes = 0;
    probeStatesBytes = 0;
    probeCount = 0;
    created = false;
    needsInitialLayoutTransition = false;
    device = nullptr;
}

void DDGIResources::updateConstants(const DDGIFrameConstants& constants)
{
    if (!created || constantsBuffer.buffer == VK_NULL_HANDLE) {
        return;
    }

    VK_CHECK_RESULT(constantsBuffer.map(sizeof(DDGIFrameConstants)));
    std::memcpy(constantsBuffer.mapped, &constants, sizeof(DDGIFrameConstants));
    constantsBuffer.unmap();
}

void DDGIResources::resetProbeBuffers(const DDGIVolumeDesc& desc)
{
    if (!created || desc.raysPerProbe == 0u) {
        return;
    }

    std::vector<DDGIProbeRayGpuData> clearedRayData = createClearedProbeRayData(probeCount, desc.raysPerProbe);
    std::vector<glm::vec4> clearedOffsets(probeCount, glm::vec4(0.0f));
    std::vector<uint32_t> clearedStates(probeCount, 0u);

    // These buffers are intentionally host visible during bring-up so debug UI
    // can inspect and edit probe state. A direct map/copy clear is simpler and
    // avoids allocating transient staging buffers for a user-triggered action.
    if (probeRayDataBuffer.buffer != VK_NULL_HANDLE) {
        VK_CHECK_RESULT(probeRayDataBuffer.map(probeRayDataBytes));
        std::memcpy(probeRayDataBuffer.mapped, clearedRayData.data(), static_cast<size_t>(probeRayDataBytes));
        probeRayDataBuffer.unmap();
    }
    if (probeOffsetsBuffer.buffer != VK_NULL_HANDLE) {
        VK_CHECK_RESULT(probeOffsetsBuffer.map(probeOffsetsBytes));
        std::memcpy(probeOffsetsBuffer.mapped, clearedOffsets.data(), static_cast<size_t>(probeOffsetsBytes));
        probeOffsetsBuffer.unmap();
    }
    if (probeStatesBuffer.buffer != VK_NULL_HANDLE) {
        VK_CHECK_RESULT(probeStatesBuffer.map(probeStatesBytes));
        std::memcpy(probeStatesBuffer.mapped, clearedStates.data(), static_cast<size_t>(probeStatesBytes));
        probeStatesBuffer.unmap();
    }
}

void DDGIResources::recordInitialLayoutTransitions(vk::CommandBuffer commandBuffer)
{
    if (!created || !needsInitialLayoutTransition) {
        return;
    }

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    std::array<vk::ImageMemoryBarrier, 3> barriers{};
    const std::array<vk::Image, 3> images{
        textureSet.irradiance.image,
        textureSet.depth.image,
        textureSet.depthSquared.image,
    };

    for (size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        barriers[imageIndex].setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eNone)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(images[imageIndex])
            .setSubresourceRange(subresourceRange);
    }

    // The atlases start from Undefined and are first touched by DDGI compute/RT
    // shaders. The barrier makes those storage image reads/writes legal before
    // any update pipeline runs, while avoiding an unnecessary transfer clear.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        {},
        {},
        {},
        barriers);
    needsInitialLayoutTransition = false;
}

void DDGIResources::recordClearAtlases(vk::CommandBuffer commandBuffer) const
{
    if (!created) {
        return;
    }

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    const vk::ClearColorValue clearRadiance(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    const vk::ClearColorValue clearDistance(std::array<float, 4>{1.0e27f, 0.0f, 0.0f, 0.0f});

    commandBuffer.clearColorImage(textureSet.irradiance.image, vk::ImageLayout::eGeneral, clearRadiance, subresourceRange);
    commandBuffer.clearColorImage(textureSet.depth.image, vk::ImageLayout::eGeneral, clearDistance, subresourceRange);
    commandBuffer.clearColorImage(textureSet.depthSquared.image, vk::ImageLayout::eGeneral, clearDistance, subresourceRange);

    std::array<vk::ImageMemoryBarrier, 3> barriers{};
    const std::array<vk::Image, 3> images{
        textureSet.irradiance.image,
        textureSet.depth.image,
        textureSet.depthSquared.image,
    };
    for (size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        barriers[imageIndex].setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(images[imageIndex])
            .setSubresourceRange(subresourceRange);
    }

    // Clear uses the transfer pipeline, while subsequent DDGI passes read and
    // write the same storage images in RT, compute, and fragment shaders. This
    // barrier makes a user-triggered history reset visible without changing
    // the long-lived General layout used by the atlas descriptors.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eRayTracingShaderKHR |
            vk::PipelineStageFlagBits::eComputeShader |
            vk::PipelineStageFlagBits::eFragmentShader,
        {},
        {},
        {},
        barriers);
}

void DDGIResources::recordProbeRayReadBarrier(vk::CommandBuffer commandBuffer) const
{
    if (!created || probeRayDataBuffer.buffer == VK_NULL_HANDLE) {
        return;
    }

    std::array<vk::BufferMemoryBarrier, 1> barriers{};
    barriers[0].setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setBuffer(probeRayDataBuffer.buffer)
        .setOffset(0)
        .setSize(probeRayDataBytes);

    // Probe rays are produced by RT/compute shaders and consumed by the probe
    // update compute shaders in the same frame. This makes the write->read
    // dependency explicit once tracing is wired in.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eRayTracingShaderKHR | vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {},
        {},
        barriers,
        {});
}

void DDGIResources::recordAtlasTraceReadBarrier(vk::CommandBuffer commandBuffer) const
{
    if (!created) {
        return;
    }

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    std::array<vk::ImageMemoryBarrier, 3> barriers{};
    const std::array<vk::Image, 3> images{
        textureSet.irradiance.image,
        textureSet.depth.image,
        textureSet.depthSquared.image,
    };

    for (size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        barriers[imageIndex].setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(images[imageIndex])
            .setSubresourceRange(subresourceRange);
    }

    // Probe tracing samples the atlas from the previous/history update to
    // estimate multi-bounce diffuse radiance at hit points. This barrier makes
    // earlier compute atlas writes visible to RT closest-hit imageLoad() calls
    // while keeping the image layout General for storage image descriptors.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        {},
        {},
        {},
        barriers);
}

void DDGIResources::recordAtlasTraceReadToComputeWriteBarrier(vk::CommandBuffer commandBuffer) const
{
    if (!created) {
        return;
    }

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    std::array<vk::ImageMemoryBarrier, 3> barriers{};
    const std::array<vk::Image, 3> images{
        textureSet.irradiance.image,
        textureSet.depth.image,
        textureSet.depthSquared.image,
    };

    for (size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        barriers[imageIndex].setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(images[imageIndex])
            .setSubresourceRange(subresourceRange);
    }

    // Trace reads must finish before the update passes write the current frame
    // atlas. This prevents same-frame feedback; L_multi always comes from the
    // atlas history visible at the start of probe tracing.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        vk::PipelineStageFlagBits::eComputeShader,
        {},
        {},
        {},
        barriers);
}

void DDGIResources::recordAtlasWriteBarrier(vk::CommandBuffer commandBuffer) const
{
    if (!created) {
        return;
    }

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    std::array<vk::ImageMemoryBarrier, 3> barriers{};
    const std::array<vk::Image, 3> images{
        textureSet.irradiance.image,
        textureSet.depth.image,
        textureSet.depthSquared.image,
    };

    for (size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        barriers[imageIndex].setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(images[imageIndex])
            .setSubresourceRange(subresourceRange);
    }

    // DDGI update passes read previous atlas values for hysteresis and then
    // write new values. The next frame's probe tracing also reads these atlases
    // for diffuse multi-bounce feedback, so make writes visible to RT as well
    // as compute/fragment consumers.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eRayTracingShaderKHR |
            vk::PipelineStageFlagBits::eComputeShader |
            vk::PipelineStageFlagBits::eFragmentShader,
        {},
        {},
        {},
        barriers);
}

} // namespace ddgi
