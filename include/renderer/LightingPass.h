#pragma once

#include "ddgi/DDGIVolume.h"

namespace renderer {

class LightingPass {
public:
    void record(vk::CommandBuffer commandBuffer, const ddgi::DDGIVolume& volume);
};

} // namespace renderer
