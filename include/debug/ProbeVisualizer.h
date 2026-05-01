#pragma once

#include <vector>

#include "Buffer.h"
#include "camera.hpp"
#include "ddgi/DDGIVolume.h"

namespace debug {

struct ProbeVisualizerSphereVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
};

struct ProbeVisualizerInstanceData {
    glm::vec4 positionAndRadius{0.0f};
    glm::vec4 color{1.0f};
};

class ProbeVisualizer {
private:
    vkm::VKMDevice* device{nullptr};
    vkm::Buffer sphereVertexBuffer{};
    vkm::Buffer sphereIndexBuffer{};
    vkm::Buffer instanceBuffer{};
    vk::PipelineLayout pipelineLayoutHandle{VK_NULL_HANDLE};
    vk::Pipeline pipelineHandle{VK_NULL_HANDLE};
    uint32_t sphereIndexCount{0u};
    uint32_t instanceCapacity{0u};
    uint32_t cachedProbeCount{0u};
    std::vector<ProbeVisualizerInstanceData> instanceCpuData{};
    ddgi::DDGIVolumeDesc cachedVolumeDesc{};
    bool instanceDataDirty{true};

public:
    /**
     * Create the instanced sphere pipeline used on top of the lit scene. A
     * low-poly sphere mesh keeps the debug path easy to maintain while still
     * making relocated probes look like real volume samples instead of quads.
     */
    void create(vkm::VKMDevice* device, vk::RenderPass renderPass, vk::PipelineCache pipelineCache);

    /**
     * Release graphics state used by the probe overlay.
     */
    void destroy();

    /**
     * Draw one sphere per probe using a single instanced indexed draw. This
     * debug path intentionally keeps instance data static unless the probe
     * lattice changes, because per-frame CPU readback of radiance/offset
     * buffers quickly dominates the cost once the scene contains hundreds of
     * probes.
     */
    void draw(vk::CommandBuffer commandBuffer,
              const ddgi::DDGIVolume& volume,
              const Camera& camera,
              vk::Extent2D framebufferExtent);
};

} // namespace debug
