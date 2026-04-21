#include "sdf/ProbeSDFUpdater.h"

namespace sdf {

void ProbeSDFUpdater::record(vk::CommandBuffer commandBuffer, ddgi::DDGIVolume& volume, const SDFVolume& sdfVolume)
{
    volume.updateProbesFromSDF(commandBuffer, sdfVolume);
}

} // namespace sdf
