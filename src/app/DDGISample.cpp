#include "app/DDGISample.h"

#include <array>
#include <string>

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

    rayTracing.create(VKMDevice);

    const std::string sponzaScenePath = vkm::tools::getAssetPath() + "models/sponza/sponza.gltf";
    scene.loadFromFile(VKMDevice, sponzaScenePath, queue);
    rayTracing.buildScene(scene, queue);
    OutputMessage("[DDGI] Loaded test scene: {} ({} mesh primitives)\n",
                  sponzaScenePath,
                  static_cast<uint32_t>(scene.meshes().size()));

    ddgi::DDGIVolumeDesc volumeDesc{};
    ddgiVolume.create(VKMDevice, pipelineCache, volumeDesc);
    renderer.create(
        VKMDevice,
        pipelineCache,
        renderPass,
        depthFormat,
        vk::Extent2D{width, height},
        ddgiVolume.resources().descriptorSetLayout());

    sdf::SDFVolumeDesc sdfDesc{};
    sdfVolume.create(VKMDevice, sdfDesc);

    probeVisualizer.create(VKMDevice, renderPass, pipelineCache);
    textureVisualizer.create(VKMDevice, renderPass, pipelineCache);

    displayWindows.camera.type = Camera::firstperson;
    displayWindows.camera.setPerspective(60.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 256.0f);
    displayWindows.camera.setPosition(glm::vec3(-0.48f, 2.f, -0.31f));
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
    if (VKMDevice == nullptr) {
        return;
    }

    const rt::RayTracingSupport support = rt::RayTracingContext::querySupport(
        VKMDevice->physicalDevice,
        VKMDevice->supportedExtensions);
    if (!support.supported) {
        OutputMessage("[DDGI] Vulkan ray tracing extensions not enabled: {}\n", support.reason);
        return;
    }

    for (const char* extensionName : rt::RayTracingContext::requiredExtensionNames()) {
        AddLayerOrExtension(enabledDeviceExtensions, extensionName);
    }

    bufferDeviceAddressFeatures = support.bufferDeviceAddressFeatures;
    rayTracingPipelineFeatures = support.rayTracingPipelineFeatures;
    accelerationStructureFeatures = support.accelerationStructureFeatures;
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
    accelerationStructureFeatures.accelerationStructure = VK_TRUE;

    accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;
    rayTracingPipelineFeatures.pNext = &bufferDeviceAddressFeatures;
    bufferDeviceAddressFeatures.pNext = nullptr;
    deviceCreatepNextChain = &accelerationStructureFeatures;
}

void DDGISample::recordFrame(vk::CommandBuffer commandBuffer)
{
    vk::CommandBufferBeginInfo beginInfo{};
    VK_CHECK_RESULT(commandBuffer.begin(&beginInfo));

    ddgiVolume.updateConstants(displayWindows.camera, currentBuffer);
    ddgiVolume.updateProbesFromSDF(commandBuffer, sdfVolume);
    ddgiVolume.traceProbeRays(commandBuffer, rayTracing.sceneBinding());
    ddgiVolume.updateProbes(commandBuffer);

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

    renderer.recordGBuffer(commandBuffer, scene, displayWindows.camera, vk::Extent2D{width, height});

    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
    renderer.drawScene(
        commandBuffer,
        scene,
        displayWindows.camera,
        vk::Extent2D{width, height},
        &ddgiVolume,
        debugState.enableDdgi);

    if (debugState.showProbes) {
        probeVisualizer.draw(commandBuffer, ddgiVolume, displayWindows.camera, vk::Extent2D{width, height});
    }
    if (debugState.showTexturePanel) {
        textureVisualizer.draw(commandBuffer, ddgiVolume, debugState.selectedTexture, vk::Extent2D{width, height});
    }

    drawUI(commandBuffer);
    commandBuffer.endRenderPass();
    commandBuffer.end();
}

} // namespace app
