#pragma once

#include "Buffer.h"
#include "ddgi/DDGITypes.h"
#include "VKMDevice.h"
#include "Texture.h"
#include "vulkan/vulkan.hpp"

namespace ddgi {

enum class DDGIResourceBinding : uint32_t {
    Constants = 0,
    ProbeRayData = 1,
    ProbeOffsets = 2,
    ProbeStates = 3,
    IrradianceAtlas = 4,
    DepthAtlas = 5,
    DepthSquaredAtlas = 6
};

/**
 * One traced ray result for a probe.
 * radianceAndDistance.xyz stores incoming radiance and .w stores hit distance.
 * directionAndDistanceSquared.xyz stores world ray direction and .w stores d^2.
 */
struct DDGIProbeRayGpuData {
    glm::vec4 radianceAndDistance{0.0f};
    glm::vec4 directionAndDistanceSquared{0.0f};
};

/**
 * Runtime DDGI atlas images and their packed tile layout.
 * Every probe owns one octahedral tile with a one-texel border.
 */
struct DDGITextureSet {
    vkm::Texture2D irradiance;
    vkm::Texture2D depth;
    vkm::Texture2D depthSquared;

    vk::Extent2D irradianceExtent{};
    vk::Extent2D depthExtent{};
    vk::Extent2D depthSquaredExtent{};
    uint32_t irradianceTileSize{0};
    uint32_t depthTileSize{0};
    uint32_t probeTileColumns{0};
    uint32_t probeTileRows{0};
};

class DDGIResources {
private:
    vkm::VKMDevice* device{nullptr};
    DDGITextureSet textureSet{};
    vkm::Buffer constantsBuffer{};
    vkm::Buffer probeRayDataBuffer{};
    vkm::Buffer probeOffsetsBuffer{};
    vkm::Buffer probeStatesBuffer{};
    vk::DescriptorPool descriptorPool{VK_NULL_HANDLE};
    vk::DescriptorSetLayout descriptorSetLayoutHandle{VK_NULL_HANDLE};
    vk::DescriptorSet descriptorSetHandle{VK_NULL_HANDLE};
    vk::DeviceSize probeRayDataBytes{0};
    vk::DeviceSize probeOffsetsBytes{0};
    vk::DeviceSize probeStatesBytes{0};
    uint32_t probeCount{0};
    bool created{false};
    bool needsInitialLayoutTransition{false};

public:
    /**
     * Allocate all DDGI images, buffers, and descriptors for a volume.
     * device must outlive this object; desc defines probe counts, ray count,
     * and octahedral atlas resolution.
     */
    void create(vkm::VKMDevice* device, const DDGIVolumeDesc& desc);

    /**
     * Release Vulkan resources in reverse ownership order.
     */
    void destroy();

    /**
     * Upload per-frame constants used by DDGI compute/RT/lighting shaders.
     */
    void updateConstants(const DDGIFrameConstants& constants);

    /**
     * Transition DDGI atlases from Undefined to General before shader access.
     * commandBuffer must be recording.
     */
    void recordInitialLayoutTransitions(vk::CommandBuffer commandBuffer);

    /**
     * Synchronize probeRayData shader writes before compute update reads it.
     * commandBuffer must be recording.
     */
    void recordProbeRayReadBarrier(vk::CommandBuffer commandBuffer) const;

    /**
     * Synchronize atlas shader writes before later shader reads/writes.
     * commandBuffer must be recording.
     */
    void recordAtlasWriteBarrier(vk::CommandBuffer commandBuffer) const;

    [[nodiscard]] bool isCreated() const { return created; }
    [[nodiscard]] const DDGITextureSet& textures() const { return textureSet; }
    [[nodiscard]] vk::DescriptorSetLayout descriptorSetLayout() const { return descriptorSetLayoutHandle; }
    [[nodiscard]] vk::DescriptorSet descriptorSet() const { return descriptorSetHandle; }
    [[nodiscard]] const vkm::Buffer& constants() const { return constantsBuffer; }
    [[nodiscard]] const vkm::Buffer& probeRayData() const { return probeRayDataBuffer; }
    [[nodiscard]] const vkm::Buffer& probeOffsets() const { return probeOffsetsBuffer; }
    [[nodiscard]] const vkm::Buffer& probeStates() const { return probeStatesBuffer; }
    [[nodiscard]] uint32_t totalProbeCount() const { return probeCount; }
};

} // namespace ddgi
