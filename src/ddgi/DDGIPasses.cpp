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
    // TODO: Implement probe relocation compute dispatch.
}

void ProbeClassificationPass::record(vk::CommandBuffer, DDGIVolume&)
{
    // TODO: Implement probe classification compute dispatch.
}

} // namespace ddgi
