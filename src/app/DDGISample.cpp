#include "app/DDGISample.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>



namespace app {
namespace {

ddgi::DDGIVolumeDesc defaultVolumeDesc()
{
    return ddgi::DDGIVolumeDesc{};
}

glm::vec3 maxVec3(const glm::vec3& value, float scalar)
{
    return (glm::max)(value, glm::vec3(scalar));
}

} // namespace

DDGISample::DDGISample()
{
    displayWindows.title = "DDGI Vulkan Sample";
    displayWindows.name = "DDGI";
    manualVolumeDesc = defaultVolumeDesc();
}

DDGISample::~DDGISample()
{
    atlasWindow.destroy();
    probeVisualizer.destroy();
    ddgiVolume.destroy();
    sdfVolume.destroy();
    rayTracing.destroy();
    renderer.destroy();
    scene.clear();
}

ddgi::DDGIVolumeDesc DDGISample::buildVolumeDescFromDebugState() const
{
    ddgi::DDGIVolumeDesc volumeDesc = manualVolumeDesc;
    const float density = (std::max)(debugState.probeDensity, 0.25f);
    volumeDesc.raysPerProbe = debugState.raysPerProbe;
    volumeDesc.probeUpdatePhaseCount = 4u;

    if (debugState.distributionMode == debug::ProbeDistributionMode::ManualVolume ||
        !debugState.autoFitProbesToSceneBounds ||
        !scene.sceneBounds().valid) {
        // Manual mode preserves the current origin/counts and only scales the
        // spacing uniformly. This gives the user one density knob without
        // introducing another full set of manual count/spacing widgets yet.
        volumeDesc.probeSpacing = manualVolumeDesc.probeSpacing / density;
        volumeDesc.origin = manualVolumeDesc.origin + debugState.volumeOffset;
        return volumeDesc;
    }

    const scene::SceneBounds& bounds = scene.sceneBounds();
    const glm::vec3 safeExtent = maxVec3(bounds.extent, 1.0f);

    // We shrink the placement bounds slightly before computing the regular
    // probe grid. That leaves a small gap to nearby walls and avoids probes
    // spawning directly on the scene AABB faces, which tends to exaggerate
    // relocation offsets and visibility artifacts during bring-up.
    const glm::vec3 inwardMargin = (glm::min)(safeExtent * 0.05f, safeExtent * 0.45f);
    const glm::vec3 paddedMin = bounds.min + inwardMargin;
    const glm::vec3 paddedMax = bounds.max - inwardMargin;
    const glm::vec3 paddedExtent = maxVec3(paddedMax - paddedMin, 0.5f);

    // Probe density maps to a target spacing measured in world units. Counts
    // are then derived from the padded AABB extent so the volume stays tightly
    // fit to the scene while still giving the user one intuitive density knob.
    const glm::vec3 targetSpacing = glm::vec3(2.0f / density);
    glm::uvec3 probeCounts = glm::uvec3(glm::floor(paddedExtent / targetSpacing)) + glm::uvec3(1u);
    probeCounts = (glm::max)(probeCounts, glm::uvec3(2u));

    const glm::vec3 denominator(
        static_cast<float>((std::max)(1u, probeCounts.x - 1u)),
        static_cast<float>((std::max)(1u, probeCounts.y - 1u)),
        static_cast<float>((std::max)(1u, probeCounts.z - 1u)));

    volumeDesc.origin = paddedMin + debugState.volumeOffset;
    volumeDesc.probeCounts = probeCounts;
    volumeDesc.probeSpacing = paddedExtent / denominator;
    return volumeDesc;
}

void DDGISample::recreateDdgiVolumeAndRenderer(const ddgi::DDGIVolumeDesc& volumeDesc)
{
    if (VKMDevice == nullptr) {
        return;
    }

    device.waitIdle();
    renderer.destroy();
    ddgiVolume.destroy();

    ddgiVolume.create(VKMDevice, pipelineCache, volumeDesc);
    renderer.create(
        VKMDevice,
        pipelineCache,
        renderPass,
        depthFormat,
        vk::Extent2D{width, height},
        ddgiVolume.resources().descriptorSetLayout());
    manualVolumeDesc = volumeDesc;
    debugState.probeCount = ddgiVolume.totalProbeCount();
    debugState.raysPerProbe = volumeDesc.raysPerProbe;
}

void DDGISample::applyPendingDebugChanges()
{
    if (debugState.requestApplyProbeLayout) {
        const ddgi::DDGIVolumeDesc updatedDesc = buildVolumeDescFromDebugState();
        recreateDdgiVolumeAndRenderer(updatedDesc);
        debugState.requestApplyProbeLayout = false;
    }
}

void DDGISample::syncDebugStateFromVolume()
{
    debugState.probeCount = ddgiVolume.totalProbeCount();
    debugState.volumeOrigin = ddgiVolume.description().origin;
    debugState.probeUpdatePhaseCount = ddgiVolume.description().probeUpdatePhaseCount;
    debugState.currentProbeUpdatePhase = frameCounter % (std::max)(1u, debugState.probeUpdatePhaseCount);
}

void DDGISample::updateRadianceDebugStats()
{
    if (!debugState.showProbeRadianceStats) {
        debugState.averageProbeRadiance = glm::vec3(0.0f);
        debugState.maxProbeRadiance = glm::vec3(0.0f);
        debugState.radianceDebugProbeCount = 0u;
        return;
    }

    constexpr uint32_t kRadianceDebugReadbackInterval = 15u;
    ++radianceDebugFrameCounter;
    if ((radianceDebugFrameCounter % kRadianceDebugReadbackInterval) != 0u) {
        return;
    }

    std::vector<glm::vec3> averageRadiance{};
    std::vector<glm::vec3> localOffsets{};
    std::vector<uint32_t> states{};
    if (!ddgiVolume.readProbeDebugData(averageRadiance, localOffsets, states)) {
        return;
    }

    glm::vec3 radianceSum(0.0f);
    glm::vec3 maxRadiance(0.0f);
    uint32_t validProbeCount = 0u;
    for (const glm::vec3& radiance : averageRadiance) {
        if (radiance.x <= 0.0f && radiance.y <= 0.0f && radiance.z <= 0.0f) {
            continue;
        }
        radianceSum += radiance;
        maxRadiance = (glm::max)(maxRadiance, radiance);
        ++validProbeCount;
    }

    debugState.averageProbeRadiance = validProbeCount > 0u
        ? radianceSum / static_cast<float>(validProbeCount)
        : glm::vec3(0.0f);
    debugState.maxProbeRadiance = maxRadiance;
    debugState.radianceDebugProbeCount = validProbeCount;
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

    const ddgi::DDGIVolumeDesc initialVolumeDesc = buildVolumeDescFromDebugState();
    ddgiVolume.create(VKMDevice, pipelineCache, initialVolumeDesc);
    manualVolumeDesc = initialVolumeDesc;
    debugState.raysPerProbe = initialVolumeDesc.raysPerProbe;
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
    atlasWindow.create(instance, physicalDevice, VKMDevice, queue, pipelineCache, displayWindows.windowInstance);

    displayWindows.camera.type = Camera::firstperson;
    displayWindows.camera.setPerspective(60.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 256.0f);
    displayWindows.camera.setPosition(glm::vec3(-0.48f, 2.f, -0.31f));

    syncDebugStateFromVolume();
    displayWindows.prepared = true;
}

void DDGISample::render()
{
    if (!displayWindows.prepared) {
        return;
    }

    applyPendingDebugChanges();
    syncDebugStateFromVolume();
    updateRadianceDebugStats();

    prepareFrame();
    auto commandBuffer = drawCmdBuffers[currentBuffer];
    commandBuffer.reset();
    recordFrame(commandBuffer);
    submitFrame();
    ++frameCounter;

    if (atlasWindow.consumeHideRequest()) {
        debugState.showAtlasWindow = false;
    }
    atlasWindow.update(ddgiVolume, debugState.showAtlasWindow);
}

void DDGISample::OnUpdateHUD(vkm::HUD* ui)
{
    debugUi.draw(ui, debugState);
}

void DDGISample::windowHasResized()
{
    renderer.destroy();
    probeVisualizer.destroy();

    renderer.create(
        VKMDevice,
        pipelineCache,
        renderPass,
        depthFormat,
        vk::Extent2D{width, height},
        ddgiVolume.resources().descriptorSetLayout());
    probeVisualizer.create(VKMDevice, renderPass, pipelineCache);
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

    ddgiVolume.updateConstants(displayWindows.camera, frameCounter);
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

    if (debugState.showProbeSpheres) {
        probeVisualizer.draw(commandBuffer, ddgiVolume, displayWindows.camera, vk::Extent2D{width, height});
    }

    drawUI(commandBuffer);
    commandBuffer.endRenderPass();
    commandBuffer.end();
}

} // namespace app
