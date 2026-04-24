#pragma once

#include "Buffer.h"
#include "camera.hpp"
#include "ddgi/DDGIPipeline.h"
#include "ddgi/DDGIResources.h"
#include "rt/RayTracingContext.h"
#include "rt/ShaderBindingTable.h"
#include "sdf/SDFVolume.h"

namespace ddgi {

class DDGIVolume {
private:
    vkm::VKMDevice* device{nullptr};
    DDGIVolumeDesc desc{};
    DDGIFrameConstants constants{};
    DDGIResources resourceSet{};
    DDGIPipeline pipelineSet{};
    rt::ShaderBindingTable traceShaderBindingTable{};
    vk::DescriptorPool rtSceneDescriptorPool{VK_NULL_HANDLE};
    vk::DescriptorSet rtSceneDescriptorSet{VK_NULL_HANDLE};
    vk::AccelerationStructureKHR boundTopLevelAccelerationStructure{VK_NULL_HANDLE};

public:
    /**
     * Create DDGI resource and pipeline state for one probe volume.
     * pipelineCache is reused for compute/RT pipeline creation.
     */
    void create(vkm::VKMDevice* device, vk::PipelineCache pipelineCache, const DDGIVolumeDesc& desc);

    /**
     * Destroy DDGI pipelines before the resources they reference.
     */
    void destroy();

    /**
     * Fill and upload DDGIFrameConstants from the active camera.
     * frameIndex is used by shaders as a deterministic temporal seed.
     */
    void updateConstants(const Camera& camera, uint32_t frameIndex);

    /**
     * Record probe ray tracing commands when an RT pipeline, TLAS, scene
     * descriptor set, and SBT are all available.
     */
    void traceProbeRays(vk::CommandBuffer commandBuffer, const rt::RayTracingScene& scene);

    /**
     * Record classification, relocation, irradiance/depth accumulation, and
     * atlas border copy compute dispatches.
     */
    void updateProbes(vk::CommandBuffer commandBuffer);

    /**
     * Record the SDF-driven probe metadata update. The current SDF pass clamps
     * offsets until SDFVolume exposes a GPU texture descriptor.
     */
    void updateProbesFromSDF(vk::CommandBuffer commandBuffer, const sdf::SDFVolume& sdfVolume);

    /**
     * Bind DDGI descriptor set for a graphics lighting pipeline.
     * pipelineLayout must contain DDGIResources::descriptorSetLayout at setIndex.
     */
    void bindForLighting(vk::CommandBuffer commandBuffer, vk::PipelineLayout pipelineLayout, uint32_t setIndex) const;

    [[nodiscard]] const DDGIVolumeDesc& description() const { return desc; }
    [[nodiscard]] const DDGIResources& resources() const { return resourceSet; }
    [[nodiscard]] const DDGIPipeline& pipelines() const { return pipelineSet; }
};

} // namespace ddgi
