#pragma once

#include "VK_Base.h"
#include "ddgi/DDGIVolume.h"
#include "debug/DebugUI.h"
#include "debug/ProbeVisualizer.h"
#include "debug/TextureVisualizer.h"
#include "renderer/Renderer.h"
#include "rt/RayTracingContext.h"
#include "scene/Scene.h"
#include "sdf/SDFVolume.h"

namespace app {

class DDGISample final : public VKM_Base {
private:
    /**
     * Record one frame of DDGI update, scene rendering, debug visualization,
     * and UI into an already reset primary command buffer.
     */
    void recordFrame(vk::CommandBuffer commandBuffer);

    scene::Scene scene;
    renderer::Renderer renderer;
    rt::RayTracingContext rayTracing;
    sdf::SDFVolume sdfVolume;
    ddgi::DDGIVolume ddgiVolume;
    debug::ProbeVisualizer probeVisualizer;
    debug::TextureVisualizer textureVisualizer;
    debug::DebugUI debugUi;
    debug::DebugUIState debugState;
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
    vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};

public:
    DDGISample();
    ~DDGISample() override;

    /**
     * Create base Vulkan objects, load the Sponza test scene, build scene
     * acceleration data, and create DDGI/debug resources.
     */
    void prepare() override;

    /**
     * Acquire a swapchain image, record commands for the current frame, and
     * submit the frame to the graphics queue.
     */
    void render() override;

    /**
     * Draw DDGI debug controls into the existing HUD instance.
     * ui must be non-null and owned by VKM_Base for the current frame.
     */
    void OnUpdateHUD(vkm::HUD* ui) override;

    /**
     * Enable Vulkan ray tracing device extensions and feature pNext chain when
     * the selected physical device supports them.
     */
    void getEnabledExtensions() override;
};

} // namespace app
