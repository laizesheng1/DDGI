#include "app/DDGISample.h"

#include <array>

namespace app {

DDGISample::DDGISample()
{
    displayWindows.title = "DDGI Vulkan Sample";
    displayWindows.name = "DDGI";
}

DDGISample::~DDGISample()
{
    textureVisualizer.destroy();
    probeVisualizer.destroy();
    ddgiVolume.destroy();
    sdfVolume.destroy();
    rayTracing.destroy();
    renderer.destroy();
    scene.clear();
}

void DDGISample::prepare()
{
    VKM_Base::prepare();

    renderer.create(VKMDevice, pipelineCache, renderPass);
    rayTracing.create(VKMDevice);

    ddgi::DDGIVolumeDesc volumeDesc{};
    ddgiVolume.create(VKMDevice, volumeDesc);

    sdf::SDFVolumeDesc sdfDesc{};
    sdfVolume.create(VKMDevice, sdfDesc);

    probeVisualizer.create(VKMDevice, renderPass, pipelineCache);
    textureVisualizer.create(VKMDevice, renderPass, pipelineCache);

    displayWindows.camera.type = Camera::firstperson;
    displayWindows.camera.setPerspective(60.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 256.0f);
    displayWindows.camera.setPosition(glm::vec3(0.0f, 2.0f, -8.0f));
    displayWindows.prepared = true;
}

void DDGISample::render()
{
    if (!displayWindows.prepared) {
        return;
    }

    prepareFrame();
    auto commandBuffer = drawCmdBuffers[currentBuffer];
    commandBuffer.reset();
    recordFrame(commandBuffer);
    submitFrame();
}

void DDGISample::OnUpdateHUD(vkm::HUD* ui)
{
    debugUi.draw(ui, debugState);
}

void DDGISample::getEnabledExtensions()
{
    // Vulkan ray tracing extensions are enabled after capability checks are added.
}

void DDGISample::recordFrame(vk::CommandBuffer commandBuffer)
{
    ddgiVolume.updateConstants(displayWindows.camera, currentBuffer);
    ddgiVolume.updateProbesFromSDF(commandBuffer, sdfVolume);
    ddgiVolume.traceProbeRays(commandBuffer, rayTracing.sceneBinding());
    ddgiVolume.updateProbes(commandBuffer);

    vk::CommandBufferBeginInfo beginInfo{};
    VK_CHECK_RESULT(commandBuffer.begin(&beginInfo));

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].setColor(vk::ClearColorValue(std::array<float, 4>{0.02f, 0.025f, 0.03f, 1.0f}));
    clearValues[1].setDepthStencil(vk::ClearDepthStencilValue(1.0f, 0));

    vk::Rect2D renderArea{};
    renderArea.setOffset(vk::Offset2D{0, 0});
    renderArea.setExtent(vk::Extent2D{width, height});

    vk::RenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.setRenderPass(renderPass)
        .setFramebuffer(frameBuffers[currentImageIndex])
        .setRenderArea(renderArea)
        .setClearValues(clearValues);

    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
    renderer.drawScene(commandBuffer, scene, &ddgiVolume);

    if (debugState.showProbes) {
        probeVisualizer.draw(commandBuffer, ddgiVolume);
    }
    if (debugState.showTexturePanel) {
        textureVisualizer.draw(commandBuffer, ddgiVolume, debugState.selectedTexture);
    }

    drawUI(commandBuffer);
    commandBuffer.endRenderPass();
    commandBuffer.end();
}

} // namespace app
