#pragma once

#include "ddgi/DDGIVolume.h"
#include "sdf/SDFVolume.h"

namespace sdf {

class ProbeSDFUpdater {
public:
    void record(vk::CommandBuffer commandBuffer, ddgi::DDGIVolume& volume, const SDFVolume& sdfVolume);
};

} // namespace sdf
