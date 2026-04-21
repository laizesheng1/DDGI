#pragma once
#include "VKM_Tools.h"

class SwainChain {
private:
	vk::Instance instance{ VK_NULL_HANDLE };
	vk::Device device{ VK_NULL_HANDLE };
	vk::PhysicalDevice physicalDevice{ VK_NULL_HANDLE };
	vk::SurfaceKHR surface{ VK_NULL_HANDLE };
protected:
	std::vector<vk::SurfaceFormatKHR> availableSurfaceFormats;
	std::vector<void(*)()> createSwapchain_callbacks;
	std::vector<void(*)()> destroySwapchain_callbacks;

	void ExecuteCallbacks(std::vector<void(*)()>& callbacks);
public:
	vk::Format colorFormat{};
	vk::ColorSpaceKHR colorSpace{};
	vk::SwapchainCreateInfoKHR swapchainCreateInfo = {};
	vk::SwapchainKHR swapChain{ VK_NULL_HANDLE };
	std::vector<vk::Image> images{};
	std::vector<vk::ImageView> imageViews{};
	uint32_t queueNodeIndex = UINT32_MAX;
	uint32_t imageCount = 0;

	void initSurface(void* platformHandle, void* platformWindow);
	void AddCreateSwapchain_callbacks(void(*function)());
	void AddDestroySwapchain_callbacks(void(*function)());
	vkm_result GetSurfaceFormats();
	vkm_result SetSurfaceFormat(vk::SurfaceFormatKHR surfaceFormat);
	vkm_result SetSurfaceFormat();		//vkexamples
	vkm_result CreateSwapchain(uint32_t& width, uint32_t& height, bool limitFrameRate = true);
	vkm_result CreateSwapchain_Resources();
	void setContext(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device);
	vkm_result acquireNextImage(vk::Semaphore presentCompleteSemaphore, uint32_t& imageIndex);
	void cleanup();
	//Gretter
	void Surface(vk::SurfaceKHR& surface);
};