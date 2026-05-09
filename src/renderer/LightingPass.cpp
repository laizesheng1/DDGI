#include "renderer/LightingPass.h"

#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace renderer {
namespace {

struct LightingPushConstants {
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 options{1.0f, 1.35f, 1.0f, 0.0f};
};

std::string shaderPath(const char* relativePath)
{
#if defined(VKM_SHADERS_DIR)
    return std::string(VKM_SHADERS_DIR) + "glsl/" + relativePath;
#else
    return std::string("./../shaders/glsl/") + relativePath;
#endif
}

vk::ShaderModule loadShaderModule(vk::Device logicalDevice, const std::string& path)
{
    std::ifstream shaderFile(path, std::ios::binary | std::ios::ate);
    if (!shaderFile.is_open()) {
        OutputMessage("[LightingPass] Failed to open shader: {}\n", path);
        return VK_NULL_HANDLE;
    }

    const std::streamsize fileSize = shaderFile.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        OutputMessage("[LightingPass] Invalid SPIR-V size for shader {}: {} bytes\n", path, static_cast<int64_t>(fileSize));
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirvWords(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    shaderFile.seekg(0, std::ios::beg);
    shaderFile.read(reinterpret_cast<char*>(spirvWords.data()), fileSize);
    if (spirvWords.empty() || spirvWords.front() != 0x07230203u) {
        OutputMessage("[LightingPass] Shader is not valid SPIR-V: {}\n", path);
        return VK_NULL_HANDLE;
    }

    vk::ShaderModuleCreateInfo shaderModuleCreateInfo{};
    shaderModuleCreateInfo.setCode(spirvWords);
    vk::ShaderModule shaderModule{VK_NULL_HANDLE};
    VK_CHECK_RESULT(logicalDevice.createShaderModule(&shaderModuleCreateInfo, nullptr, &shaderModule));
    return shaderModule;
}

vk::PipelineShaderStageCreateInfo makeStage(vk::ShaderModule shaderModule, vk::ShaderStageFlagBits stage)
{
    vk::PipelineShaderStageCreateInfo shaderStage{};
    shaderStage.setStage(stage)
        .setModule(shaderModule)
        .setPName("main");
    return shaderStage;
}

vk::DescriptorSetLayoutBinding makeGBufferBinding(uint32_t binding)
{
    vk::DescriptorSetLayoutBinding descriptorBinding{};
    descriptorBinding.setBinding(binding)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    return descriptorBinding;
}

} // namespace

void LightingPass::create(vkm::VKMDevice* inDevice,
                          vk::PipelineCache pipelineCache,
                          vk::RenderPass swapchainRenderPass,
                          vk::DescriptorSetLayout ddgiSetLayout)
{
    destroy();
    if (inDevice == nullptr || ddgiSetLayout == VK_NULL_HANDLE || swapchainRenderPass == VK_NULL_HANDLE) {
        OutputMessage("[LightingPass] create requires a valid device, swapchain render pass, and DDGI descriptor layout\n");
        return;
    }

    device = inDevice;

    std::array<vk::DescriptorSetLayoutBinding, 6> gbufferBindings{
        makeGBufferBinding(0),
        makeGBufferBinding(1),
        makeGBufferBinding(2),
        makeGBufferBinding(3),
        makeGBufferBinding(4),
        makeGBufferBinding(5),
    };
    vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
    descriptorSetLayoutCreateInfo.setBindings(gbufferBindings);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(
        &descriptorSetLayoutCreateInfo,
        nullptr,
        &descriptorSetLayoutHandle));

    std::array<vk::DescriptorSetLayoutBinding, 2> lightingBindings{};
    lightingBindings[0].setBinding(0)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    lightingBindings[1].setBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    vk::DescriptorSetLayoutCreateInfo lightingDescriptorSetLayoutCreateInfo{};
    lightingDescriptorSetLayoutCreateInfo.setBindings(lightingBindings);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(
        &lightingDescriptorSetLayoutCreateInfo,
        nullptr,
        &lightingDescriptorSetLayoutHandle));

    std::array<vk::DescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].setType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(6);
    poolSizes[1].setType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(1);
    poolSizes[2].setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1);
    vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo{};
    descriptorPoolCreateInfo.setMaxSets(2).setPoolSizes(poolSizes);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorPool(
        &descriptorPoolCreateInfo,
        nullptr,
        &descriptorPoolHandle));

    vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo{};
    descriptorSetAllocateInfo.setDescriptorPool(descriptorPoolHandle)
        .setDescriptorSetCount(1)
        .setPSetLayouts(&descriptorSetLayoutHandle);
    VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&descriptorSetAllocateInfo, &descriptorSetHandle));

    vk::DescriptorSetAllocateInfo lightingDescriptorSetAllocateInfo{};
    lightingDescriptorSetAllocateInfo.setDescriptorPool(descriptorPoolHandle)
        .setDescriptorSetCount(1)
        .setPSetLayouts(&lightingDescriptorSetLayoutHandle);
    VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&lightingDescriptorSetAllocateInfo, &lightingDescriptorSetHandle));

    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(LightingPushConstants));

    std::array<vk::DescriptorSetLayout, 3> setLayouts{
        descriptorSetLayoutHandle,
        ddgiSetLayout,
        lightingDescriptorSetLayoutHandle,
    };
    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setSetLayouts(setLayouts)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&pushConstantRange);
    VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(
        &pipelineLayoutCreateInfo,
        nullptr,
        &pipelineLayoutHandle));

    std::array<vk::ShaderModule, 2> shaderModules{
        loadShaderModule(device->logicalDevice, shaderPath("lighting/fullscreen.vert.spv")),
        loadShaderModule(device->logicalDevice, shaderPath("lighting/ddgi_lighting.frag.spv")),
    };
    if (!shaderModules[0] || !shaderModules[1]) {
        for (vk::ShaderModule shaderModule : shaderModules) {
            if (shaderModule != VK_NULL_HANDLE) {
                device->logicalDevice.destroyShaderModule(shaderModule);
            }
        }
        destroy();
        return;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{
        makeStage(shaderModules[0], vk::ShaderStageFlagBits::eVertex),
        makeStage(shaderModules[1], vk::ShaderStageFlagBits::eFragment),
    };

    vk::PipelineVertexInputStateCreateInfo vertexInputState{};
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
    inputAssemblyState.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rasterizationState{};
    rasterizationState.setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0f);

    vk::PipelineMultisampleStateCreateInfo multisampleState{};
    multisampleState.setRasterizationSamples(vk::SampleCountFlagBits::e1);

    // The deferred lighting pass is already a fullscreen pass that samples
    // GBuffer depth. Writing that depth into the swapchain render pass depth
    // attachment is cheaper than drawing a second scene depth prepass and lets
    // debug overlays such as probe spheres use normal hardware depth testing.
    vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eAlways)
        .setStencilTestEnable(VK_FALSE);

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.setBlendEnable(VK_FALSE)
        .setColorWriteMask(vk::ColorComponentFlagBits::eR |
                           vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB |
                           vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.setAttachmentCount(1).setPAttachments(&colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.setDynamicStates(dynamicStates);

    vk::GraphicsPipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.setStages(shaderStages)
        .setPVertexInputState(&vertexInputState)
        .setPInputAssemblyState(&inputAssemblyState)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizationState)
        .setPMultisampleState(&multisampleState)
        .setPDepthStencilState(&depthStencilState)
        .setPColorBlendState(&colorBlendState)
        .setPDynamicState(&dynamicState)
        .setLayout(pipelineLayoutHandle)
        .setRenderPass(swapchainRenderPass)
        .setSubpass(0);
    VK_CHECK_RESULT(device->logicalDevice.createGraphicsPipelines(
        pipelineCache,
        1,
        &pipelineCreateInfo,
        nullptr,
        &pipelineHandle));

    for (vk::ShaderModule shaderModule : shaderModules) {
        device->logicalDevice.destroyShaderModule(shaderModule);
    }
}

void LightingPass::destroy()
{
    if (device == nullptr) {
        return;
    }

    if (pipelineHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyPipeline(pipelineHandle);
    }
    if (pipelineLayoutHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyPipelineLayout(pipelineLayoutHandle);
    }
    if (descriptorPoolHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyDescriptorPool(descriptorPoolHandle);
    }
    if (descriptorSetLayoutHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyDescriptorSetLayout(descriptorSetLayoutHandle);
    }
    if (lightingDescriptorSetLayoutHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyDescriptorSetLayout(lightingDescriptorSetLayoutHandle);
    }

    cachedImageViews = {};
    cachedLightingInfoBuffer = VK_NULL_HANDLE;
    cachedLightsBuffer = VK_NULL_HANDLE;
    pipelineHandle = VK_NULL_HANDLE;
    pipelineLayoutHandle = VK_NULL_HANDLE;
    descriptorPoolHandle = VK_NULL_HANDLE;
    descriptorSetLayoutHandle = VK_NULL_HANDLE;
    lightingDescriptorSetLayoutHandle = VK_NULL_HANDLE;
    descriptorSetHandle = VK_NULL_HANDLE;
    lightingDescriptorSetHandle = VK_NULL_HANDLE;
    device = nullptr;
}

void LightingPass::record(vk::CommandBuffer commandBuffer,
                          const GBufferPass& gbufferPass,
                          const scene::SceneGpuData& sceneGpuData,
                          const Camera& camera,
                          const ddgi::DDGIVolume& volume,
                          vk::Extent2D framebufferExtent,
                          bool enableDdgi,
                          float ddgiIntensity)
{
    if (device == nullptr || !gbufferPass.isCreated() || pipelineHandle == VK_NULL_HANDLE || !sceneGpuData.isCreated()) {
        return;
    }

    const std::array<vk::DescriptorImageInfo, 6> gbufferDescriptors{
        gbufferPass.worldPosition().descriptorImageInfo,
        gbufferPass.normal().descriptorImageInfo,
        gbufferPass.albedo().descriptorImageInfo,
        gbufferPass.material().descriptorImageInfo,
        gbufferPass.emissive().descriptorImageInfo,
        gbufferPass.depth().descriptorImageInfo,
    };
    const std::array<vk::ImageView, 6> currentImageViews{
        gbufferPass.worldPosition().imageView,
        gbufferPass.normal().imageView,
        gbufferPass.albedo().imageView,
        gbufferPass.material().imageView,
        gbufferPass.emissive().imageView,
        gbufferPass.depth().imageView,
    };
    if (currentImageViews != cachedImageViews) {
        std::array<vk::WriteDescriptorSet, 6> descriptorWrites{};
        for (uint32_t bindingIndex = 0; bindingIndex < descriptorWrites.size(); ++bindingIndex) {
            descriptorWrites[bindingIndex].setDstSet(descriptorSetHandle)
                .setDstBinding(bindingIndex)
                .setDescriptorCount(1)
                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                .setPImageInfo(&gbufferDescriptors[bindingIndex]);
        }
        device->logicalDevice.updateDescriptorSets(descriptorWrites, {});
        cachedImageViews = currentImageViews;
    }

    if (sceneGpuData.lightingInfoBufferObject().buffer != cachedLightingInfoBuffer ||
        sceneGpuData.lightsBuffer().buffer != cachedLightsBuffer) {
        const vk::DescriptorBufferInfo lightingInfoDescriptor = sceneGpuData.lightingInfoDescriptor();
        const vk::DescriptorBufferInfo lightDescriptor = sceneGpuData.lightDescriptor();
        std::array<vk::WriteDescriptorSet, 2> lightWrites{};
        lightWrites[0].setDstSet(lightingDescriptorSetHandle)
            .setDstBinding(0)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setPBufferInfo(&lightingInfoDescriptor);
        lightWrites[1].setDstSet(lightingDescriptorSetHandle)
            .setDstBinding(1)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setPBufferInfo(&lightDescriptor);
        device->logicalDevice.updateDescriptorSets(lightWrites, {});
        cachedLightingInfoBuffer = sceneGpuData.lightingInfoBufferObject().buffer;
        cachedLightsBuffer = sceneGpuData.lightsBuffer().buffer;
    }

    vk::Viewport viewport{};
    viewport.setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(framebufferExtent.width))
        .setHeight(static_cast<float>(framebufferExtent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vk::Rect2D scissor{vk::Offset2D{0, 0}, framebufferExtent};
    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, scissor);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineHandle);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayoutHandle,
        0,
        descriptorSetHandle,
        {});
    volume.bindForLighting(commandBuffer, pipelineLayoutHandle, 1);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayoutHandle,
        2,
        lightingDescriptorSetHandle,
        {});

    LightingPushConstants pushConstants{};
    pushConstants.cameraPosition = glm::vec4(camera.position, 1.0f);
    pushConstants.options = glm::vec4(enableDdgi ? 1.0f : 0.0f, std::max(ddgiIntensity, 0.0f), 1.0f, 0.0f);
    commandBuffer.pushConstants(
        pipelineLayoutHandle,
        vk::ShaderStageFlagBits::eFragment,
        0,
        sizeof(LightingPushConstants),
        &pushConstants);

    // A fullscreen triangle keeps the deferred pass bandwidth low: the heavy
    // DDGI query happens per visible pixel, while geometry shading already ran
    // once into the GBuffer.
    commandBuffer.draw(3, 1, 0, 0);
}

} // namespace renderer
