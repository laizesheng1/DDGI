#include "ddgi/DDGIPasses.h"

namespace ddgi {

void ProbeRayTracePass::record(vk::CommandBuffer commandBuffer, DDGIVolume& volume, const rt::RayTracingScene& scene)
{
    volume.traceProbeRays(commandBuffer, scene);
}

void ProbeUpdatePass::record(vk::CommandBuffer commandBuffer, DDGIVolume& volume)
{
    volume.updateProbes(commandBuffer);
}

void ProbeRelocationPass::record(vk::CommandBuffer, DDGIVolume&)
{
    // Relocation is scheduled inside DDGIVolume::updateProbes() so it shares
    // barriers and descriptors with classification and atlas updates.
}

void ProbeClassificationPass::record(vk::CommandBuffer, DDGIVolume&)
{
    // Classification is scheduled inside DDGIVolume::updateProbes() so probe
    // state writes are synchronized before atlas accumulation.
}

} // namespace ddgi
