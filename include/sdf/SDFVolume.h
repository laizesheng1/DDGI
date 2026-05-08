#pragma once

#include "VKMDevice.h"
#include "vulkan/vulkan.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace sdf {

struct SDFVolumeDesc {
    glm::vec3 origin{0.0f};
    glm::vec3 voxelSize{0.25f};
    glm::uvec3 resolution{64u};
    float maxDistance{64.0f};
    float narrowBandDistance{0.0f};
};

enum class SDFSeedImage : uint32_t {
    Surface = 0,
    Ping = 1,
    Pong = 2
};

struct SDFImageResource {
    vk::Image image{VK_NULL_HANDLE};
    vk::DeviceMemory memory{VK_NULL_HANDLE};
    vk::ImageView view{VK_NULL_HANDLE};
    vk::DescriptorImageInfo storageDescriptor{};
    vk::DescriptorImageInfo sampledDescriptor{};
    vk::Format format{vk::Format::eUndefined};
    vk::ImageLayout layout{vk::ImageLayout::eUndefined};
};

class SDFVolume {
private:
    vkm::VKMDevice* device{nullptr};
    SDFVolumeDesc desc{};
    SDFImageResource surfaceSeed{};
    SDFImageResource seedPing{};
    SDFImageResource seedPong{};
    SDFImageResource finalDistance{};
    vk::Sampler nearestSampler{VK_NULL_HANDLE};
    vk::Sampler linearSampler{VK_NULL_HANDLE};
    std::vector<float> cpuDistances{};
    bool created{false};
    bool hasCpuDistanceData{false};

    void createImageResource(SDFImageResource& resource,
                             vk::Format format,
                             vk::ImageUsageFlags usage,
                             const char* debugName);
    void destroyImageResource(SDFImageResource& resource);
    void updateImageDescriptors(SDFImageResource& resource, vk::Sampler sampler);

public:
    void create(vkm::VKMDevice* device, const SDFVolumeDesc& desc);
    void destroy();

    /**
     * CPU fallback upload path. The default project path now generates the SDF
     * on GPU, but keeping this explicitly named method is useful for debug
     * readbacks and for machines where compute SDF generation is disabled.
     */
    void uploadDistanceDataCPU(const std::vector<float>& distances, vk::Queue transferQueue);
    void uploadSignedDistanceData(const std::vector<float>& distances, vk::Queue transferQueue)
    {
        uploadDistanceDataCPU(distances, transferQueue);
    }

    /**
     * CPU trilinear sample for debug/fallback only. DDGI relocation samples the
     * GPU final-distance image directly so the probe state seen by shaders is
     * generated from the same image layout and descriptor used by JFA.
     */
    [[nodiscard]] float sampleWorld(const glm::vec3& worldPosition) const;
    [[nodiscard]] std::vector<float> readFinalDistanceData(vk::Queue transferQueue) const;

    void recordTransitionAllToGeneral(vk::CommandBuffer commandBuffer);
    void recordFinalDistanceShaderReadBarrier(vk::CommandBuffer commandBuffer) const;

    [[nodiscard]] const SDFVolumeDesc& description() const { return desc; }
    [[nodiscard]] const SDFImageResource& seedImage(SDFSeedImage image) const;
    [[nodiscard]] const SDFImageResource& finalDistanceImage() const { return finalDistance; }
    [[nodiscard]] const vk::DescriptorImageInfo& finalDistanceDescriptor() const { return finalDistance.sampledDescriptor; }
    [[nodiscard]] const vk::DescriptorImageInfo& finalDistanceStorageDescriptor() const { return finalDistance.storageDescriptor; }
    [[nodiscard]] bool isCreated() const { return created; }
    [[nodiscard]] bool hasCpuData() const { return hasCpuDistanceData; }
};

} // namespace sdf
