#pragma once

#include <vector>

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
    bool clearRequested{false};

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
     * Request that the next recorded DDGI frame clears probe ray data, offsets,
     * states, and atlas history before tracing/updating. The clear is deferred
     * until command recording so atlas image clears remain synchronized with
     * the rest of the DDGI GPU work.
     */
    void requestClearProbes();

    /**
     * Record probe ray tracing commands when an RT pipeline, TLAS, scene
     * descriptor set, and SBT are all available.
     */
    void traceProbeRays(vk::CommandBuffer commandBuffer, const rt::RayTracingScene& scene);

    /**
     * Record classification, relocation, irradiance/depth accumulation, and
     * atlas border copy compute dispatches. Atlas update shaders only rewrite
     * the probes assigned to the current interleaved update phase so the
     * volume can amortize work across multiple frames.
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

    /**
     * Copy probe debug state from host-visible DDGI buffers.
     * averageRadiance becomes one RGB value per probe by averaging the traced
     * per-ray radiance buffer. This is a debug approximation chosen because the
     * traced ray buffer is already host visible, while the atlas images are
     * device-local storage images.
     */
    bool readProbeDebugData(std::vector<glm::vec3>& averageRadiance,
                            std::vector<glm::vec3>& localOffsets,
                            std::vector<uint32_t>& states) const;

    /**
     * Update one probe's local relocation offset in the host-visible debug
     * buffer. The next trace/update pass consumes the modified offset buffer on
     * GPU, so manual edits immediately affect both visualization and DDGI.
     */
    bool writeProbeOffset(uint32_t probeIndex, const glm::vec3& localOffset);

    /**
     * Convert a flat probe index into its current world-space position. The
     * optional local offset is added on top of the regular grid location so
     * relocation and manual edits are visible in the probe debug renderer.
     */
    [[nodiscard]] glm::vec3 probeWorldPosition(uint32_t probeIndex, const glm::vec3* localOffset = nullptr) const;

    [[nodiscard]] uint32_t totalProbeCount() const { return resourceSet.totalProbeCount(); }
    [[nodiscard]] const DDGIVolumeDesc& description() const { return desc; }
    [[nodiscard]] const DDGIResources& resources() const { return resourceSet; }
    [[nodiscard]] const DDGIPipeline& pipelines() const { return pipelineSet; }
};

} // namespace ddgi
