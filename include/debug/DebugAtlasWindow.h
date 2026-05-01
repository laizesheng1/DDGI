#pragma once

#include <array>
#include <string>
#include <vector>

#include "TextureVisualizer.h"
#include "Swapchain.h"

namespace debug {

/**
 * Independent Win32 + Vulkan window used to inspect DDGI atlases outside the
 * main scene swapchain. Keeping atlas inspection in a second swapchain avoids
 * consuming main-window pixels and makes it easier to leave the debug view open
 * while navigating the scene camera.
 */
class DebugAtlasWindow {
private:
    static constexpr uint32_t kBufferedFrames = 2u;

    vkm::VKMDevice* device{nullptr};
    vk::Instance instance{VK_NULL_HANDLE};
    vk::PhysicalDevice physicalDevice{VK_NULL_HANDLE};
    vk::Queue queue{VK_NULL_HANDLE};
    vk::PipelineCache pipelineCache{VK_NULL_HANDLE};
    SwainChain swapchain{};
    TextureVisualizer atlasRenderer{};
    vk::CommandPool commandPool{VK_NULL_HANDLE};
    vk::RenderPass renderPassHandle{VK_NULL_HANDLE};
    std::vector<vk::Framebuffer> framebuffers{};
    std::array<vk::CommandBuffer, kBufferedFrames> commandBuffers{};
    std::array<vk::Semaphore, kBufferedFrames> imageAvailableSemaphores{};
    std::vector<vk::Semaphore> renderFinishedSemaphores{};
    std::array<vk::Fence, kBufferedFrames> inFlightFences{};
    uint32_t currentFrame{0u};
    uint32_t currentImageIndex{0u};
    uint32_t width{960u};
    uint32_t height{360u};
    bool open{false};
    bool visible{true};
    bool swapchainDirty{false};
    bool userRequestedHide{false};

#if defined(_WIN32)
    HINSTANCE windowInstance{nullptr};
    HWND windowHandle{nullptr};
    HWND titleLabels[3]{};
    std::string windowClassName{"DDGIAtlasWindow"};
    static DebugAtlasWindow* activeWindow;
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    bool handleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result);
    bool createWindow();
    void destroyWindow();
    void layoutTitleLabels() const;
#endif

    void createRenderPass();
    void destroyRenderPass();
    void createFramebuffers();
    void destroyFramebuffers();
    void createSyncObjects();
    void destroySyncObjects();
    void createCommandBuffers();
    void destroyCommandBuffers();
    void recreateSwapchain();
    void processMessages();

public:
    /**
     * Create the independent atlas debug window. The window shares the same
     * Vulkan instance/device/queue as the main renderer but owns its own
     * surface, swapchain, render pass, and frame synchronization objects.
     */
    void create(vk::Instance instance,
                vk::PhysicalDevice physicalDevice,
                vkm::VKMDevice* device,
                vk::Queue queue,
                vk::PipelineCache pipelineCache,
#if defined(_WIN32)
                HINSTANCE windowInstance
#else
                void* windowInstance
#endif
    );

    /**
     * Destroy swapchain resources before the OS window and shared texture
     * renderer helper.
     */
    void destroy();

    /**
     * Return whether the user clicked the atlas window close button since the
     * last poll. The close button is translated into the same "hidden but still
     * alive" state as the HUD checkbox so we avoid tearing down swapchain
     * resources from an OS close event.
     */
    bool consumeHideRequest();

    /**
     * Render the three DDGI atlas textures into the independent debug window.
     * The render runs after the main frame submission on the same queue, so
     * queue submission order becomes the synchronization point between atlas
     * writers in the main frame and atlas readers in this debug window.
     */
    void update(const ddgi::DDGIVolume& volume, bool visible);
};

} // namespace debug
