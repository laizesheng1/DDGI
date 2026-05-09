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

void copyVolumeDescToDebugState(debug::DebugUIState& state, const ddgi::DDGIVolumeDesc& desc)
{
    state.raysPerProbe = desc.raysPerProbe;
    state.fixedRayCount = (std::min)(desc.fixedRayCount, desc.raysPerProbe);
    state.probeUpdatePhaseCount = desc.probeUpdatePhaseCount;
    state.hysteresis = desc.hysteresis;
    state.maxRayDistance = desc.maxRayDistance;
    state.relocationEnabled = desc.relocationEnabled;
    state.classificationEnabled = desc.classificationEnabled;
    state.probeMultiBounceEnabled = desc.probeMultiBounceEnabled;
    state.probeMultiBounceIntensity = desc.probeMultiBounceIntensity;
}

struct ProbeStateCounts {
    uint32_t active{0u};
    uint32_t inactive{0u};
    uint32_t backface{0u};
    uint32_t noGeometry{0u};
    uint32_t noLocalFrontface{0u};
    uint32_t onlyBackface{0u};
};

ProbeStateCounts countProbeStates(const std::vector<uint32_t>& states)
{
    ProbeStateCounts counts{};
    for (const uint32_t state : states) {
        switch (state) {
        case ddgi::ProbeStateActive:
            ++counts.active;
            break;
        case ddgi::ProbeStateInactiveBackface:
            ++counts.inactive;
            ++counts.backface;
            break;
        case ddgi::ProbeStateInactiveNoGeometry:
            ++counts.inactive;
            ++counts.noGeometry;
            break;
        case ddgi::ProbeStateInactiveNoLocalFrontface:
            ++counts.inactive;
            ++counts.noLocalFrontface;
            break;
        case ddgi::ProbeStateInactiveOnlyBackface:
            ++counts.inactive;
            ++counts.onlyBackface;
            break;
        default:
            if (state == ddgi::ProbeStateActive) {
                ++counts.active;
            } else {
                ++counts.inactive;
            }
            break;
        }
    }
    return counts;
}

void copyProbeStateCountsToDebugState(debug::DebugUIState& state, const ProbeStateCounts& counts)
{
    state.activeProbeCount = counts.active;
    state.inactiveProbeCount = counts.inactive;
    state.inactiveBackfaceCount = counts.backface;
    state.inactiveNoGeometryCount = counts.noGeometry;
    state.inactiveNoLocalFrontfaceCount = counts.noLocalFrontface;
    state.inactiveOnlyBackfaceCount = counts.onlyBackface;
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
    sdfGenerator.destroy();
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
    volumeDesc.fixedRayCount = (std::min)(debugState.fixedRayCount, volumeDesc.raysPerProbe);
    volumeDesc.probeUpdatePhaseCount = (std::max)(1u, debugState.probeUpdatePhaseCount);
    volumeDesc.hysteresis = (std::clamp)(debugState.hysteresis, 0.0f, 0.99f);
    volumeDesc.maxRayDistance = (std::max)(debugState.maxRayDistance, 1.0f);
    volumeDesc.relocationEnabled = debugState.relocationEnabled;
    volumeDesc.classificationEnabled = debugState.classificationEnabled;
    volumeDesc.probeMultiBounceEnabled = debugState.probeMultiBounceEnabled;
    volumeDesc.probeMultiBounceIntensity = (std::max)(debugState.probeMultiBounceIntensity, 0.0f);

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

    ddgiVolume.create(VKMDevice, pipelineCache, volumeDesc, &sdfVolume);
    renderer.create(
        VKMDevice,
        pipelineCache,
        renderPass,
        depthFormat,
        vk::Extent2D{width, height},
        ddgiVolume.resources().descriptorSetLayout());
    manualVolumeDesc = volumeDesc;
    debugState.probeCount = ddgiVolume.totalProbeCount();
    copyVolumeDescToDebugState(debugState, volumeDesc);
}

void DDGISample::applyPendingDebugChanges()
{
    if (debugState.requestApplyProbeLayout) {
        const ddgi::DDGIVolumeDesc updatedDesc = buildVolumeDescFromDebugState();
        recreateDdgiVolumeAndRenderer(updatedDesc);
        debugState.requestApplyProbeLayout = false;
    }
    if (debugState.requestClearProbes) {
        ddgiVolume.requestClearProbes();
        debugState.requestClearProbes = false;
    }
}

void DDGISample::syncDebugStateFromVolume()
{
    debugState.probeCount = ddgiVolume.totalProbeCount();
    debugState.volumeOrigin = ddgiVolume.description().origin;
    debugState.currentProbeUpdatePhase = frameCounter % (std::max)(1u, debugState.probeUpdatePhaseCount);
}

void DDGISample::updateRadianceDebugStats()
{
    constexpr uint32_t kDebugReadbackInterval = 15u;
    ++radianceDebugFrameCounter;

    if (!debugState.showProbeRadianceStats) {
        debugState.averageProbeRadiance = glm::vec3(0.0f);
        debugState.maxProbeRadiance = glm::vec3(0.0f);
        debugState.radianceDebugProbeCount = 0u;
        if ((radianceDebugFrameCounter % kDebugReadbackInterval) == 0u) {
            std::vector<glm::vec3> localOffsets{};
            std::vector<uint32_t> states{};
            if (ddgiVolume.readProbePlacementDebugData(localOffsets, states)) {
                copyProbeStateCountsToDebugState(debugState, countProbeStates(states));
            }
        }
        return;
    }

    if ((radianceDebugFrameCounter % kDebugReadbackInterval) != 0u) {
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
    const ProbeStateCounts probeStateCounts = countProbeStates(states);
    for (uint32_t probeIndex = 0u; probeIndex < averageRadiance.size(); ++probeIndex) {
        const glm::vec3& radiance = averageRadiance[probeIndex];
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
    copyProbeStateCountsToDebugState(debugState, probeStateCounts);
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

    sdf::SDFVolumeDesc sdfDesc{};
    if (scene.sceneBounds().valid) {
        const glm::vec3 paddedExtent = maxVec3(scene.sceneBounds().extent, 1.0f) * 1.10f;
        sdfDesc.resolution = glm::uvec3(96u, 64u, 96u);
        sdfDesc.voxelSize = paddedExtent / glm::vec3(sdfDesc.resolution);
        sdfDesc.origin = scene.sceneBounds().center - paddedExtent * 0.5f;
        sdfDesc.maxDistance = maxVec3(paddedExtent, 1.0f).x;
        sdfDesc.maxDistance = (std::max)(sdfDesc.maxDistance, (std::max)(paddedExtent.y, paddedExtent.z));
        sdfDesc.narrowBandDistance = glm::length(sdfDesc.voxelSize);
    }
    sdfVolume.create(VKMDevice, sdfDesc);
    sdfGenerator.create(VKMDevice, pipelineCache);
    sdfGenerator.generateFromScene(scene, rayTracing.gpuSceneData(), sdfVolume, queue);

    const ddgi::DDGIVolumeDesc initialVolumeDesc = buildVolumeDescFromDebugState();
    ddgiVolume.create(VKMDevice, pipelineCache, initialVolumeDesc, &sdfVolume);
    manualVolumeDesc = initialVolumeDesc;
    copyVolumeDescToDebugState(debugState, initialVolumeDesc);
    renderer.create(
        VKMDevice,
        pipelineCache,
        renderPass,
        depthFormat,
        vk::Extent2D{width, height},
        ddgiVolume.resources().descriptorSetLayout());

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
    //shaderDemoteFeatures = {};
    //vulkan13Features = {};
    void* shaderDemoteFeatureChain = nullptr;

    const bool shaderDemoteExtensionSupported =
        std::find(
            VKMDevice->supportedExtensions.begin(),
            VKMDevice->supportedExtensions.end(),
            VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME) != VKMDevice->supportedExtensions.end();
    if (shaderDemoteExtensionSupported) {
        vk::PhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT availableShaderDemoteFeatures{};
        vk::PhysicalDeviceFeatures2 availableFeatures{};
        availableFeatures.pNext = &availableShaderDemoteFeatures;
        VKMDevice->physicalDevice.getFeatures2(&availableFeatures);
        if (availableShaderDemoteFeatures.shaderDemoteToHelperInvocation) {
            AddLayerOrExtension(enabledDeviceExtensions, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
            shaderDemoteFeatures.shaderDemoteToHelperInvocation = VK_TRUE;
            shaderDemoteFeatureChain = &shaderDemoteFeatures;
        } else {
            OutputMessage("[DDGI] {} is present but shaderDemoteToHelperInvocation feature is not supported\n",
                          VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
        }
    } else if (VK_API_VERSION_MAJOR(VKMDevice->properties.apiVersion) > 1 ||
               (VK_API_VERSION_MAJOR(VKMDevice->properties.apiVersion) == 1 &&
                VK_API_VERSION_MINOR(VKMDevice->properties.apiVersion) >= 3)) {
        vk::PhysicalDeviceVulkan13Features availableVulkan13Features{};
        vk::PhysicalDeviceFeatures2 availableFeatures{};
        availableFeatures.pNext = &availableVulkan13Features;
        VKMDevice->physicalDevice.getFeatures2(&availableFeatures);
        if (availableVulkan13Features.shaderDemoteToHelperInvocation) {
            vulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;
            shaderDemoteFeatureChain = &vulkan13Features;
        } else {
            OutputMessage("[DDGI] Vulkan 1.3 is present but shaderDemoteToHelperInvocation feature is not supported\n");
        }
    } else {
        OutputMessage("[DDGI] {} is not supported; alpha-tested GBuffer shaders must be compiled without DemoteToHelperInvocation\n",
                      VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
    }

    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
    accelerationStructureFeatures.accelerationStructure = VK_TRUE;

    accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;
    rayTracingPipelineFeatures.pNext = &bufferDeviceAddressFeatures;
    bufferDeviceAddressFeatures.pNext = shaderDemoteFeatureChain;
    shaderDemoteFeatures.pNext = nullptr;
    vulkan13Features.pNext = nullptr;
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
        rayTracing.gpuSceneData(),
        displayWindows.camera,
        vk::Extent2D{width, height},
        &ddgiVolume,
        debugState.enableDdgi,
        debugState.ddgiIntensity);

    if (debugState.showProbeSpheres) {
        probeVisualizer.draw(commandBuffer, ddgiVolume, displayWindows.camera, vk::Extent2D{width, height});
    }

    drawUI(commandBuffer);
    commandBuffer.endRenderPass();
    commandBuffer.end();
}

} // namespace app
