#pragma once

#include "Buffer.h"
#include "VKMDevice.h"
#include "vulkan/vulkan.hpp"

namespace rt {

struct ShaderBindingTableRegions {
    vk::StridedDeviceAddressRegionKHR raygen{};
    vk::StridedDeviceAddressRegionKHR miss{};
    vk::StridedDeviceAddressRegionKHR hit{};
    vk::StridedDeviceAddressRegionKHR callable{};
};

class ShaderBindingTable {
private:
    vkm::VKMDevice* device{nullptr};
    vkm::Buffer tableBuffer{};
    ShaderBindingTableRegions regions{};
    uint32_t handleSize{0};
    uint32_t handleStride{0};
    uint32_t tableSizeBytes{0};

public:
    /**
     * Fetch ray tracing shader group handles from pipeline and build a compact
     * SBT buffer containing one raygen, one miss, and one hit record.
     * rtProperties provides handle size/stride/base alignment limits for the
     * active physical device.
     */
    void create(vkm::VKMDevice* device,
                vk::Pipeline pipeline,
                const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& rtProperties);

    /**
     * Release the SBT buffer and reset its strided regions.
     */
    void destroy();

    [[nodiscard]] bool isCreated() const { return tableBuffer.buffer != VK_NULL_HANDLE; }
    [[nodiscard]] const ShaderBindingTableRegions& stridedRegions() const { return regions; }
    [[nodiscard]] uint32_t shaderGroupHandleSize() const { return handleSize; }
    [[nodiscard]] uint32_t shaderGroupHandleStride() const { return handleStride; }
    [[nodiscard]] uint32_t sizeBytes() const { return tableSizeBytes; }
};

} // namespace rt
