#include "Swapchain.h"

void SwainChain::initSurface(void* platformHandle, void* platformWindow)
{
	//create surface
	if(surface==VK_NULL_HANDLE)
	{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
		vk::Result err = vk::Result::eSuccess;
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo{};
		surfaceCreateInfo.hinstance = (HINSTANCE)platformHandle;
		surfaceCreateInfo.hwnd = (HWND)platformWindow;
		err = instance.createWin32SurfaceKHR(&surfaceCreateInfo, nullptr, &surface);
		if (err != vk::Result::eSuccess) {
			OutputMessage("Could not create surface!", uint32_t(err));
		}
#endif
	}
	assert(surface);

	//try to find one that supports both graphics and present
	std::vector<vk::QueueFamilyProperties> queueProps = physicalDevice.getQueueFamilyProperties();
	uint32_t queueCount = queueProps.size();
	std::vector<VkBool32> supportsPresent(queueCount);
	for (uint32_t i = 0; i < queueCount; i++)
	{
		supportsPresent[i] = physicalDevice.getSurfaceSupportKHR(i, surface);
	}
	uint32_t graphicsQueueNodeIndex = UINT32_MAX;
	uint32_t presentQueueNodeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < queueCount; i++) {
		if (queueProps[i].queueFlags & vk::QueueFlagBits::eGraphics) {
			if (graphicsQueueNodeIndex == UINT32_MAX) {
				graphicsQueueNodeIndex = i;
			}
			if (supportsPresent[i] == VK_TRUE) {
				graphicsQueueNodeIndex = i;
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
	//use separate present queue
	if (presentQueueNodeIndex == UINT32_MAX)
	{
		for (uint32_t i = 0; i < queueCount; ++i) {
			if (supportsPresent[i] == VK_TRUE) {
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
	if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) {
		OutputMessage("Could not find a graphics and/or presenting queue!");
	}
	if (graphicsQueueNodeIndex != presentQueueNodeIndex) {
		OutputMessage("Separate graphics and presenting queues are not supported yet!");
	}
	queueNodeIndex = graphicsQueueNodeIndex;
	
	vk::Result result = GetSurfaceFormats();
	VK_CHECK_RESULT(SetSurfaceFormat());
}

vkm_result SwainChain::GetSurfaceFormats()
{
	availableSurfaceFormats = physicalDevice.getSurfaceFormatsKHR(surface);
	if (availableSurfaceFormats.size()==0)
	{
		std::cout << "[ Swapchain ] ERROR\nFailed to get surface formats!\n";
	}
	return  vk::Result::eSuccess;
}

vkm_result SwainChain::SetSurfaceFormat(vk::SurfaceFormatKHR surfaceFormat)
{
	bool formatIsAvailable = false;
	if (surfaceFormat.format==vk::Format::eUndefined) {
		for (auto& format : availableSurfaceFormats)
		{
			if (format.colorSpace == surfaceFormat.colorSpace) {
				colorFormat = format.format;
				colorSpace = format.colorSpace;
				formatIsAvailable = true;
				break;
			}
		}
	}
	else
	{
		for (auto& format : availableSurfaceFormats)
		{
			if (format.format == surfaceFormat.format &&
				format.colorSpace == surfaceFormat.colorSpace) {
				colorFormat = format.format;
				colorSpace = format.colorSpace;
				formatIsAvailable = true;
				break;
			}
		}
	}
	if (!formatIsAvailable)
		return vk::Result::eErrorFormatNotSupported;

	return vk::Result::eSuccess;

}

vkm_result SwainChain::SetSurfaceFormat()
{
	bool formatIsAvailable = false;
	vk::SurfaceFormatKHR selectedFormat = availableSurfaceFormats[0];
	std::vector<vk::Format> preferredImageFormats = {
		vk::Format::eB8G8R8A8Unorm,
		vk::Format::eR8G8B8A8Unorm,
		vk::Format::eA8B8G8R8UnormPack32
	};
	for (auto& availableFormat : availableSurfaceFormats) {
		if (std::find(preferredImageFormats.begin(), preferredImageFormats.end(), availableFormat.format) != preferredImageFormats.end()) {
			selectedFormat = availableFormat;
			formatIsAvailable = true;
			break;
		}
	}
	if(formatIsAvailable)
	{
		colorFormat = selectedFormat.format;
		colorSpace = selectedFormat.colorSpace;
		return vk::Result::eSuccess;
	}
	//if (swapChain)
	//	recreateSwapchain();
	return vk::Result::eErrorFormatNotSupported;
}

vkm_result SwainChain::CreateSwapchain(uint32_t& width, uint32_t& height, bool limitFrameRate)
{
	assert(device);
	assert(physicalDevice);
	assert(instance);

	vk::SwapchainKHR oldSwapchain = swapChain;
	//Get surface capabilities
	vk::SurfaceCapabilitiesKHR surfaceCapabilities = {};
	vk::Result result = physicalDevice.getSurfaceCapabilitiesKHR(surface, &surfaceCapabilities);
	if (result != vk::Result::eSuccess) {
		OutputMessage("[ Swapchain ] ERROR\nFailed to get physical device surface capabilities!\nError code: {}\n", int32_t(result));
		return result;
	}
	//Set image count
	uint32_t desiredNumberOfSwapchainImages = surfaceCapabilities.minImageCount + (surfaceCapabilities.maxImageCount > surfaceCapabilities.minImageCount);
	//Set image extent
	vk::Extent2D swapchainExtent =
		surfaceCapabilities.currentExtent.width == UINT32_MAX ?
		vk::Extent2D{ glm::clamp(width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width),
					  glm::clamp(height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height) } :
		surfaceCapabilities.currentExtent;

	//Set alpha compositing mode
	
	vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	// Simply select the first composite alpha format available
	std::vector<vk::CompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
		vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
		vk::CompositeAlphaFlagBitsKHR::eInherit
	};
	for (auto& compositeAlphaFlag : compositeAlphaFlags) {
		if (surfaceCapabilities.supportedCompositeAlpha & compositeAlphaFlag) {
			compositeAlpha = compositeAlphaFlag;
			break;
		};
	}

	//Get surface formats
	if (!availableSurfaceFormats.size())
		if (vk::Result result = GetSurfaceFormats(); result == vk::Result::eSuccess)
			return result;

	//Get surface present modes
	std::vector<vk::PresentModeKHR> surfacePresentModes= physicalDevice.getSurfacePresentModesKHR(surface);
	uint32_t presentModeCount= surfacePresentModes.size();
	if (presentModeCount == 0) {
		OutputMessage("[ Swapchain ] ERROR\nFailed to get surface present modes!\n");
	}

	//Set image usage
	swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
	if (surfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
		swapchainCreateInfo.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
	if (surfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst)
		swapchainCreateInfo.imageUsage |= vk::ImageUsageFlagBits::eTransferDst;
	else
		OutputMessage("[ Swapchain ] WARNING\nVK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported!\n");

	//Set present mode to mailbox if available and necessary
	swapchainCreateInfo.presentMode = vk::PresentModeKHR::eFifo;
	if (!limitFrameRate)
	{
		for (size_t i = 0; i < presentModeCount; i++)
		{
			if (surfacePresentModes[i] == vk::PresentModeKHR::eMailbox) {
				swapchainCreateInfo.presentMode = vk::PresentModeKHR::eMailbox;
				break;
			}
			if (surfacePresentModes[i] == vk::PresentModeKHR::eImmediate)
			{
				swapchainCreateInfo.presentMode = vk::PresentModeKHR::eImmediate;
			}
		}
	}

	//Create swapchain
	swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
	swapchainCreateInfo.minImageCount = desiredNumberOfSwapchainImages;
	swapchainCreateInfo.imageFormat = colorFormat;
	swapchainCreateInfo.imageColorSpace = colorSpace;
	swapchainCreateInfo.imageExtent = swapchainExtent;
	swapchainCreateInfo.surface = surface;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageSharingMode =vk::SharingMode::eExclusive;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.compositeAlpha = compositeAlpha;
	swapchainCreateInfo.queueFamilyIndexCount = 0;
	swapchainCreateInfo.oldSwapchain = oldSwapchain;

	//Create new swapchain
	VK_CHECK_RESULT(device.createSwapchainKHR(&swapchainCreateInfo, nullptr, &swapChain));

	if (oldSwapchain != nullptr)
	{
		for (auto i = 0; i < images.size(); i++) {
			device.destroyImageView(imageViews[i]);
		}
		device.destroySwapchainKHR(oldSwapchain);
	}

	CreateSwapchain_Resources();
	
	//Create related objects
	ExecuteCallbacks(createSwapchain_callbacks);

	return vk::Result::eSuccess;
}

vkm_result SwainChain::CreateSwapchain_Resources()
{
	//Get swapchain images
	images = device.getSwapchainImagesKHR(swapChain);
	imageCount = images.size();
	//Create new swapchain image views
	imageViews.resize(imageCount);
	for (auto i = 0; i < imageCount; i++)
	{
		vk::ImageViewCreateInfo createInfo;
		vk::ComponentMapping mapping;
		vk::ImageSubresourceRange range;
		range.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseArrayLayer(0)
			.setBaseMipLevel(0)
			.setLayerCount(1)
			.setLevelCount(1);
		createInfo.setImage(images[i])
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(colorFormat)
			.setComponents(mapping)
			.setSubresourceRange(range);
		imageViews[i] = device.createImageView(createInfo);
	}
	return vk::Result::eSuccess;
}

void SwainChain::setContext(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device)
{
	this->instance = instance;
	this->physicalDevice = physicalDevice;
	this->device = device;
}

vkm_result SwainChain::acquireNextImage(vk::Semaphore presentCompleteSemaphore, uint32_t& imageIndex)
{
	return device.acquireNextImageKHR(swapChain, UINT64_MAX, presentCompleteSemaphore, (vk::Fence)nullptr, &imageIndex);
}

void SwainChain::cleanup()
{
	if (swapChain != VK_NULL_HANDLE) {

		ExecuteCallbacks(destroySwapchain_callbacks);

		for (auto i = 0; i < images.size(); i++) {
			device.destroyImageView(imageViews[i]);
		}
		device.destroySwapchainKHR(swapChain);
	}
	if (surface != VK_NULL_HANDLE) {
		instance.destroySurfaceKHR(surface);
	}
	surface = VK_NULL_HANDLE;
	swapChain = VK_NULL_HANDLE;
}

void SwainChain::Surface(vk::SurfaceKHR& surface)
{
	if (!this->surface)
		this->surface = surface;
}

void SwainChain::AddCreateSwapchain_callbacks(void(*function)())
{
	createSwapchain_callbacks.push_back(function);
}

void SwainChain::AddDestroySwapchain_callbacks(void(*function)())
{
	destroySwapchain_callbacks.push_back(function);
}

void SwainChain::ExecuteCallbacks(std::vector<void(*)()>& callbacks)
{
	for (size_t size = callbacks.size(), i = 0; i < size; i++)
		callbacks[i]();
}
