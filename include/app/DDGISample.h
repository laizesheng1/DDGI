#pragma once

#include "VK_Base.h"
#include "ddgi/DDGIVolume.h"
#include "debug/DebugAtlasWindow.h"
#include "debug/DebugUI.h"
#include "debug/ProbeVisualizer.h"
#include "renderer/Renderer.h"
#include "rt/RayTracingContext.h"
#include "scene/Scene.h"
#include "sdf/SDFVolume.h"


namespace app {

class DDGISample final : public VKM_Base {
private:
    /**
     * Build a DDGI volume description from the current debug controls. In
     * UniformInSceneBounds mode the scene AABB is padded inward before probe
     * placement so probes start inside the playable space instead of sitting
     * directly on the boundary surfaces.
     */
    [[nodiscard]] ddgi::DDGIVolumeDesc buildVolumeDescFromDebugState() const;

    /**
     * Recreate DDGI resources and the renderer when probe layout changes.
     * Renderer recreation is required because its lighting pipeline layout
     * captures the DDGI descriptor set layout created by the volume.
     */
    void recreateDdgiVolumeAndRenderer(const ddgi::DDGIVolumeDesc& volumeDesc);

    /**
     * Apply pending HUD requests such as probe-layout rebuilds. These
     * operations wait for the device to idle because DDGI resources and
     * descriptor layouts are recreated under the hood.
     */
    void applyPendingDebugChanges();

    /**
     * Refresh probe-count and volume-origin debug state from the current DDGI
     * volume. The UI only exposes whole-volume movement now, so per-probe
     * state no longer needs to be mirrored into the HUD.
     */
    void syncDebugStateFromVolume();

    /**
     * Pull a coarse radiance summary back from DDGI buffers for HUD display.
     * The readback is intentionally throttled because host-visible debug reads
     * are much more expensive than the normal lighting path when probe counts
     * become large.
     */
    void updateRadianceDebugStats();

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
    debug::DebugAtlasWindow atlasWindow;
    debug::DebugUI debugUi;
    debug::DebugUIState debugState;
    ddgi::DDGIVolumeDesc manualVolumeDesc{};
    uint32_t frameCounter{0u};
    uint32_t radianceDebugFrameCounter{0u};
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
     * Recreate render-pass-dependent scene and probe debug pipelines after the
     * main window swapchain is resized by VKM_Base.
     */
    void windowHasResized() override;

    /**
     * Enable Vulkan ray tracing device extensions and feature pNext chain when
     * the selected physical device supports them.
     */
    void getEnabledExtensions() override;
};

} // namespace app
