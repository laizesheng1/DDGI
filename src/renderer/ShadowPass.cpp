#include "renderer/ShadowPass.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "glTFModel.h"

#include <glm/gtc/matrix_transform.hpp>

namespace renderer {
namespace {

struct ShadowPushConstants {
    glm::mat4 lightViewProjection{1.0f};
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
        OutputMessage("[ShadowPass] Failed to open shader: {}\n", path);
        return VK_NULL_HANDLE;
    }
    const std::streamsize fileSize = shaderFile.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        OutputMessage("[ShadowPass] Invalid SPIR-V size for shader {}: {} bytes\n", path, static_cast<int64_t>(fileSize));
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirvWords(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    shaderFile.seekg(0, std::ios::beg);
    shaderFile.read(reinterpret_cast<char*>(spirvWords.data()), fileSize);
    if (spirvWords.empty() || spirvWords.front() != 0x07230203u) {
        OutputMessage("[ShadowPass] Shader is not valid SPIR-V: {}\n", path);
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

std::array<vk::VertexInputAttributeDescription, 5> sceneVertexAttributes()
{
    return {
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, offsetof(vkmglTF::Vertex, pos)},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, offsetof(vkmglTF::Vertex, normal)},
        vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32Sfloat, offsetof(vkmglTF::Vertex, uv)},
        vk::VertexInputAttributeDescription{3, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(vkmglTF::Vertex, color)},
        vk::VertexInputAttributeDescription{4, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(vkmglTF::Vertex, tangent)},
    };
}

void createDepthTexture(vkm::VKMDevice& device,
                        vkm::Texture2D& texture,
                        vk::Extent2D extent,
                        vk::Format format)
{
    texture = vkm::Texture2D(&device);
    texture.width = extent.width;
    texture.height = extent.height;
    texture.mipLevels = 1;
    texture.layerCount = 1;

    vk::ImageCreateInfo imageCreateInfo{};
    texture.initImageCreateInfo(
        imageCreateInfo,
        format,
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
    VK_CHECK_RESULT(device.logicalDevice.createImage(&imageCreateInfo, nullptr, &texture.image));
    texture.allocImageDeviceMem(vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eDepth)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
    texture.CreateImageview(subresourceRange, format, vk::ImageViewType::e2D);
    vk::SamplerCreateInfo samplerCreateInfo{};
    samplerCreateInfo.setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
        .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
        .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
        .setMipLodBias(0.0f)
        .setAnisotropyEnable(VK_FALSE)
        .setCompareEnable(VK_FALSE)
        .setCompareOp(vk::CompareOp::eNever)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatOpaqueWhite);
    VK_CHECK_RESULT(device.logicalDevice.createSampler(&samplerCreateInfo, nullptr, &texture.sampler));
    texture.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    texture.updateDescriptor();
}

glm::vec3 chooseShadowDirectionToLight(const scene::SceneGpuData& sceneGpuData)
{
    for (const scene::SceneLightGpuData& light : sceneGpuData.lights()) {
        const uint32_t lightType = static_cast<uint32_t>(light.positionAndType.w + 0.5f);
        if (lightType == scene::SceneLightDirectional) {
            const glm::vec3 direction = glm::vec3(light.directionAndRange);
            const float length = glm::length(direction);
            if (length > 1.0e-5f) {
                return direction / length;
            }
        }
    }
    return glm::normalize(glm::vec3(-0.45f, 0.90f, -0.30f));
}

std::array<glm::vec3, 8> sceneBoundsCorners(const scene::SceneBounds& bounds)
{
    return {
        glm::vec3{bounds.min.x, bounds.min.y, bounds.min.z},
        glm::vec3{bounds.max.x, bounds.min.y, bounds.min.z},
        glm::vec3{bounds.min.x, bounds.max.y, bounds.min.z},
        glm::vec3{bounds.max.x, bounds.max.y, bounds.min.z},
        glm::vec3{bounds.min.x, bounds.min.y, bounds.max.z},
        glm::vec3{bounds.max.x, bounds.min.y, bounds.max.z},
        glm::vec3{bounds.min.x, bounds.max.y, bounds.max.z},
        glm::vec3{bounds.max.x, bounds.max.y, bounds.max.z},
    };
}

ShadowMapData buildShadowData(const scene::Scene& scene, const scene::SceneGpuData& sceneGpuData, vk::Extent2D extent)
{
    ShadowMapData data{};
    const scene::SceneBounds& bounds = scene.sceneBounds();
    if (!bounds.valid) {
        return data;
    }

    const glm::vec3 directionToLight = chooseShadowDirectionToLight(sceneGpuData);
    const glm::vec3 center = bounds.center;
    const float radius = (std::max)(bounds.radius, 1.0f);
    const glm::vec3 lightPosition = center + directionToLight * radius * 2.0f;
    glm::vec3 up = std::abs(directionToLight.y) > 0.95f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 lightView = glm::lookAt(lightPosition, center, up);

    glm::vec3 lightMin(FLT_MAX);
    glm::vec3 lightMax(-FLT_MAX);
    for (const glm::vec3& corner : sceneBoundsCorners(bounds)) {
        const glm::vec3 lightSpaceCorner = glm::vec3(lightView * glm::vec4(corner, 1.0f));
        lightMin = glm::min(lightMin, lightSpaceCorner);
        lightMax = glm::max(lightMax, lightSpaceCorner);
    }

    const float padding = radius * 0.10f;
    lightMin -= glm::vec3(padding);
    lightMax += glm::vec3(padding);
    const float nearPlane = (std::max)(0.01f, -lightMax.z);
    const float farPlane = (std::max)(nearPlane + 1.0f, -lightMin.z);
    const glm::mat4 lightProjection = glm::ortho(
        lightMin.x,
        lightMax.x,
        lightMin.y,
        lightMax.y,
        nearPlane,
        farPlane);

    data.lightViewProjection = lightProjection * lightView;
    const float texelWorldSize = (lightMax.x - lightMin.x) / static_cast<float>((std::max)(extent.width, 1u));
    data.params = glm::vec4(1.0f, texelWorldSize, texelWorldSize * 2.0f, 0.0060f);
    return data;
}

} // namespace

void ShadowPass::create(vkm::VKMDevice* inDevice, vk::PipelineCache pipelineCache)
{
    destroy();
    if (inDevice == nullptr || vkmglTF::descriptorSetLayoutImage == VK_NULL_HANDLE) {
        return;
    }
    device = inDevice;

    createDepthTexture(*device, depthTexture, shadowExtent, depthFormat);

    vk::AttachmentDescription depthAttachment{};
    depthAttachment.setFormat(depthFormat)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eDepthStencilReadOnlyOptimal);

    vk::AttachmentReference depthReference{0u, vk::ImageLayout::eDepthStencilAttachmentOptimal};
    vk::SubpassDescription subpass{};
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setPDepthStencilAttachment(&depthReference);

    std::array<vk::SubpassDependency, 2> dependencies{};
    dependencies[0].setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eFragmentShader)
        .setDstStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
        .setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite);
    dependencies[1].setSrcSubpass(0)
        .setDstSubpass(VK_SUBPASS_EXTERNAL)
        .setSrcStageMask(vk::PipelineStageFlagBits::eLateFragmentTests)
        .setDstStageMask(vk::PipelineStageFlagBits::eFragmentShader)
        .setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

    vk::RenderPassCreateInfo renderPassCreateInfo{};
    renderPassCreateInfo.setAttachmentCount(1)
        .setPAttachments(&depthAttachment)
        .setSubpasses(subpass)
        .setDependencies(dependencies);
    VK_CHECK_RESULT(device->logicalDevice.createRenderPass(&renderPassCreateInfo, nullptr, &renderPassHandle));

    vk::FramebufferCreateInfo framebufferCreateInfo{};
    framebufferCreateInfo.setRenderPass(renderPassHandle)
        .setAttachmentCount(1)
        .setPAttachments(&depthTexture.imageView)
        .setWidth(shadowExtent.width)
        .setHeight(shadowExtent.height)
        .setLayers(1);
    VK_CHECK_RESULT(device->logicalDevice.createFramebuffer(&framebufferCreateInfo, nullptr, &framebufferHandle));

    std::array<vk::PushConstantRange, 2> pushConstantRanges{};
    pushConstantRanges[0].setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(ShadowPushConstants));
    pushConstantRanges[1].setStageFlags(vk::ShaderStageFlagBits::eFragment)
        .setOffset(vkmglTF::MaterialPushConstantOffset)
        .setSize(sizeof(vkmglTF::MaterialPushConstants));

    vk::DescriptorSetLayout materialSetLayout = vkmglTF::descriptorSetLayoutImage;
    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setSetLayoutCount(1)
        .setPSetLayouts(&materialSetLayout)
        .setPushConstantRanges(pushConstantRanges);
    VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(&pipelineLayoutCreateInfo, nullptr, &pipelineLayoutHandle));

    std::array<vk::ShaderModule, 2> shaderModules{
        loadShaderModule(device->logicalDevice, shaderPath("scene/shadow_scene.vert.spv")),
        loadShaderModule(device->logicalDevice, shaderPath("scene/shadow_scene.frag.spv")),
    };
    if (!shaderModules[0] || !shaderModules[1]) {
        for (vk::ShaderModule shaderModule : shaderModules) {
            if (shaderModule != VK_NULL_HANDLE) device->logicalDevice.destroyShaderModule(shaderModule);
        }
        destroy();
        return;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{
        makeStage(shaderModules[0], vk::ShaderStageFlagBits::eVertex),
        makeStage(shaderModules[1], vk::ShaderStageFlagBits::eFragment),
    };

    vk::VertexInputBindingDescription vertexBinding{0, sizeof(vkmglTF::Vertex), vk::VertexInputRate::eVertex};
    const std::array<vk::VertexInputAttributeDescription, 5> vertexAttributes = sceneVertexAttributes();
    vk::PipelineVertexInputStateCreateInfo vertexInputState{};
    vertexInputState.setVertexBindingDescriptionCount(1)
        .setPVertexBindingDescriptions(&vertexBinding)
        .setVertexAttributeDescriptions(vertexAttributes);

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
    inputAssemblyState.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rasterizationState{};
    rasterizationState.setPolygonMode(vk::PolygonMode::eFill)
        // Front-face culling is a common shadow-map trick for closed/thin
        // assets such as Sponza. With culling disabled, back faces and mirrored
        // two-sided surfaces can become the nearest light-space depth and make
        // interior receivers appear "behind" an artificial blocker everywhere.
        .setCullMode(vk::CullModeFlagBits::eFront)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(VK_TRUE)
        .setDepthBiasConstantFactor(1.25f)
        .setDepthBiasSlopeFactor(1.75f)
        .setLineWidth(1.0f);

    vk::PipelineMultisampleStateCreateInfo multisampleState{};
    multisampleState.setRasterizationSamples(vk::SampleCountFlagBits::e1);

    vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
        .setStencilTestEnable(VK_FALSE);

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
        .setPDynamicState(&dynamicState)
        .setLayout(pipelineLayoutHandle)
        .setRenderPass(renderPassHandle)
        .setSubpass(0);
    VK_CHECK_RESULT(device->logicalDevice.createGraphicsPipelines(pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelineHandle));

    for (vk::ShaderModule shaderModule : shaderModules) {
        device->logicalDevice.destroyShaderModule(shaderModule);
    }
}

void ShadowPass::destroy()
{
    if (device == nullptr) {
        return;
    }
    if (pipelineHandle != VK_NULL_HANDLE) device->logicalDevice.destroyPipeline(pipelineHandle);
    if (pipelineLayoutHandle != VK_NULL_HANDLE) device->logicalDevice.destroyPipelineLayout(pipelineLayoutHandle);
    if (framebufferHandle != VK_NULL_HANDLE) device->logicalDevice.destroyFramebuffer(framebufferHandle);
    if (renderPassHandle != VK_NULL_HANDLE) device->logicalDevice.destroyRenderPass(renderPassHandle);
    depthTexture.destroy();

    pipelineHandle = VK_NULL_HANDLE;
    pipelineLayoutHandle = VK_NULL_HANDLE;
    framebufferHandle = VK_NULL_HANDLE;
    renderPassHandle = VK_NULL_HANDLE;
    depthTexture = vkm::Texture2D{};
    shadowData = {};
    device = nullptr;
}

void ShadowPass::record(vk::CommandBuffer commandBuffer,
                        scene::Scene& scene,
                        const scene::SceneGpuData& sceneGpuData)
{
    if (device == nullptr || pipelineHandle == VK_NULL_HANDLE || !sceneGpuData.isCreated()) {
        return;
    }
    shadowData = buildShadowData(scene, sceneGpuData, shadowExtent);
    if (shadowData.params.x <= 0.0f) {
        return;
    }

    vk::ClearValue clearValue{};
    clearValue.setDepthStencil(vk::ClearDepthStencilValue(1.0f, 0));
    vk::RenderPassBeginInfo beginInfo{};
    beginInfo.setRenderPass(renderPassHandle)
        .setFramebuffer(framebufferHandle)
        .setRenderArea(vk::Rect2D{vk::Offset2D{0, 0}, shadowExtent})
        .setClearValueCount(1)
        .setPClearValues(&clearValue);
    commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);

    vk::Viewport viewport{};
    viewport.setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(shadowExtent.width))
        .setHeight(static_cast<float>(shadowExtent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vk::Rect2D scissor{vk::Offset2D{0, 0}, shadowExtent};
    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, scissor);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineHandle);

    ShadowPushConstants pushConstants{};
    pushConstants.lightViewProjection = shadowData.lightViewProjection;
    commandBuffer.pushConstants(pipelineLayoutHandle, vk::ShaderStageFlagBits::eVertex, 0, sizeof(pushConstants), &pushConstants);

    // Render opaque and alpha-masked geometry into the same depth map. The
    // fragment shader only discards masked texels; all color/material work is
    // intentionally skipped because shadow maps store visibility, not shading.
    scene.model().draw(
        commandBuffer,
        vkmglTF::RenderFlags::BindImages | vkmglTF::RenderFlags::PushMaterialConstants | vkmglTF::RenderFlags::RenderOpaqueNodes,
        pipelineLayoutHandle,
        0);
    scene.model().draw(
        commandBuffer,
        vkmglTF::RenderFlags::BindImages | vkmglTF::RenderFlags::PushMaterialConstants | vkmglTF::RenderFlags::RenderAlphaMaskedNodes,
        pipelineLayoutHandle,
        0);

    commandBuffer.endRenderPass();
}

} // namespace renderer
