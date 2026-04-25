#include "debug/TextureVisualizer.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace debug {
namespace {

struct TextureDebugPushConstants {
    glm::vec4 visualizeModeAndScale{0.0f, 1.0f, 0.0f, 0.0f};
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
        OutputMessage("[TextureVisualizer] Failed to open shader: {}\n", path);
        return VK_NULL_HANDLE;
    }

    const std::streamsize fileSize = shaderFile.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        OutputMessage("[TextureVisualizer] Invalid SPIR-V size for shader {}: {} bytes\n", path, static_cast<int64_t>(fileSize));
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirvWords(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    shaderFile.seekg(0, std::ios::beg);
    shaderFile.read(reinterpret_cast<char*>(spirvWords.data()), fileSize);
    if (spirvWords.empty() || spirvWords.front() != 0x07230203u) {
        OutputMessage("[TextureVisualizer] Shader is not valid SPIR-V: {}\n", path);
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

vk::DescriptorImageInfo atlasDescriptor(const ddgi::DDGIVolume& volume, ddgi::DebugTexture texture)
{
    switch (texture) {
    case ddgi::DebugTexture::Depth:
        return volume.resources().textures().depth.descriptorImageInfo;
    case ddgi::DebugTexture::DepthSquared:
        return volume.resources().textures().depthSquared.descriptorImageInfo;
    case ddgi::DebugTexture::Irradiance:
    default:
        return volume.resources().textures().irradiance.descriptorImageInfo;
    }
}

} // namespace

void TextureVisualizer::create(vkm::VKMDevice* inDevice, vk::RenderPass renderPass, vk::PipelineCache pipelineCache)
{
    destroy();
    if (inDevice == nullptr || renderPass == VK_NULL_HANDLE) {
        OutputMessage("[TextureVisualizer] create requires a valid device and render pass\n");
        return;
    }

    device = inDevice;

    vk::DescriptorSetLayoutBinding sampledTextureBinding{};
    sampledTextureBinding.setBinding(0)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
    descriptorSetLayoutCreateInfo.setBindings(sampledTextureBinding);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(
        &descriptorSetLayoutCreateInfo,
        nullptr,
        &descriptorSetLayoutHandle));

    vk::DescriptorPoolSize poolSize{};
    poolSize.setType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(3);
    vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo{};
    descriptorPoolCreateInfo.setMaxSets(3).setPoolSizeCount(1).setPPoolSizes(&poolSize);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorPool(
        &descriptorPoolCreateInfo,
        nullptr,
        &descriptorPoolHandle));

    std::array<vk::DescriptorSetLayout, 3> layouts{
        descriptorSetLayoutHandle,
        descriptorSetLayoutHandle,
        descriptorSetLayoutHandle,
    };
    vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo{};
    descriptorSetAllocateInfo.setDescriptorPool(descriptorPoolHandle)
        .setDescriptorSetCount(static_cast<uint32_t>(layouts.size()))
        .setPSetLayouts(layouts.data());
    VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&descriptorSetAllocateInfo, descriptorSets.data()));

    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(TextureDebugPushConstants));

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setSetLayoutCount(1)
        .setPSetLayouts(&descriptorSetLayoutHandle)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&pushConstantRange);
    VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(&pipelineLayoutCreateInfo, nullptr, &pipelineLayoutHandle));

    std::array<vk::ShaderModule, 2> shaderModules{
        loadShaderModule(device->logicalDevice, shaderPath("debug/texture_debug.vert.spv")),
        loadShaderModule(device->logicalDevice, shaderPath("debug/texture_debug.frag.spv")),
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

    vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.setDepthTestEnable(VK_FALSE)
        .setDepthWriteEnable(VK_FALSE)
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
        .setRenderPass(renderPass)
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

void TextureVisualizer::destroy()
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

    descriptorSets = {};
    cachedImageViews = {};
    pipelineHandle = VK_NULL_HANDLE;
    pipelineLayoutHandle = VK_NULL_HANDLE;
    descriptorPoolHandle = VK_NULL_HANDLE;
    descriptorSetLayoutHandle = VK_NULL_HANDLE;
    device = nullptr;
}

void TextureVisualizer::draw(vk::CommandBuffer commandBuffer,
                             const ddgi::DDGIVolume& volume,
                             ddgi::DebugTexture texture,
                             vk::Extent2D framebufferExtent)
{
    if (device == nullptr || pipelineHandle == VK_NULL_HANDLE) {
        return;
    }

    const uint32_t textureIndex = static_cast<uint32_t>(texture);
    const vk::DescriptorImageInfo descriptor = atlasDescriptor(volume, texture);
    if (cachedImageViews[textureIndex] != descriptor.imageView) {
        vk::WriteDescriptorSet descriptorWrite{};
        descriptorWrite.setDstSet(descriptorSets[textureIndex])
            .setDstBinding(0)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setPImageInfo(&descriptor);
        device->logicalDevice.updateDescriptorSets(descriptorWrite, {});
        cachedImageViews[textureIndex] = descriptor.imageView;
    }

    const float panelWidth = static_cast<float>(std::max(256u, framebufferExtent.width / 4u));
    const float panelHeight = panelWidth;
    const float margin = 16.0f;
    vk::Viewport viewport{};
    viewport.setX(static_cast<float>(framebufferExtent.width) - panelWidth - margin)
        .setY(margin)
        .setWidth(panelWidth)
        .setHeight(panelHeight)
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vk::Rect2D scissor{};
    scissor.setOffset(vk::Offset2D{
        static_cast<int32_t>(std::max(0.0f, viewport.x)),
        static_cast<int32_t>(std::max(0.0f, viewport.y)),
    });
    scissor.setExtent(vk::Extent2D{
        static_cast<uint32_t>(viewport.width),
        static_cast<uint32_t>(viewport.height),
    });

    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, scissor);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineHandle);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayoutHandle,
        0,
        descriptorSets[textureIndex],
        {});

    TextureDebugPushConstants pushConstants{};
    pushConstants.visualizeModeAndScale.x = static_cast<float>(textureIndex);
    pushConstants.visualizeModeAndScale.y = texture == ddgi::DebugTexture::Irradiance ? 1.0f : 0.05f;
    commandBuffer.pushConstants(
        pipelineLayoutHandle,
        vk::ShaderStageFlagBits::eFragment,
        0,
        sizeof(TextureDebugPushConstants),
        &pushConstants);
    commandBuffer.draw(3, 1, 0, 0);
}

} // namespace debug
