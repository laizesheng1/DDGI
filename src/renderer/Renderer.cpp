#include "renderer/Renderer.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "glTFModel.h"

namespace renderer {
namespace {

struct ScenePushConstants {
    glm::mat4 viewProjection{1.0f};
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
        OutputMessage("[Renderer] Failed to open shader: {}\n", path);
        return VK_NULL_HANDLE;
    }

    const std::streamsize fileSize = shaderFile.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        OutputMessage("[Renderer] Invalid SPIR-V size for shader {}: {} bytes\n", path, static_cast<int64_t>(fileSize));
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirvWords(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    shaderFile.seekg(0, std::ios::beg);
    shaderFile.read(reinterpret_cast<char*>(spirvWords.data()), fileSize);
    if (spirvWords.empty() || spirvWords.front() != 0x07230203u) {
        OutputMessage("[Renderer] Shader is not valid SPIR-V: {}\n", path);
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

std::array<vk::VertexInputAttributeDescription, 4> sceneVertexAttributes()
{
    return {
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, offsetof(vkmglTF::Vertex, pos)},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, offsetof(vkmglTF::Vertex, normal)},
        vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32Sfloat, offsetof(vkmglTF::Vertex, uv)},
        vk::VertexInputAttributeDescription{3, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(vkmglTF::Vertex, color)},
    };
}

} // namespace

void Renderer::create(vkm::VKMDevice* inDevice, vk::PipelineCache pipelineCache, vk::RenderPass renderPass)
{
    destroy();
    if (inDevice == nullptr) {
        OutputMessage("[Renderer] create received a null device\n");
        return;
    }
    if (vkmglTF::descriptorSetLayoutImage == VK_NULL_HANDLE) {
        OutputMessage("[Renderer] glTF material descriptor layout is missing. Load the scene before creating Renderer.\n");
        return;
    }

    device = inDevice;

    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(ScenePushConstants));

    vk::DescriptorSetLayout materialSetLayout = vkmglTF::descriptorSetLayoutImage;
    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setSetLayoutCount(1)
        .setPSetLayouts(&materialSetLayout)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&pushConstantRange);
    VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(&pipelineLayoutCreateInfo, nullptr, &forwardPipelineLayout));

    std::array<vk::ShaderModule, 2> shaderModules{
        loadShaderModule(device->logicalDevice, shaderPath("scene/forward_scene.vert.spv")),
        loadShaderModule(device->logicalDevice, shaderPath("scene/forward_scene.frag.spv")),
    };
    if (!shaderModules[0] || !shaderModules[1]) {
        for (vk::ShaderModule shaderModule : shaderModules) {
            if (shaderModule) {
                device->logicalDevice.destroyShaderModule(shaderModule);
            }
        }
        return;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{
        makeStage(shaderModules[0], vk::ShaderStageFlagBits::eVertex),
        makeStage(shaderModules[1], vk::ShaderStageFlagBits::eFragment),
    };

    vk::VertexInputBindingDescription vertexBinding{0, sizeof(vkmglTF::Vertex), vk::VertexInputRate::eVertex};
    const std::array<vk::VertexInputAttributeDescription, 4> vertexAttributes = sceneVertexAttributes();
    vk::PipelineVertexInputStateCreateInfo vertexInputState{};
    vertexInputState.setVertexBindingDescriptionCount(1)
        .setPVertexBindingDescriptions(&vertexBinding)
        .setVertexAttributeDescriptions(vertexAttributes);

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
    inputAssemblyState.setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(VK_FALSE);

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.setViewportCount(1)
        .setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rasterizationState{};
    rasterizationState.setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthClampEnable(VK_FALSE)
        .setRasterizerDiscardEnable(VK_FALSE)
        .setLineWidth(1.0f);

    vk::PipelineMultisampleStateCreateInfo multisampleState{};
    multisampleState.setRasterizationSamples(vk::SampleCountFlagBits::e1);

    vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
        .setDepthBoundsTestEnable(VK_FALSE)
        .setStencilTestEnable(VK_FALSE);

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.setBlendEnable(VK_FALSE)
        .setColorWriteMask(vk::ColorComponentFlagBits::eR |
                           vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB |
                           vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

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
        .setLayout(forwardPipelineLayout)
        .setRenderPass(renderPass)
        .setSubpass(0);

    VK_CHECK_RESULT(device->logicalDevice.createGraphicsPipelines(
        pipelineCache,
        1,
        &pipelineCreateInfo,
        nullptr,
        &forwardPipeline));

    for (vk::ShaderModule shaderModule : shaderModules) {
        device->logicalDevice.destroyShaderModule(shaderModule);
    }
}

void Renderer::destroy()
{
    if (device == nullptr) {
        return;
    }

    if (forwardPipeline) {
        device->logicalDevice.destroyPipeline(forwardPipeline);
    }
    if (forwardPipelineLayout) {
        device->logicalDevice.destroyPipelineLayout(forwardPipelineLayout);
    }
    forwardPipeline = VK_NULL_HANDLE;
    forwardPipelineLayout = VK_NULL_HANDLE;
    device = nullptr;
}

void Renderer::drawScene(vk::CommandBuffer commandBuffer,
                         scene::Scene& scene,
                         const Camera& camera,
                         vk::Extent2D framebufferExtent,
                         const ddgi::DDGIVolume*)
{
    if (forwardPipeline == VK_NULL_HANDLE || forwardPipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    vk::Viewport viewport{};
    viewport.setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(framebufferExtent.width))
        .setHeight(static_cast<float>(framebufferExtent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vk::Rect2D scissor{};
    scissor.setOffset(vk::Offset2D{0, 0})
        .setExtent(framebufferExtent);

    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, scissor);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, forwardPipeline);

    ScenePushConstants pushConstants{};
    pushConstants.viewProjection = camera.matrices.perspective * camera.matrices.view;
    commandBuffer.pushConstants(
        forwardPipelineLayout,
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(ScenePushConstants),
        &pushConstants);

    // vkmglTF::Model::drawNode() treats RenderOpaqueNodes,
    // RenderAlphaMaskedNodes and RenderAlphaBlendedNodes as mutually exclusive
    // filters. Passing more than one filter bit makes the later test overwrite
    // the earlier one, so draw the material classes in separate passes.
    scene.model().draw(
        commandBuffer,
        vkmglTF::RenderFlags::BindImages | vkmglTF::RenderFlags::RenderOpaqueNodes,
        forwardPipelineLayout,
        0);
    scene.model().draw(
        commandBuffer,
        vkmglTF::RenderFlags::BindImages | vkmglTF::RenderFlags::RenderAlphaMaskedNodes,
        forwardPipelineLayout,
        0);
}

} // namespace renderer
