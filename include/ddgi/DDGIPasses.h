#pragma once

#include "ddgi/DDGIVolume.h"

namespace ddgi {

class ProbeRayTracePass {
public:
    void record(vk::CommandBuffer commandBuffer, DDGIVolume& volume, const rt::RayTracingScene& scene);
};

class ProbeUpdatePass {
public:
    void record(vk::CommandBuffer commandBuffer, DDGIVolume& volume);
};

class ProbeRelocationPass {
public:
    void record(vk::CommandBuffer commandBuffer, DDGIVolume& volume);
};

class ProbeClassificationPass {
public:
    void record(vk::CommandBuffer commandBuffer, DDGIVolume& volume);
};

} // namespace ddgi
