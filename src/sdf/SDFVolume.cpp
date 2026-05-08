#include "sdf/SDFVolume.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "VKM_Tools.h"

namespace sdf {
namespace {

uint32_t voxelCount(const SDFVolumeDesc& desc)
{
    return desc.resolution.x * desc.resolution.y * desc.resolution.z;
}

uint32_t voxelIndex(const glm::uvec3& coord, const glm::uvec3& resolution)
{
    return coord.x + coord.y * resolution.x + coord.z * resolution.x * resolution.y;
}

float maxComponent(const glm::vec3& value)
{
    return std::max(value.x, std::max(value.y, value.z));
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

vk::ImageSubresourceRange colorRange()
{
    vk::ImageSubresourceRange range{};
    range.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
    return range;
}

} // namespace

void SDFVolume::createImageResource(SDFImageResource& resource,
                                    vk::Format format,
                                    vk::ImageUsageFlags usage,
                                    const char* debugName)
{
    resource = {};
    resource.format = format;

    vk::ImageCreateInfo imageCreateInfo{};
    imageCreateInfo.setImageType(vk::ImageType::e3D)
        .setFormat(format)
        .setExtent(vk::Extent3D{desc.resolution.x, desc.resolution.y, desc.resolution.z})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(usage)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);
    VK_CHECK_RESULT(device->logicalDevice.createImage(&imageCreateInfo, nullptr, &resource.image));

    const vk::MemoryRequirements memoryRequirements = device->logicalDevice.getImageMemoryRequirements(resource.image);
    vk::MemoryAllocateInfo allocateInfo{};
    allocateInfo.setAllocationSize(memoryRequirements.size)
        .setMemoryTypeIndex(device->queryMemTypeIndex(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
    VK_CHECK_RESULT(device->logicalDevice.allocateMemory(&allocateInfo, nullptr, &resource.memory));
    device->logicalDevice.bindImageMemory(resource.image, resource.memory, 0);

    vk::ImageViewCreateInfo viewCreateInfo{};
    viewCreateInfo.setImage(resource.image)
        .setViewType(vk::ImageViewType::e3D)
        .setFormat(format)
        .setSubresourceRange(colorRange());
    VK_CHECK_RESULT(device->logicalDevice.createImageView(&viewCreateInfo, nullptr, &resource.view));

    resource.layout = vk::ImageLayout::eUndefined;
    updateImageDescriptors(resource, nearestSampler);
    OutputMessage("[SDF] Created {} image {}x{}x{}\n", debugName, desc.resolution.x, desc.resolution.y, desc.resolution.z);
}

void SDFVolume::destroyImageResource(SDFImageResource& resource)
{
    if (resource.view != VK_NULL_HANDLE) {
        device->logicalDevice.destroyImageView(resource.view);
    }
    if (resource.image != VK_NULL_HANDLE) {
        device->logicalDevice.destroyImage(resource.image);
    }
    if (resource.memory != VK_NULL_HANDLE) {
        device->logicalDevice.freeMemory(resource.memory);
    }
    resource = {};
}

void SDFVolume::updateImageDescriptors(SDFImageResource& resource, vk::Sampler sampler)
{
    resource.storageDescriptor = vk::DescriptorImageInfo{
        VK_NULL_HANDLE,
        resource.view,
        vk::ImageLayout::eGeneral};
    resource.sampledDescriptor = vk::DescriptorImageInfo{
        sampler,
        resource.view,
        vk::ImageLayout::eGeneral};
}

void SDFVolume::create(vkm::VKMDevice* inDevice, const SDFVolumeDesc& inDesc)
{
    destroy();
    if (inDevice == nullptr || voxelCount(inDesc) == 0u) {
        OutputMessage("[SDF] Invalid SDFVolume create request\n");
        return;
    }

    const vk::PhysicalDeviceProperties properties = inDevice->physicalDevice.getProperties();
    const uint32_t max3D = properties.limits.maxImageDimension3D;
    if (inDesc.resolution.x > max3D || inDesc.resolution.y > max3D || inDesc.resolution.z > max3D) {
        OutputMessage("[SDF] Resolution {}x{}x{} exceeds max 3D image dimension {}\n",
                      inDesc.resolution.x,
                      inDesc.resolution.y,
                      inDesc.resolution.z,
                      max3D);
        return;
    }

    device = inDevice;
    desc = inDesc;
    const glm::vec3 gridExtent = glm::vec3(desc.resolution) * desc.voxelSize;
    if (desc.maxDistance <= 0.0f) {
        desc.maxDistance = maxComponent(gridExtent);
    }
    if (desc.narrowBandDistance <= 0.0f) {
        desc.narrowBandDistance = glm::length(desc.voxelSize) * 1.25f;
    }
    cpuDistances.assign(voxelCount(desc), desc.maxDistance);

    vk::SamplerCreateInfo nearestSamplerCreateInfo{};
    nearestSamplerCreateInfo.setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatOpaqueBlack);
    VK_CHECK_RESULT(device->logicalDevice.createSampler(&nearestSamplerCreateInfo, nullptr, &nearestSampler));

    vk::SamplerCreateInfo linearSamplerCreateInfo = nearestSamplerCreateInfo;
    linearSamplerCreateInfo.setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear);
    VK_CHECK_RESULT(device->logicalDevice.createSampler(&linearSamplerCreateInfo, nullptr, &linearSampler));

    const vk::ImageUsageFlags commonUsage =
        vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eTransferSrc |
        vk::ImageUsageFlagBits::eTransferDst;
    createImageResource(surfaceSeed, vk::Format::eR32G32B32A32Sfloat, commonUsage, "surface seed");
    createImageResource(seedPing, vk::Format::eR32G32B32A32Sfloat, commonUsage, "JFA ping seed");
    createImageResource(seedPong, vk::Format::eR32G32B32A32Sfloat, commonUsage, "JFA pong seed");
    createImageResource(finalDistance, vk::Format::eR32Sfloat, commonUsage, "final distance");
    updateImageDescriptors(finalDistance, linearSampler);

    created = true;
    hasCpuDistanceData = false;
    OutputMessage("[SDF] Volume origin=({}, {}, {}) voxelSize=({}, {}, {}) maxDistance={} narrowBand={}\n",
                  desc.origin.x,
                  desc.origin.y,
                  desc.origin.z,
                  desc.voxelSize.x,
                  desc.voxelSize.y,
                  desc.voxelSize.z,
                  desc.maxDistance,
                  desc.narrowBandDistance);
}

void SDFVolume::destroy()
{
    if (device != nullptr) {
        destroyImageResource(finalDistance);
        destroyImageResource(seedPong);
        destroyImageResource(seedPing);
        destroyImageResource(surfaceSeed);
        if (linearSampler != VK_NULL_HANDLE) {
            device->logicalDevice.destroySampler(linearSampler);
        }
        if (nearestSampler != VK_NULL_HANDLE) {
            device->logicalDevice.destroySampler(nearestSampler);
        }
    }
    nearestSampler = VK_NULL_HANDLE;
    linearSampler = VK_NULL_HANDLE;
    cpuDistances.clear();
    created = false;
    hasCpuDistanceData = false;
    device = nullptr;
}

void SDFVolume::uploadDistanceDataCPU(const std::vector<float>& distances, vk::Queue transferQueue)
{
    if (!created || device == nullptr || distances.size() != cpuDistances.size()) {
        OutputMessage("[SDF] uploadDistanceDataCPU size mismatch\n");
        return;
    }

    cpuDistances = distances;
    hasCpuDistanceData = true;
    const vk::DeviceSize uploadSize = static_cast<vk::DeviceSize>(cpuDistances.size() * sizeof(float));
    vk::Buffer stagingBuffer{VK_NULL_HANDLE};
    vk::DeviceMemory stagingMemory{VK_NULL_HANDLE};
    VK_CHECK_RESULT(device->createBuffer(
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        uploadSize,
        &stagingBuffer,
        &stagingMemory,
        const_cast<float*>(cpuDistances.data())));

    vk::CommandBuffer commandBuffer = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
    vkm::tools::setImageLayout(
        commandBuffer,
        finalDistance.image,
        finalDistance.layout,
        vk::ImageLayout::eTransferDstOptimal,
        colorRange());

    vk::ImageSubresourceLayers layers{};
    layers.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
    vk::BufferImageCopy copyRegion{};
    copyRegion.setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(layers)
        .setImageOffset(vk::Offset3D{0, 0, 0})
        .setImageExtent(vk::Extent3D{desc.resolution.x, desc.resolution.y, desc.resolution.z});
    commandBuffer.copyBufferToImage(stagingBuffer, finalDistance.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

    vkm::tools::setImageLayout(
        commandBuffer,
        finalDistance.image,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eGeneral,
        colorRange());
    finalDistance.layout = vk::ImageLayout::eGeneral;
    updateImageDescriptors(finalDistance, linearSampler);
    device->flushCommandBuffer(commandBuffer, transferQueue, true);
    device->logicalDevice.destroyBuffer(stagingBuffer);
    device->logicalDevice.freeMemory(stagingMemory);
}

float SDFVolume::sampleWorld(const glm::vec3& worldPosition) const
{
    if (!created || !hasCpuDistanceData || cpuDistances.empty()) {
        return desc.maxDistance;
    }

    const glm::vec3 local = (worldPosition - desc.origin) / desc.voxelSize;
    const glm::vec3 clampedLocal = glm::clamp(
        local,
        glm::vec3(0.0f),
        glm::vec3(desc.resolution) - glm::vec3(1.001f));
    glm::uvec3 base = glm::uvec3(glm::floor(clampedLocal));
    const glm::uvec3 maxBase = glm::max(desc.resolution - glm::uvec3(2u), glm::uvec3(0u));
    base = glm::min(base, maxBase);
    const glm::vec3 f = glm::clamp(clampedLocal - glm::vec3(base), glm::vec3(0.0f), glm::vec3(1.0f));

    const auto load = [&](uint32_t dx, uint32_t dy, uint32_t dz) {
        const glm::uvec3 coord = glm::min(base + glm::uvec3(dx, dy, dz), desc.resolution - glm::uvec3(1u));
        return cpuDistances[voxelIndex(coord, desc.resolution)];
    };

    const float c00 = lerp(load(0u, 0u, 0u), load(1u, 0u, 0u), f.x);
    const float c10 = lerp(load(0u, 1u, 0u), load(1u, 1u, 0u), f.x);
    const float c01 = lerp(load(0u, 0u, 1u), load(1u, 0u, 1u), f.x);
    const float c11 = lerp(load(0u, 1u, 1u), load(1u, 1u, 1u), f.x);
    const float c0 = lerp(c00, c10, f.y);
    const float c1 = lerp(c01, c11, f.y);
    return lerp(c0, c1, f.z);
}

std::vector<float> SDFVolume::readFinalDistanceData(vk::Queue transferQueue) const
{
    std::vector<float> distances(voxelCount(desc), desc.maxDistance);
    if (!created || device == nullptr || finalDistance.image == VK_NULL_HANDLE) {
        return distances;
    }

    const vk::DeviceSize readbackSize = static_cast<vk::DeviceSize>(distances.size() * sizeof(float));
    vk::Buffer readbackBuffer{VK_NULL_HANDLE};
    vk::DeviceMemory readbackMemory{VK_NULL_HANDLE};
    VK_CHECK_RESULT(device->createBuffer(
        vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        readbackSize,
        &readbackBuffer,
        &readbackMemory));

    vk::CommandBuffer commandBuffer = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

    std::array<vk::ImageMemoryBarrier, 1> barriers{};
    barriers[0].setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(finalDistance.image)
        .setSubresourceRange(colorRange());
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eTransfer,
        {},
        {},
        {},
        barriers);

    vk::ImageSubresourceLayers layers{};
    layers.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
    vk::BufferImageCopy copyRegion{};
    copyRegion.setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(layers)
        .setImageOffset(vk::Offset3D{0, 0, 0})
        .setImageExtent(vk::Extent3D{desc.resolution.x, desc.resolution.y, desc.resolution.z});

    // This readback is deliberately explicit and slow-path. It is for SDF slice
    // debugging and validation, while normal DDGI relocation samples the final
    // distance image directly on GPU.
    commandBuffer.copyImageToBuffer(finalDistance.image, vk::ImageLayout::eGeneral, readbackBuffer, copyRegion);
    std::array<vk::BufferMemoryBarrier, 1> bufferBarriers{};
    bufferBarriers[0].setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eHostRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setBuffer(readbackBuffer)
        .setOffset(0)
        .setSize(readbackSize);
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eHost,
        {},
        {},
        bufferBarriers,
        {});
    device->flushCommandBuffer(commandBuffer, transferQueue, true);

    void* mapped = nullptr;
    VK_CHECK_RESULT(device->logicalDevice.mapMemory(readbackMemory, 0, readbackSize, {}, &mapped));
    std::memcpy(distances.data(), mapped, static_cast<size_t>(readbackSize));
    device->logicalDevice.unmapMemory(readbackMemory);
    device->logicalDevice.destroyBuffer(readbackBuffer);
    device->logicalDevice.freeMemory(readbackMemory);
    return distances;
}

void SDFVolume::recordTransitionAllToGeneral(vk::CommandBuffer commandBuffer)
{
    if (!created) {
        return;
    }

    std::array<SDFImageResource*, 4> resources{
        &surfaceSeed,
        &seedPing,
        &seedPong,
        &finalDistance,
    };
    std::array<vk::ImageMemoryBarrier, 4> barriers{};
    for (size_t resourceIndex = 0; resourceIndex < resources.size(); ++resourceIndex) {
        SDFImageResource& resource = *resources[resourceIndex];
        barriers[resourceIndex].setOldLayout(resource.layout)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eNone)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(resource.image)
            .setSubresourceRange(colorRange());
        resource.layout = vk::ImageLayout::eGeneral;
    }

    // The generator keeps every SDF image in General layout. That is less
    // optimal than per-pass layout bouncing, but avoids descriptor churn: the
    // same views are legal as storage images during generation and as sampled
    // images during DDGI relocation/classification.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader,
        {},
        {},
        {},
        barriers);
}

void SDFVolume::recordFinalDistanceShaderReadBarrier(vk::CommandBuffer commandBuffer) const
{
    if (!created) {
        return;
    }

    std::array<vk::ImageMemoryBarrier, 1> barriers{};
    barriers[0].setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(finalDistance.image)
        .setSubresourceRange(colorRange());

    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader,
        {},
        {},
        {},
        barriers);
}

const SDFImageResource& SDFVolume::seedImage(SDFSeedImage image) const
{
    switch (image) {
    case SDFSeedImage::Surface:
        return surfaceSeed;
    case SDFSeedImage::Ping:
        return seedPing;
    case SDFSeedImage::Pong:
        return seedPong;
    default:
        return surfaceSeed;
    }
}

} // namespace sdf
