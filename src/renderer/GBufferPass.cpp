#include "renderer/GBufferPass.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "glTFModel.h"

namespace renderer {
namespace {

struct GBufferPushConstants {
    glm::mat4 viewProjection{1.0f};
};

constexpr vk::Format kWorldPositionFormat = vk::Format::eR16G16B16A16Sfloat;
constexpr vk::Format kNormalFormat = vk::Format::eR16G16B16A16Sfloat;
constexpr vk::Format kAlbedoFormat = vk::Format::eR8G8B8A8Unorm;
constexpr vk::Format kMaterialFormat = vk::Format::eR8G8B8A8Unorm;

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
        OutputMessage("[GBufferPass] Failed to open shader: {}\n", path);
        return VK_NULL_HANDLE;
    }

    const std::streamsize fileSize = shaderFile.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        OutputMessage("[GBufferPass] Invalid SPIR-V size for shader {}: {} bytes\n", path, static_cast<int64_t>(fileSize));
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirvWords(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    shaderFile.seekg(0, std::ios::beg);
    shaderFile.read(reinterpret_cast<char*>(spirvWords.data()), fileSize);
    if (spirvWords.empty() || spirvWords.front() != 0x07230203u) {
        OutputMessage("[GBufferPass] Shader is not valid SPIR-V: {}\n", path);
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

bool formatHasStencil(vk::Format format)
{
    return format == vk::Format::eD32SfloatS8Uint ||
        format == vk::Format::eD24UnormS8Uint ||
        format == vk::Format::eD16UnormS8Uint;
}

void createAttachmentTexture(vkm::VKMDevice& device,
                             vkm::Texture2D& texture,
                             vk::Extent2D extent,
                             vk::Format format,
                             vk::ImageUsageFlags usage,
                             vk::ImageAspectFlags viewAspectMask,
                             vk::ImageLayout descriptorLayout)
{
    texture = vkm::Texture2D(&device);
    texture.width = extent.width;
    texture.height = extent.height;
    texture.mipLevels = 1;
    texture.layerCount = 1;

    vk::ImageCreateInfo imageCreateInfo{};
    texture.initImageCreateInfo(imageCreateInfo, format, usage);
    VK_CHECK_RESULT(device.logicalDevice.createImage(&imageCreateInfo, nullptr, &texture.image));
    texture.allocImageDeviceMem(vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(viewAspectMask)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
    texture.CreateDefaultSampler(vk::SamplerAddressMode::eClampToEdge);
    texture.CreateImageview(subresourceRange, format, vk::ImageViewType::e2D);
    texture.imageLayout = descriptorLayout;
    texture.updateDescriptor();
}

void destroyTexture(vkm::Texture2D& texture)
{
    texture.destroy();
    texture = vkm::Texture2D{};
}

} // namespace

void GBufferPass::create(vkm::VKMDevice* inDevice,
                         vk::PipelineCache pipelineCache,
                         vk::Extent2D inFramebufferExtent,
                         vk::Format inDepthFormat)
{
    destroy();
    if (inDevice == nullptr) {
        OutputMessage("[GBufferPass] create received a null device\n");
        return;
    }
    if (vkmglTF::descriptorSetLayoutImage == VK_NULL_HANDLE) {
        OutputMessage("[GBufferPass] glTF material descriptor layout is missing\n");
        return;
    }

    device = inDevice;
    framebufferExtent = inFramebufferExtent;
    depthFormat = inDepthFormat;

    createAttachmentTexture(
        *device,
        worldPositionTexture,
        framebufferExtent,
        kWorldPositionFormat,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::ImageAspectFlagBits::eColor,
        vk::ImageLayout::eShaderReadOnlyOptimal);
    createAttachmentTexture(
        *device,
        normalTexture,
        framebufferExtent,
        kNormalFormat,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::ImageAspectFlagBits::eColor,
        vk::ImageLayout::eShaderReadOnlyOptimal);
    createAttachmentTexture(
        *device,
        albedoTexture,
        framebufferExtent,
        kAlbedoFormat,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::ImageAspectFlagBits::eColor,
        vk::ImageLayout::eShaderReadOnlyOptimal);
    createAttachmentTexture(
        *device,
        materialTexture,
        framebufferExtent,
        kMaterialFormat,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::ImageAspectFlagBits::eColor,
        vk::ImageLayout::eShaderReadOnlyOptimal);

    vk::ImageAspectFlags depthAspectMask = vk::ImageAspectFlagBits::eDepth;
    if (formatHasStencil(depthFormat)) {
        depthAspectMask |= vk::ImageAspectFlagBits::eStencil;
    }
    depthBarrierAspectMask = depthAspectMask;
    createAttachmentTexture(
        *device,
        depthTexture,
        framebufferExtent,
        depthFormat,
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::ImageAspectFlagBits::eDepth,
        vk::ImageLayout::eDepthStencilReadOnlyOptimal);

    std::array<vk::AttachmentDescription, 5> attachments{};
    for (vk::AttachmentDescription& attachment : attachments) {
        attachment.setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined);
    }
    attachments[0].setFormat(kWorldPositionFormat).setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    attachments[1].setFormat(kNormalFormat).setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    attachments[2].setFormat(kAlbedoFormat).setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    attachments[3].setFormat(kMaterialFormat).setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    attachments[4].setFormat(depthFormat).setFinalLayout(vk::ImageLayout::eDepthStencilReadOnlyOptimal);

    std::array<vk::AttachmentReference, 4> colorAttachments{
        vk::AttachmentReference{0, vk::ImageLayout::eColorAttachmentOptimal},
        vk::AttachmentReference{1, vk::ImageLayout::eColorAttachmentOptimal},
        vk::AttachmentReference{2, vk::ImageLayout::eColorAttachmentOptimal},
        vk::AttachmentReference{3, vk::ImageLayout::eColorAttachmentOptimal},
    };
    vk::AttachmentReference depthAttachment{4, vk::ImageLayout::eDepthStencilAttachmentOptimal};

    vk::SubpassDescription subpass{};
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(colorAttachments)
        .setPDepthStencilAttachment(&depthAttachment);

    std::array<vk::SubpassDependency, 2> dependencies{};
    dependencies[0].setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eBottomOfPipe)
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
    dependencies[1].setSrcSubpass(0)
        .setDstSubpass(VK_SUBPASS_EXTERNAL)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests)
        .setDstStageMask(vk::PipelineStageFlagBits::eFragmentShader)
        .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

    vk::RenderPassCreateInfo renderPassCreateInfo{};
    renderPassCreateInfo.setAttachments(attachments)
        .setSubpasses(subpass)
        .setDependencies(dependencies);
    VK_CHECK_RESULT(device->logicalDevice.createRenderPass(&renderPassCreateInfo, nullptr, &renderPassHandle));

    std::array<vk::ImageView, 5> attachmentViews{
        worldPositionTexture.imageView,
        normalTexture.imageView,
        albedoTexture.imageView,
        materialTexture.imageView,
        depthTexture.imageView,
    };
    vk::FramebufferCreateInfo framebufferCreateInfo{};
    framebufferCreateInfo.setRenderPass(renderPassHandle)
        .setAttachments(attachmentViews)
        .setWidth(framebufferExtent.width)
        .setHeight(framebufferExtent.height)
        .setLayers(1);
    VK_CHECK_RESULT(device->logicalDevice.createFramebuffer(&framebufferCreateInfo, nullptr, &framebufferHandle));

    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(GBufferPushConstants));

    vk::DescriptorSetLayout materialSetLayout = vkmglTF::descriptorSetLayoutImage;
    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setSetLayoutCount(1)
        .setPSetLayouts(&materialSetLayout)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&pushConstantRange);
    VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(&pipelineLayoutCreateInfo, nullptr, &pipelineLayoutHandle));

    std::array<vk::ShaderModule, 2> shaderModules{
        loadShaderModule(device->logicalDevice, shaderPath("scene/gbuffer_scene.vert.spv")),
        loadShaderModule(device->logicalDevice, shaderPath("scene/gbuffer_scene.frag.spv")),
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
    viewportState.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rasterizationState{};
    rasterizationState.setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0f);

    vk::PipelineMultisampleStateCreateInfo multisampleState{};
    multisampleState.setRasterizationSamples(vk::SampleCountFlagBits::e1);

    vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
        .setDepthBoundsTestEnable(VK_FALSE)
        .setStencilTestEnable(VK_FALSE);

    std::array<vk::PipelineColorBlendAttachmentState, 4> colorBlendAttachments{};
    for (vk::PipelineColorBlendAttachmentState& attachment : colorBlendAttachments) {
        attachment.setBlendEnable(VK_FALSE)
            .setColorWriteMask(vk::ColorComponentFlagBits::eR |
                               vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB |
                               vk::ColorComponentFlagBits::eA);
    }
    vk::PipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.setAttachments(colorBlendAttachments);

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
        .setRenderPass(renderPassHandle)
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

void GBufferPass::destroy()
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
    if (framebufferHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyFramebuffer(framebufferHandle);
    }
    if (renderPassHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyRenderPass(renderPassHandle);
    }

    destroyTexture(depthTexture);
    destroyTexture(materialTexture);
    destroyTexture(albedoTexture);
    destroyTexture(normalTexture);
    destroyTexture(worldPositionTexture);

    pipelineHandle = VK_NULL_HANDLE;
    pipelineLayoutHandle = VK_NULL_HANDLE;
    framebufferHandle = VK_NULL_HANDLE;
    renderPassHandle = VK_NULL_HANDLE;
    framebufferExtent = vk::Extent2D{};
    depthFormat = vk::Format::eUndefined;
    depthBarrierAspectMask = vk::ImageAspectFlagBits::eDepth;
    device = nullptr;
}

void GBufferPass::record(vk::CommandBuffer commandBuffer,
                         scene::Scene& scene,
                         const Camera& camera,
                         vk::Extent2D inFramebufferExtent)
{
    if (device == nullptr || pipelineHandle == VK_NULL_HANDLE) {
        return;
    }
    if (framebufferExtent.width != inFramebufferExtent.width || framebufferExtent.height != inFramebufferExtent.height) {
        OutputMessage("[GBufferPass] Extent changed from {}x{} to {}x{}; recreate renderer resources to match resize\n",
                      framebufferExtent.width,
                      framebufferExtent.height,
                      inFramebufferExtent.width,
                      inFramebufferExtent.height);
        return;
    }

    std::array<vk::ClearValue, 5> clearValues{};
    clearValues[0].setColor(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));
    clearValues[1].setColor(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));
    clearValues[2].setColor(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));
    clearValues[3].setColor(vk::ClearColorValue(std::array<float, 4>{1.0f, 0.0f, 1.0f, 0.0f}));
    clearValues[4].setDepthStencil(vk::ClearDepthStencilValue(1.0f, 0));

    vk::RenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.setRenderPass(renderPassHandle)
        .setFramebuffer(framebufferHandle)
        .setRenderArea(vk::Rect2D{vk::Offset2D{0, 0}, framebufferExtent})
        .setClearValues(clearValues);
    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

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

    GBufferPushConstants pushConstants{};
    pushConstants.viewProjection = camera.matrices.perspective * camera.matrices.view;
    commandBuffer.pushConstants(
        pipelineLayoutHandle,
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(GBufferPushConstants),
        &pushConstants);

    // Keep the draw ordering identical to the forward path so material classes
    // are filtered the same way. The GBuffer stores per-surface attributes that
    // the later fullscreen lighting pass consumes.
    scene.model().draw(
        commandBuffer,
        vkmglTF::RenderFlags::BindImages | vkmglTF::RenderFlags::RenderOpaqueNodes,
        pipelineLayoutHandle,
        0);
    scene.model().draw(
        commandBuffer,
        vkmglTF::RenderFlags::BindImages | vkmglTF::RenderFlags::RenderAlphaMaskedNodes,
        pipelineLayoutHandle,
        0);

    commandBuffer.endRenderPass();

    std::array<vk::ImageMemoryBarrier, 5> readBarriers{};
    const std::array<vk::Image, 5> images{
        worldPositionTexture.image,
        normalTexture.image,
        albedoTexture.image,
        materialTexture.image,
        depthTexture.image,
    };
    const std::array<vk::ImageLayout, 5> layouts{
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eDepthStencilReadOnlyOptimal,
    };
    const std::array<vk::ImageAspectFlags, 5> aspects{
        vk::ImageAspectFlagBits::eColor,
        vk::ImageAspectFlagBits::eColor,
        vk::ImageAspectFlagBits::eColor,
        vk::ImageAspectFlagBits::eColor,
        depthBarrierAspectMask,
    };
    for (size_t imageIndex = 0; imageIndex < readBarriers.size(); ++imageIndex) {
        vk::ImageSubresourceRange barrierSubresourceRange{};
        barrierSubresourceRange.setAspectMask(aspects[imageIndex])
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1);
        readBarriers[imageIndex].setOldLayout(layouts[imageIndex])
            .setNewLayout(layouts[imageIndex])
            .setSrcAccessMask(imageIndex == 4
                                  ? vk::AccessFlagBits::eDepthStencilAttachmentWrite
                                  : vk::AccessFlagBits::eColorAttachmentWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(images[imageIndex])
            .setSubresourceRange(barrierSubresourceRange);
    }

    // The lighting pass samples the freshly rendered GBuffer in the same frame.
    // Keeping the images in read-only layouts and inserting an explicit
    // attachment-write -> fragment-read barrier avoids hidden dependencies on
    // the swapchain render pass.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests,
        vk::PipelineStageFlagBits::eFragmentShader,
        {},
        {},
        {},
        readBarriers);
}

} // namespace renderer
