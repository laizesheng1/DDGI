#include "rt/ShaderBindingTable.h"

#include <array>
#include <cstring>
#include <vector>

namespace rt {
namespace {

uint32_t alignUp(uint32_t value, uint32_t alignment)
{
    if (alignment == 0u) {
        return value;
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
}

vk::DeviceAddress alignUpAddress(vk::DeviceAddress value, vk::DeviceAddress alignment)
{
    if (alignment == 0u) {
        return value;
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
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
    vk::BufferDeviceAddressInfo addressInfo{};
    addressInfo.setBuffer(buffer);
    return logicalDevice.getBufferAddress(&addressInfo);
}

} // namespace

void ShaderBindingTable::create(vkm::VKMDevice* inDevice,
                                vk::Pipeline pipeline,
                                const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& rtProperties)
{
    destroy();
    if (inDevice == nullptr) {
        OutputMessage("[RT] ShaderBindingTable::create received a null device\n");
        return;
    }
    if (pipeline == VK_NULL_HANDLE) {
        OutputMessage("[RT] ShaderBindingTable::create requires a valid ray tracing pipeline\n");
        return;
    }

    device = inDevice;
    handleSize = rtProperties.shaderGroupHandleSize;
    handleStride = alignUp(handleSize, rtProperties.shaderGroupHandleAlignment);
    const uint32_t groupCount = 3u;
    const uint32_t baseAlignedRecordSize = alignUp(handleStride, rtProperties.shaderGroupBaseAlignment);
    const uint32_t worstCaseBasePadding = rtProperties.shaderGroupBaseAlignment;
    tableSizeBytes = worstCaseBasePadding + baseAlignedRecordSize * groupCount;

    std::vector<uint8_t> shaderGroupHandles(static_cast<size_t>(handleSize) * groupCount);
    const VkResult rawResult = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetRayTracingShaderGroupHandlesKHR(
        static_cast<VkDevice>(device->logicalDevice),
        static_cast<VkPipeline>(pipeline),
        0,
        groupCount,
        shaderGroupHandles.size(),
        shaderGroupHandles.data());
    VK_CHECK_RESULT(static_cast<vk::Result>(rawResult));

    prepareBufferForCreate(
        tableBuffer,
        vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    tableBuffer.createBuffer(
        tableSizeBytes,
        vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        nullptr,
        device);
    tableBuffer.deviceAddress = getBufferDeviceAddress(device->logicalDevice, tableBuffer.buffer);

    const vk::DeviceAddress baseAddress = tableBuffer.deviceAddress;
    const vk::DeviceAddress alignedAddress = alignUpAddress(baseAddress, rtProperties.shaderGroupBaseAlignment);
    const uint32_t firstRecordOffset = static_cast<uint32_t>(alignedAddress - baseAddress);

    // SBT record starts must honor shaderGroupBaseAlignment. Each region keeps a
    // single handle record for this first DDGI bring-up path: raygen, miss, hit.
    std::array<uint32_t, 3> recordOffsets{
        firstRecordOffset,
        firstRecordOffset + baseAlignedRecordSize,
        firstRecordOffset + baseAlignedRecordSize * 2u,
    };

    VK_CHECK_RESULT(tableBuffer.map(tableSizeBytes));
    std::memset(tableBuffer.mapped, 0, tableSizeBytes);
    for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        std::memcpy(
            static_cast<uint8_t*>(tableBuffer.mapped) + recordOffsets[groupIndex],
            shaderGroupHandles.data() + static_cast<size_t>(groupIndex) * handleSize,
            handleSize);
    }
    tableBuffer.unmap();

    regions.raygen.setDeviceAddress(baseAddress + recordOffsets[0]).setStride(handleStride).setSize(handleStride);
    regions.miss.setDeviceAddress(baseAddress + recordOffsets[1]).setStride(handleStride).setSize(handleStride);
    regions.hit.setDeviceAddress(baseAddress + recordOffsets[2]).setStride(handleStride).setSize(handleStride);
    regions.callable = vk::StridedDeviceAddressRegionKHR(0, 0, 0);
}

void ShaderBindingTable::destroy()
{
    tableBuffer.destroy();
    regions = {};
    handleSize = 0;
    handleStride = 0;
    tableSizeBytes = 0;
    device = nullptr;
}

} // namespace rt
