#include "debug/ProbeVisualizer.h"

#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace debug {
namespace {

struct ProbeDebugPushConstants {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 volumeOriginAndSize{0.0f};
    glm::vec4 probeSpacing{0.0f};
    glm::uvec4 probeCounts{0u};
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
        OutputMessage("[ProbeVisualizer] Failed to open shader: {}\n", path);
        return VK_NULL_HANDLE;
    }

    const std::streamsize fileSize = shaderFile.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        OutputMessage("[ProbeVisualizer] Invalid SPIR-V size for shader {}: {} bytes\n", path, static_cast<int64_t>(fileSize));
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirvWords(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    shaderFile.seekg(0, std::ios::beg);
    shaderFile.read(reinterpret_cast<char*>(spirvWords.data()), fileSize);
    if (spirvWords.empty() || spirvWords.front() != 0x07230203u) {
        OutputMessage("[ProbeVisualizer] Shader is not valid SPIR-V: {}\n", path);
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

} // namespace

void ProbeVisualizer::create(vkm::VKMDevice* inDevice, vk::RenderPass renderPass, vk::PipelineCache pipelineCache)
{
    destroy();
    if (inDevice == nullptr || renderPass == VK_NULL_HANDLE) {
        OutputMessage("[ProbeVisualizer] create requires a valid device and render pass\n");
        return;
    }

    device = inDevice;

    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(ProbeDebugPushConstants));
    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setPushConstantRangeCount(1).setPPushConstantRanges(&pushConstantRange);
    VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(&pipelineLayoutCreateInfo, nullptr, &pipelineLayoutHandle));

    std::array<vk::ShaderModule, 2> shaderModules{
        loadShaderModule(device->logicalDevice, shaderPath("debug/probe_debug.vert.spv")),
        loadShaderModule(device->logicalDevice, shaderPath("debug/probe_debug.frag.spv")),
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

void ProbeVisualizer::destroy()
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

    pipelineHandle = VK_NULL_HANDLE;
    pipelineLayoutHandle = VK_NULL_HANDLE;
    device = nullptr;
}

void ProbeVisualizer::draw(vk::CommandBuffer commandBuffer,
                           const ddgi::DDGIVolume& volume,
                           const Camera& camera,
                           vk::Extent2D framebufferExtent)
{
    if (device == nullptr || pipelineHandle == VK_NULL_HANDLE) {
        return;
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

    ProbeDebugPushConstants pushConstants{};
    pushConstants.viewProjection = camera.matrices.perspective * camera.matrices.view;
    // Use a screen-space billboard size large enough to validate the probe
    // grid visually before we replace these quads with richer sphere meshes.
    pushConstants.volumeOriginAndSize = glm::vec4(volume.description().origin, 0.055f);
    pushConstants.probeSpacing = glm::vec4(volume.description().probeSpacing, 0.0f);
    pushConstants.probeCounts = glm::uvec4(volume.description().probeCounts, 0u);
    commandBuffer.pushConstants(
        pipelineLayoutHandle,
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(ProbeDebugPushConstants),
        &pushConstants);

    const glm::uvec3 probeCounts = volume.description().probeCounts;
    const uint32_t totalProbeCount = probeCounts.x * probeCounts.y * probeCounts.z;
    commandBuffer.draw(6, totalProbeCount, 0, 0);
}

} // namespace debug
