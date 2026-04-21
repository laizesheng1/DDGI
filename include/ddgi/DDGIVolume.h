#pragma once

#include "Buffer.h"
#include "camera.hpp"
#include "ddgi/DDGIResources.h"
#include "rt/RayTracingContext.h"
#include "sdf/SDFVolume.h"

namespace ddgi {

class DDGIVolume {
public:
    void create(vkm::VKMDevice* device, const DDGIVolumeDesc& desc);
    void destroy();

    void updateConstants(const Camera& camera, uint32_t frameIndex);
    void traceProbeRays(vk::CommandBuffer commandBuffer, const rt::RayTracingScene& scene);
    void updateProbes(vk::CommandBuffer commandBuffer);
    void updateProbesFromSDF(vk::CommandBuffer commandBuffer, const sdf::SDFVolume& sdfVolume);
    void bindForLighting(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout, uint32_t setIndex) const;

    [[nodiscard]] const DDGIVolumeDesc& description() const { return desc; }
    [[nodiscard]] const DDGIResources& resources() const { return resourceSet; }

private:
    vkm::VKMDevice* device{nullptr};
    DDGIVolumeDesc desc{};
    DDGIFrameConstants constants{};
    DDGIResources resourceSet{};
    vkm::Buffer constantsBuffer{};
    vkm::Buffer probeRayData{};
    vkm::Buffer probeOffsets{};
    vkm::Buffer probeStates{};
};

} // namespace ddgi
