#include "debug/DebugAtlasWindow.h"

#include <algorithm>
#include <array>
#include <string>

namespace debug {
namespace {

constexpr uint32_t kTitleStripHeight = 28u;
constexpr uint32_t kPanelPadding = 8u;

void recordAtlasReadBarrier(vk::CommandBuffer commandBuffer, const ddgi::DDGIVolume& volume)
{
    const ddgi::DDGITextureSet& textures = volume.resources().textures();
    if (textures.irradiance.image == VK_NULL_HANDLE ||
        textures.depth.image == VK_NULL_HANDLE ||
        textures.depthSquared.image == VK_NULL_HANDLE) {
        return;
    }
    const std::array<vk::Image, 3> atlasImages{
        textures.irradiance.image,
        textures.depth.image,
        textures.depthSquared.image,
    };

    std::array<vk::ImageMemoryBarrier, 3> barriers{};
    vk::ImageSubresourceRange subresourceRange{};
    subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    for (size_t imageIndex = 0; imageIndex < atlasImages.size(); ++imageIndex) {
        barriers[imageIndex].setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(atlasImages[imageIndex])
            .setSubresourceRange(subresourceRange);
    }

    // The atlas debug window samples the same storage images that the main DDGI
    // trace/update pass just wrote. Both submissions run on the same queue, so
    // submission order guarantees execution order; this barrier adds the memory
    // dependency that makes those writes visible to fragment shader sampling.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        vk::PipelineStageFlagBits::eFragmentShader,
        {},
        {},
        {},
        barriers);
}

} // namespace

#if defined(_WIN32)
DebugAtlasWindow* DebugAtlasWindow::activeWindow = nullptr;

LRESULT CALLBACK DebugAtlasWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (activeWindow != nullptr) {
        LRESULT handledResult = 0;
        if (activeWindow->handleMessage(hWnd, uMsg, wParam, lParam, handledResult)) {
            return handledResult;
        }
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

bool DebugAtlasWindow::handleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
    if (hWnd != windowHandle) {
        return false;
    }

    switch (uMsg) {
    case WM_CLOSE:
        visible = false;
        userRequestedHide = true;
        ShowWindow(windowHandle, SW_HIDE);
        result = 0;
        return true;
    case WM_SIZE:
    {
        RECT clientRect{};
        GetClientRect(windowHandle, &clientRect);
        width = std::max<uint32_t>(1u, static_cast<uint32_t>(clientRect.right - clientRect.left));
        height = std::max<uint32_t>(1u, static_cast<uint32_t>(clientRect.bottom - clientRect.top));
        layoutTitleLabels();
        if (wParam != SIZE_MINIMIZED) {
            swapchainDirty = true;
        }
        result = 0;
        return true;
    }
    default:
        break;
    }
    return false;
}

bool DebugAtlasWindow::createWindow()
{
    if (windowInstance == nullptr) {
        return false;
    }

    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXA);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = windowInstance;
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = windowClassName.c_str();
    windowClass.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    if (RegisterClassExA(&windowClass) == 0) {
        const DWORD errorCode = GetLastError();
        if (errorCode != ERROR_CLASS_ALREADY_EXISTS) {
            OutputMessage("[DebugAtlasWindow] Failed to register window class (error={})\n", errorCode);
            return false;
        }
    }

    RECT windowRect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    windowHandle = CreateWindowExA(
        0,
        windowClassName.c_str(),
        "DDGI Atlas Debug",
        WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        windowInstance,
        nullptr);
    if (windowHandle == nullptr) {
        OutputMessage("[DebugAtlasWindow] Failed to create window (error={})\n", GetLastError());
        return false;
    }

    activeWindow = this;
    const std::array<const char*, 3> labelNames{
        "Irradiance",
        "Depth",
        "Depth Squared",
    };
    for (size_t labelIndex = 0; labelIndex < std::size(titleLabels); ++labelIndex) {
        titleLabels[labelIndex] = CreateWindowExA(
            0,
            "STATIC",
            labelNames[labelIndex],
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0,
            0,
            10,
            10,
            windowHandle,
            nullptr,
            windowInstance,
            nullptr);
    }

    ShowWindow(windowHandle, SW_SHOW);
    UpdateWindow(windowHandle);
    layoutTitleLabels();
    open = true;
    visible = true;
    userRequestedHide = false;
    return true;
}

void DebugAtlasWindow::destroyWindow()
{
    for (HWND& titleLabel : titleLabels) {
        if (titleLabel != nullptr) {
            DestroyWindow(titleLabel);
            titleLabel = nullptr;
        }
    }
    if (windowHandle != nullptr) {
        DestroyWindow(windowHandle);
        windowHandle = nullptr;
    }
    activeWindow = nullptr;
    open = false;
}

void DebugAtlasWindow::layoutTitleLabels() const
{
    if (windowHandle == nullptr) {
        return;
    }

    const uint32_t clientWidth = std::max(1u, width);
    const uint32_t panelWidth = std::max(1u, (clientWidth - (kPanelPadding * 4u)) / 3u);
    for (uint32_t panelIndex = 0; panelIndex < 3u; ++panelIndex) {
        if (titleLabels[panelIndex] == nullptr) {
            continue;
        }
        const uint32_t x = kPanelPadding + panelIndex * (panelWidth + kPanelPadding);
        MoveWindow(
            titleLabels[panelIndex],
            static_cast<int>(x),
            2,
            static_cast<int>(panelWidth),
            static_cast<int>(kTitleStripHeight - 4u),
            TRUE);
    }
}
#endif

void DebugAtlasWindow::createRenderPass()
{
    if (device == nullptr || swapchain.colorFormat == vk::Format::eUndefined) {
        return;
    }

    vk::AttachmentDescription colorAttachment{};
    colorAttachment.setFormat(swapchain.colorFormat)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    vk::AttachmentReference colorReference{0, vk::ImageLayout::eColorAttachmentOptimal};
    vk::SubpassDescription subpass{};
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(colorReference);

    vk::SubpassDependency dependency{};
    dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setSrcAccessMask(vk::AccessFlagBits::eNone)
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);

    vk::RenderPassCreateInfo renderPassCreateInfo{};
    renderPassCreateInfo.setAttachments(colorAttachment)
        .setSubpasses(subpass)
        .setDependencies(dependency);
    VK_CHECK_RESULT(device->logicalDevice.createRenderPass(&renderPassCreateInfo, nullptr, &renderPassHandle));
}

void DebugAtlasWindow::destroyRenderPass()
{
    if (device != nullptr && renderPassHandle != VK_NULL_HANDLE) {
        device->logicalDevice.destroyRenderPass(renderPassHandle);
    }
    renderPassHandle = VK_NULL_HANDLE;
}

void DebugAtlasWindow::createFramebuffers()
{
    if (device == nullptr || renderPassHandle == VK_NULL_HANDLE) {
        return;
    }

    framebuffers.resize(swapchain.imageViews.size());
    for (size_t framebufferIndex = 0; framebufferIndex < framebuffers.size(); ++framebufferIndex) {
        const vk::ImageView attachment = swapchain.imageViews[framebufferIndex];
        vk::FramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.setRenderPass(renderPassHandle)
            .setAttachmentCount(1)
            .setPAttachments(&attachment)
            .setWidth(width)
            .setHeight(height)
            .setLayers(1);
        VK_CHECK_RESULT(device->logicalDevice.createFramebuffer(&framebufferCreateInfo, nullptr, &framebuffers[framebufferIndex]));
    }
}

void DebugAtlasWindow::destroyFramebuffers()
{
    if (device == nullptr) {
        framebuffers.clear();
        return;
    }
    for (vk::Framebuffer framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            device->logicalDevice.destroyFramebuffer(framebuffer);
        }
    }
    framebuffers.clear();
}

void DebugAtlasWindow::createSyncObjects()
{
    if (device == nullptr) {
        return;
    }

    vk::FenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
    vk::SemaphoreCreateInfo semaphoreCreateInfo{};
    for (uint32_t frameIndex = 0; frameIndex < kBufferedFrames; ++frameIndex) {
        VK_CHECK_RESULT(device->logicalDevice.createFence(&fenceCreateInfo, nullptr, &inFlightFences[frameIndex]));
        VK_CHECK_RESULT(device->logicalDevice.createSemaphore(&semaphoreCreateInfo, nullptr, &imageAvailableSemaphores[frameIndex]));
    }

    renderFinishedSemaphores.resize(swapchain.images.size(), VK_NULL_HANDLE);
    for (vk::Semaphore& renderFinishedSemaphore : renderFinishedSemaphores) {
        VK_CHECK_RESULT(device->logicalDevice.createSemaphore(&semaphoreCreateInfo, nullptr, &renderFinishedSemaphore));
    }
}

void DebugAtlasWindow::destroySyncObjects()
{
    if (device == nullptr) {
        imageAvailableSemaphores = {};
        renderFinishedSemaphores.clear();
        inFlightFences = {};
        return;
    }
    for (uint32_t frameIndex = 0; frameIndex < kBufferedFrames; ++frameIndex) {
        if (inFlightFences[frameIndex] != VK_NULL_HANDLE) {
            device->logicalDevice.destroyFence(inFlightFences[frameIndex]);
        }
        if (imageAvailableSemaphores[frameIndex] != VK_NULL_HANDLE) {
            device->logicalDevice.destroySemaphore(imageAvailableSemaphores[frameIndex]);
        }
    }
    for (vk::Semaphore renderFinishedSemaphore : renderFinishedSemaphores) {
        if (renderFinishedSemaphore != VK_NULL_HANDLE) {
            device->logicalDevice.destroySemaphore(renderFinishedSemaphore);
        }
    }
    imageAvailableSemaphores = {};
    renderFinishedSemaphores.clear();
    inFlightFences = {};
}

void DebugAtlasWindow::createCommandBuffers()
{
    if (device == nullptr) {
        return;
    }

    vk::CommandPoolCreateInfo commandPoolCreateInfo{};
    commandPoolCreateInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
        .setQueueFamilyIndex(swapchain.queueNodeIndex);
    VK_CHECK_RESULT(device->logicalDevice.createCommandPool(&commandPoolCreateInfo, nullptr, &commandPool));

    vk::CommandBufferAllocateInfo commandBufferAllocateInfo{};
    commandBufferAllocateInfo.setCommandPool(commandPool)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(kBufferedFrames);
    VK_CHECK_RESULT(device->logicalDevice.allocateCommandBuffers(&commandBufferAllocateInfo, commandBuffers.data()));
}

void DebugAtlasWindow::destroyCommandBuffers()
{
    if (device == nullptr) {
        commandBuffers = {};
        commandPool = VK_NULL_HANDLE;
        return;
    }
    if (commandPool != VK_NULL_HANDLE) {
        device->logicalDevice.freeCommandBuffers(commandPool, kBufferedFrames, commandBuffers.data());
        device->logicalDevice.destroyCommandPool(commandPool);
    }
    commandBuffers = {};
    commandPool = VK_NULL_HANDLE;
}

void DebugAtlasWindow::recreateSwapchain()
{
    if (device == nullptr || windowHandle == nullptr || width == 0u || height == 0u) {
        return;
    }

    device->logicalDevice.waitIdle();
    destroyFramebuffers();
    destroySyncObjects();
    atlasRenderer.destroy();
    destroyRenderPass();
    VK_CHECK_RESULT(swapchain.CreateSwapchain(width, height, false));
    width = swapchain.swapchainCreateInfo.imageExtent.width;
    height = swapchain.swapchainCreateInfo.imageExtent.height;
    createRenderPass();
    atlasRenderer.create(device, renderPassHandle, pipelineCache);
    createFramebuffers();
    createSyncObjects();
    layoutTitleLabels();
    swapchainDirty = false;
}

void DebugAtlasWindow::processMessages()
{
#if defined(_WIN32)
    if (windowHandle == nullptr) {
        return;
    }

    MSG message{};
    while (PeekMessage(&message, windowHandle, 0, 0, PM_REMOVE) != FALSE) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
#endif
}

void DebugAtlasWindow::create(vk::Instance inInstance,
                              vk::PhysicalDevice inPhysicalDevice,
                              vkm::VKMDevice* inDevice,
                              vk::Queue inQueue,
                              vk::PipelineCache inPipelineCache,
#if defined(_WIN32)
                              HINSTANCE inWindowInstance
#else
                              void* inWindowInstance
#endif
)
{
    destroy();

    if (inDevice == nullptr || inInstance == VK_NULL_HANDLE || inPhysicalDevice == VK_NULL_HANDLE || inQueue == VK_NULL_HANDLE) {
        OutputMessage("[DebugAtlasWindow] create requires a valid shared Vulkan context\n");
        return;
    }

    instance = inInstance;
    physicalDevice = inPhysicalDevice;
    device = inDevice;
    queue = inQueue;
    pipelineCache = inPipelineCache;
#if defined(_WIN32)
    windowInstance = inWindowInstance;
    if (!createWindow()) {
        return;
    }
#else
    (void)inWindowInstance;
    return;
#endif

    swapchain.setContext(instance, physicalDevice, device->logicalDevice);
    swapchain.initSurface(windowInstance, windowHandle);
    VK_CHECK_RESULT(swapchain.CreateSwapchain(width, height, false));
    width = swapchain.swapchainCreateInfo.imageExtent.width;
    height = swapchain.swapchainCreateInfo.imageExtent.height;
    createRenderPass();
    atlasRenderer.create(device, renderPassHandle, pipelineCache);
    createFramebuffers();
    createCommandBuffers();
    createSyncObjects();
}

void DebugAtlasWindow::destroy()
{
    if (device != nullptr) {
        device->logicalDevice.waitIdle();
    }

    destroySyncObjects();
    destroyCommandBuffers();
    destroyFramebuffers();
    atlasRenderer.destroy();
    destroyRenderPass();
    swapchain.cleanup();
#if defined(_WIN32)
    destroyWindow();
#endif

    currentFrame = 0u;
    currentImageIndex = 0u;
    open = false;
    visible = true;
    swapchainDirty = false;
    userRequestedHide = false;
    queue = VK_NULL_HANDLE;
    pipelineCache = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    device = nullptr;
}

bool DebugAtlasWindow::consumeHideRequest()
{
    const bool hideRequested = userRequestedHide;
    userRequestedHide = false;
    return hideRequested;
}

void DebugAtlasWindow::update(const ddgi::DDGIVolume& volume, bool shouldBeVisible)
{
    if (device == nullptr || !open) {
        return;
    }

#if defined(_WIN32)
    if (windowHandle != nullptr && visible != shouldBeVisible) {
        visible = shouldBeVisible;
        ShowWindow(windowHandle, visible ? SW_SHOW : SW_HIDE);
    }
#endif

    processMessages();
    if (!visible ||
        width == 0u ||
        height == 0u ||
        renderPassHandle == VK_NULL_HANDLE ||
        framebuffers.empty() ||
        renderFinishedSemaphores.empty()) {
        return;
    }
    if (swapchainDirty) {
        recreateSwapchain();
        if (swapchainDirty) {
            return;
        }
    }

    VK_CHECK_RESULT(device->logicalDevice.waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX));
    VK_CHECK_RESULT(device->logicalDevice.resetFences(1, &inFlightFences[currentFrame]));

    vk::Result acquireResult = swapchain.acquireNextImage(imageAvailableSemaphores[currentFrame], currentImageIndex);
    if (acquireResult == vk::Result::eErrorOutOfDateKHR || acquireResult == vk::Result::eSuboptimalKHR) {
        swapchainDirty = true;
        recreateSwapchain();
        return;
    }
    VK_CHECK_RESULT(acquireResult);

    vk::CommandBuffer commandBuffer = commandBuffers[currentFrame];
    commandBuffer.reset();

    vk::CommandBufferBeginInfo beginInfo{};
    VK_CHECK_RESULT(commandBuffer.begin(&beginInfo));

    recordAtlasReadBarrier(commandBuffer, volume);

    const std::array<vk::ClearValue, 1> clearValues{
        vk::ClearValue{vk::ClearColorValue(std::array<float, 4>{0.03f, 0.03f, 0.035f, 1.0f})},
    };
    vk::RenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.setRenderPass(renderPassHandle)
        .setFramebuffer(framebuffers[currentImageIndex])
        .setRenderArea(vk::Rect2D{vk::Offset2D{0, 0}, vk::Extent2D{width, height}})
        .setClearValues(clearValues);
    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

    const uint32_t panelWidth = std::max(1u, (width - (kPanelPadding * 4u)) / 3u);
    const uint32_t panelHeight = std::max(1u, height - kTitleStripHeight - (kPanelPadding * 2u));
    const uint32_t panelY = kTitleStripHeight + kPanelPadding;
    const std::array<ddgi::DebugTexture, 3> textures{
        ddgi::DebugTexture::Irradiance,
        ddgi::DebugTexture::Depth,
        ddgi::DebugTexture::DepthSquared,
    };
    for (uint32_t panelIndex = 0; panelIndex < 3u; ++panelIndex) {
        const uint32_t panelX = kPanelPadding + panelIndex * (panelWidth + kPanelPadding);
        vk::Viewport viewport{};
        viewport.setX(static_cast<float>(panelX))
            .setY(static_cast<float>(panelY))
            .setWidth(static_cast<float>(panelWidth))
            .setHeight(static_cast<float>(panelHeight))
            .setMinDepth(0.0f)
            .setMaxDepth(1.0f);
        vk::Rect2D scissor{vk::Offset2D{static_cast<int32_t>(panelX), static_cast<int32_t>(panelY)},
                           vk::Extent2D{panelWidth, panelHeight}};
        atlasRenderer.draw(commandBuffer, volume, textures[panelIndex], viewport, scissor);
    }

    commandBuffer.endRenderPass();
    commandBuffer.end();

    const std::array<vk::Semaphore, 1> waitSemaphores{imageAvailableSemaphores[currentFrame]};
    const std::array<vk::PipelineStageFlags, 1> waitStages{vk::PipelineStageFlagBits::eColorAttachmentOutput};
    const std::array<vk::CommandBuffer, 1> submitCommandBuffers{commandBuffer};
    const std::array<vk::Semaphore, 1> signalSemaphores{renderFinishedSemaphores[currentImageIndex]};
    vk::SubmitInfo submitInfo{};
    submitInfo.setWaitSemaphores(waitSemaphores)
        .setWaitDstStageMask(waitStages)
        .setCommandBuffers(submitCommandBuffers)
        .setSignalSemaphores(signalSemaphores);
    VK_CHECK_RESULT(queue.submit(1, &submitInfo, inFlightFences[currentFrame]));

    const std::array<vk::SwapchainKHR, 1> presentSwapchains{swapchain.swapChain};
    const std::array<uint32_t, 1> presentImageIndices{currentImageIndex};
    vk::PresentInfoKHR presentInfo{};
    presentInfo.setWaitSemaphores(signalSemaphores)
        .setSwapchains(presentSwapchains)
        .setImageIndices(presentImageIndices);

    const vk::Result presentResult = static_cast<vk::Result>(
        VULKAN_HPP_DEFAULT_DISPATCHER.vkQueuePresentKHR(
            static_cast<VkQueue>(queue),
            reinterpret_cast<const VkPresentInfoKHR*>(&presentInfo)));
    if (presentResult == vk::Result::eErrorOutOfDateKHR ||
        presentResult == vk::Result::eSuboptimalKHR ||
        presentResult == vk::Result::eErrorSurfaceLostKHR) {
        swapchainDirty = true;
        if (visible) {
            recreateSwapchain();
        }
    } else {
        VK_CHECK_RESULT(presentResult);
    }

    currentFrame = (currentFrame + 1u) % kBufferedFrames;
}

} // namespace debug
