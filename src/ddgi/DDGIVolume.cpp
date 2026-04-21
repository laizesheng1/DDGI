#include "ddgi/DDGIVolume.h"

namespace ddgi {

void DDGIVolume::create(vkm::VKMDevice* inDevice, const DDGIVolumeDesc& inDesc)
{
    device = inDevice;
    desc = inDesc;
    resourceSet.create(device, desc);
}

void DDGIVolume::destroy()
{
    probeStates.destroy();
    probeOffsets.destroy();
    probeRayData.destroy();
    constantsBuffer.destroy();
    resourceSet.destroy();
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
}

void DDGIVolume::traceProbeRays(vk::CommandBuffer, const rt::RayTracingScene&)
{
    // TODO: Dispatch Vulkan ray tracing or compute-BVH probe ray tracing.
}

void DDGIVolume::updateProbes(vk::CommandBuffer)
{
    // TODO: Accumulate probe ray data into irradiance, depth, and depth-squared atlases.
}

void DDGIVolume::updateProbesFromSDF(vk::CommandBuffer, const sdf::SDFVolume&)
{
    // TODO: Push probes out of invalid SDF regions and update probe state flags.
}

void DDGIVolume::bindForLighting(vk::CommandBuffer, vk::PipelineLayout, uint32_t) const
{
    // TODO: Bind DDGI descriptors for the lighting pass.
}

} // namespace ddgi
