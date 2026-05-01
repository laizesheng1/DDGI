#include "debug/ProbeVisualizer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>

namespace debug {
namespace {

struct ProbeDebugPushConstants {
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

void prepareBufferForCreate(vkm::Buffer& buffer,
                            vk::BufferUsageFlags usageFlags,
                            vk::MemoryPropertyFlags memoryPropertyFlags)
{
    buffer.usageFlags = usageFlags;
    buffer.memoryPropertyFlags = memoryPropertyFlags;
}

void createBuffer(vkm::VKMDevice& device,
                  vkm::Buffer& buffer,
                  vk::DeviceSize sizeBytes,
                  vk::BufferUsageFlags usageFlags,
                  vk::MemoryPropertyFlags memoryPropertyFlags,
                  void* initialData)
{
    prepareBufferForCreate(buffer, usageFlags, memoryPropertyFlags);
    buffer.createBuffer(sizeBytes, usageFlags, memoryPropertyFlags, initialData, &device);
    buffer.setupDescriptor(sizeBytes);
}

void appendSphereMesh(std::vector<ProbeVisualizerSphereVertex>& vertices,
                      std::vector<uint32_t>& indices,
                      uint32_t latitudeSegments,
                      uint32_t longitudeSegments)
{
    vertices.clear();
    indices.clear();

    for (uint32_t latitude = 0; latitude <= latitudeSegments; ++latitude) {
        const float v = static_cast<float>(latitude) / static_cast<float>(latitudeSegments);
        const float phi = v * glm::pi<float>();
        for (uint32_t longitude = 0; longitude <= longitudeSegments; ++longitude) {
            const float u = static_cast<float>(longitude) / static_cast<float>(longitudeSegments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 normal(
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta));
            vertices.push_back({normal, normal});
        }
    }

    const uint32_t ringVertexCount = longitudeSegments + 1u;
    for (uint32_t latitude = 0; latitude < latitudeSegments; ++latitude) {
        for (uint32_t longitude = 0; longitude < longitudeSegments; ++longitude) {
            const uint32_t a = latitude * ringVertexCount + longitude;
            const uint32_t b = a + ringVertexCount;
            const uint32_t c = b + 1u;
            const uint32_t d = a + 1u;
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(d);
        }
    }
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

    std::vector<ProbeVisualizerSphereVertex> sphereVertices{};
    std::vector<uint32_t> sphereIndices{};
    // The overlay should still read as a sphere, but it does not need many
    // triangles. Keeping the mesh coarse reduces vertex and fragment cost when
    // hundreds of probes are visible.
    appendSphereMesh(sphereVertices, sphereIndices, 6u, 8u);
    sphereIndexCount = static_cast<uint32_t>(sphereIndices.size());
    createBuffer(
        *device,
        sphereVertexBuffer,
        static_cast<vk::DeviceSize>(sphereVertices.size() * sizeof(ProbeVisualizerSphereVertex)),
        vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        sphereVertices.data());
    createBuffer(
        *device,
        sphereIndexBuffer,
        static_cast<vk::DeviceSize>(sphereIndices.size() * sizeof(uint32_t)),
        vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        sphereIndices.data());

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

    std::array<vk::VertexInputBindingDescription, 2> bindings{
        vk::VertexInputBindingDescription{0, sizeof(ProbeVisualizerSphereVertex), vk::VertexInputRate::eVertex},
        vk::VertexInputBindingDescription{1, sizeof(ProbeVisualizerInstanceData), vk::VertexInputRate::eInstance},
    };
    std::array<vk::VertexInputAttributeDescription, 4> attributes{
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, offsetof(ProbeVisualizerSphereVertex, position)},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, offsetof(ProbeVisualizerSphereVertex, normal)},
        vk::VertexInputAttributeDescription{2, 1, vk::Format::eR32G32B32A32Sfloat, offsetof(ProbeVisualizerInstanceData, positionAndRadius)},
        vk::VertexInputAttributeDescription{3, 1, vk::Format::eR32G32B32A32Sfloat, offsetof(ProbeVisualizerInstanceData, color)},
    };
    vk::PipelineVertexInputStateCreateInfo vertexInputState{};
    vertexInputState.setVertexBindingDescriptions(bindings)
        .setVertexAttributeDescriptions(attributes);

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
    inputAssemblyState.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rasterizationState{};
    rasterizationState.setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0f);

    vk::PipelineMultisampleStateCreateInfo multisampleState{};
    multisampleState.setRasterizationSamples(vk::SampleCountFlagBits::e1);

    vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
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

    if (instanceBuffer.mapped != nullptr) {
        instanceBuffer.unmap();
    }
    instanceBuffer.destroy();
    sphereIndexBuffer.destroy();
    sphereVertexBuffer.destroy();
    if (pipelineHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyPipeline(pipelineHandle);
    }
    if (pipelineLayoutHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyPipelineLayout(pipelineLayoutHandle);
    }

    pipelineHandle = VK_NULL_HANDLE;
    pipelineLayoutHandle = VK_NULL_HANDLE;
    sphereIndexCount = 0u;
    instanceCapacity = 0u;
    cachedProbeCount = 0u;
    instanceCpuData.clear();
    cachedVolumeDesc = ddgi::DDGIVolumeDesc{};
    instanceDataDirty = true;
    device = nullptr;
}

void ProbeVisualizer::draw(vk::CommandBuffer commandBuffer,
                           const ddgi::DDGIVolume& volume,
                           const Camera& camera,
                           vk::Extent2D framebufferExtent)
{
    if (device == nullptr || pipelineHandle == VK_NULL_HANDLE || sphereIndexCount == 0u) {
        return;
    }

    const ddgi::DDGIVolumeDesc& volumeDesc = volume.description();
    const uint32_t probeCount = volume.totalProbeCount();
    if (probeCount == 0u) {
        return;
    }

    if (instanceCapacity < probeCount || instanceBuffer.buffer == VK_NULL_HANDLE) {
        if (instanceBuffer.mapped != nullptr) {
            instanceBuffer.unmap();
        }
        instanceBuffer.destroy();
        instanceCapacity = probeCount;
        createBuffer(
            *device,
            instanceBuffer,
            static_cast<vk::DeviceSize>(instanceCapacity) * sizeof(ProbeVisualizerInstanceData),
            vk::BufferUsageFlagBits::eVertexBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            nullptr);
        VK_CHECK_RESULT(instanceBuffer.map(VK_WHOLE_SIZE));
        instanceDataDirty = true;
    }

    const bool layoutChanged =
        cachedProbeCount != probeCount ||
        cachedVolumeDesc.origin != volumeDesc.origin ||
        cachedVolumeDesc.probeSpacing != volumeDesc.probeSpacing ||
        cachedVolumeDesc.probeCounts != volumeDesc.probeCounts;

    if (layoutChanged) {
        cachedProbeCount = probeCount;
        cachedVolumeDesc = volumeDesc;
        instanceDataDirty = true;
    }

    if (instanceDataDirty) {
        instanceCpuData.resize(probeCount);
        const float sphereRadius = std::min(volumeDesc.probeSpacing.x,
                                            std::min(volumeDesc.probeSpacing.y, volumeDesc.probeSpacing.z)) * 0.10f;
        const glm::vec4 debugColor(0.10f, 0.85f, 1.00f, 1.0f);
        for (uint32_t probeIndex = 0; probeIndex < probeCount; ++probeIndex) {
            // Probe instance data is rebuilt only when the lattice changes.
            // That keeps the overlay aligned with the current volume while
            // avoiding per-frame CPU readback of DDGI ray buffers.
            instanceCpuData[probeIndex].positionAndRadius = glm::vec4(
                volume.probeWorldPosition(probeIndex, nullptr),
                sphereRadius);
            instanceCpuData[probeIndex].color = debugColor;
        }

        std::memcpy(instanceBuffer.mapped, instanceCpuData.data(), probeCount * sizeof(ProbeVisualizerInstanceData));
        instanceDataDirty = false;
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
    commandBuffer.pushConstants(
        pipelineLayoutHandle,
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(ProbeDebugPushConstants),
        &pushConstants);

    const std::array<vk::Buffer, 2> vertexBuffers{
        sphereVertexBuffer.buffer,
        instanceBuffer.buffer,
    };
    const std::array<vk::DeviceSize, 2> offsets{0, 0};
    commandBuffer.bindVertexBuffers(0, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(sphereIndexBuffer.buffer, 0, vk::IndexType::eUint32);
    commandBuffer.drawIndexed(sphereIndexCount, probeCount, 0, 0, 0);
}

} // namespace debug
