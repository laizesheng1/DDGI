#include "renderer/LightingPass.h"

#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace renderer {
namespace {

struct LightingPushConstants {
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 lightDirectionAndIntensity{0.0f, 1.0f, 0.0f, 2.0f};
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

    std::array<vk::DescriptorSetLayoutBinding, 5> gbufferBindings{
        makeGBufferBinding(0),
        makeGBufferBinding(1),
        makeGBufferBinding(2),
        makeGBufferBinding(3),
        makeGBufferBinding(4),
    };
    vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
    descriptorSetLayoutCreateInfo.setBindings(gbufferBindings);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(
        &descriptorSetLayoutCreateInfo,
        nullptr,
        &descriptorSetLayoutHandle));

    vk::DescriptorPoolSize poolSize{};
    poolSize.setType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(5);
    vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo{};
    descriptorPoolCreateInfo.setMaxSets(1).setPoolSizeCount(1).setPPoolSizes(&poolSize);
    VK_CHECK_RESULT(device->logicalDevice.createDescriptorPool(
        &descriptorPoolCreateInfo,
        nullptr,
        &descriptorPoolHandle));

    vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo{};
    descriptorSetAllocateInfo.setDescriptorPool(descriptorPoolHandle)
        .setDescriptorSetCount(1)
        .setPSetLayouts(&descriptorSetLayoutHandle);
    VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&descriptorSetAllocateInfo, &descriptorSetHandle));

    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(LightingPushConstants));

    std::array<vk::DescriptorSetLayout, 2> setLayouts{
        descriptorSetLayoutHandle,
        ddgiSetLayout,
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

    cachedImageViews = {};
    pipelineHandle = VK_NULL_HANDLE;
    pipelineLayoutHandle = VK_NULL_HANDLE;
    descriptorPoolHandle = VK_NULL_HANDLE;
    descriptorSetLayoutHandle = VK_NULL_HANDLE;
    descriptorSetHandle = VK_NULL_HANDLE;
    device = nullptr;
}

void LightingPass::record(vk::CommandBuffer commandBuffer,
                          const GBufferPass& gbufferPass,
                          const Camera& camera,
                          const ddgi::DDGIVolume& volume,
                          vk::Extent2D framebufferExtent,
                          bool enableDdgi)
{
    if (device == nullptr || !gbufferPass.isCreated() || pipelineHandle == VK_NULL_HANDLE) {
        return;
    }

    const std::array<vk::DescriptorImageInfo, 5> gbufferDescriptors{
        gbufferPass.worldPosition().descriptorImageInfo,
        gbufferPass.normal().descriptorImageInfo,
        gbufferPass.albedo().descriptorImageInfo,
        gbufferPass.material().descriptorImageInfo,
        gbufferPass.depth().descriptorImageInfo,
    };
    const std::array<vk::ImageView, 5> currentImageViews{
        gbufferPass.worldPosition().imageView,
        gbufferPass.normal().imageView,
        gbufferPass.albedo().imageView,
        gbufferPass.material().imageView,
        gbufferPass.depth().imageView,
    };
    if (currentImageViews != cachedImageViews) {
        std::array<vk::WriteDescriptorSet, 5> descriptorWrites{};
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

    LightingPushConstants pushConstants{};
    pushConstants.cameraPosition = glm::vec4(camera.position, 1.0f);
    pushConstants.lightDirectionAndIntensity = glm::vec4(glm::normalize(glm::vec3(0.35f, 0.85f, 0.25f)), 1.8f);
    pushConstants.options = glm::vec4(enableDdgi ? 1.0f : 0.0f, 1.35f, 1.0f, 0.0f);
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
