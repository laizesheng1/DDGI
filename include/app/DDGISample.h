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
public:
    DDGISample();
    ~DDGISample() override;

    void prepare() override;
    void render() override;
    void OnUpdateHUD(vkm::HUD* ui) override;
    void getEnabledExtensions() override;

private:
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
};

} // namespace app
