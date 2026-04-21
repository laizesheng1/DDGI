#include "VKMDevice.h"

namespace vkm {
	VKMDevice::VKMDevice(vk::PhysicalDevice physicalDevice)
	{
		assert(physicalDevice);
		this->physicalDevice = physicalDevice;
		properties = physicalDevice.getProperties();
		features = physicalDevice.getFeatures();
		memoryProperties = physicalDevice.getMemoryProperties();

		queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

		auto  extensions = physicalDevice.enumerateDeviceExtensionProperties();
		for (auto& ext : extensions)
		{
			supportedExtensions.push_back(ext.extensionName);
		}
	}

	VKMDevice::~VKMDevice()
	{
		if (commandPool)
			logicalDevice.destroyCommandPool(commandPool);
		if (logicalDevice)
			logicalDevice.destroy();
	}

	uint32_t VKMDevice::queryMemTypeIndex(uint32_t type, vk::MemoryPropertyFlags properties, vk::Bool32* memTypeFound) const
	{
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
			if ((1 << i) & type &&
				(memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				if (memTypeFound)
				{
					*memTypeFound = true;
				}
				return i;
			}
		}

		if (memTypeFound)
		{
			*memTypeFound = false;
			return 0;
		}
		else
		{
			throw std::runtime_error("Could not find a matching memory type");
		}
	}

	vkm_result VKMDevice::createLogicalDevice(vk::PhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, void* pNextChain, bool useSwapChain, vk::QueueFlags requestedQueueTypes)
	{
		std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos{};
		const float defaultQueuePriority = 0.0f;
		// Graphics queue
		if (requestedQueueTypes & vk::QueueFlagBits::eGraphics)
		{
			queueFamilyIndices.graphics = getQueueFamilyIndex(vk::QueueFlagBits::eGraphics);
			
			vk::DeviceQueueCreateInfo queueInfo;
			queueInfo.queueFamilyIndex = queueFamilyIndices.graphics;
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &defaultQueuePriority;
			queueCreateInfos.push_back(queueInfo);
		}
		else
		{
			queueFamilyIndices.graphics = 0;
		}
		// compute queue
		if (requestedQueueTypes & vk::QueueFlagBits::eCompute)
		{
			queueFamilyIndices.compute = getQueueFamilyIndex(vk::QueueFlagBits::eCompute);
			if (queueFamilyIndices.compute != queueFamilyIndices.graphics)
			{
				// If compute family index differs, we need an additional queue create info for the compute queue
				vk::DeviceQueueCreateInfo queueInfo;
				queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
		}
		else
		{
			queueFamilyIndices.compute = queueFamilyIndices.graphics;
		}
		//tranfer
		if (requestedQueueTypes & vk::QueueFlagBits::eTransfer)
		{
			queueFamilyIndices.transfer = getQueueFamilyIndex(vk::QueueFlagBits::eTransfer);
			if (queueFamilyIndices.transfer != queueFamilyIndices.graphics)
			{
				// If compute family index differs, we need an additional queue create info for the compute queue
				vk::DeviceQueueCreateInfo queueInfo;
				queueInfo.queueFamilyIndex = queueFamilyIndices.transfer;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
		}
		else
		{
			queueFamilyIndices.transfer = queueFamilyIndices.graphics;
		}
		std::vector<const char*> deviceExtensions(enabledExtensions);
		if (useSwapChain)
		{
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}
		vk::DeviceCreateInfo createInfo;
		createInfo.setQueueCreateInfos(queueCreateInfos)
			.setPEnabledFeatures(&enabledFeatures);

		//TODO:
		vk::PhysicalDeviceFeatures2 physicalDeviceFeatures2{};
		if (pNextChain) {
			physicalDeviceFeatures2.sType = vk::StructureType::ePhysicalDeviceFeatures2;
			physicalDeviceFeatures2.features = enabledFeatures;
			physicalDeviceFeatures2.pNext = pNextChain;
			createInfo.pEnabledFeatures = nullptr;
			createInfo.pNext = &physicalDeviceFeatures2;
		}

		if (deviceExtensions.size() > 0)
		{
			for (const char* enabledExtension : deviceExtensions)
			{
				if (!extensionSupported(enabledExtension)) {
					OutputMessage("Enabled device extension {} is not present at device level", enabledExtension);
				}
			}
			createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
			createInfo.ppEnabledExtensionNames = deviceExtensions.data();
		}

		this->enabledFeatures = enabledFeatures;

		vk::Result result = physicalDevice.createDevice(&createInfo,nullptr,&logicalDevice);
		if (result != vk::Result::eSuccess)
		{
			OutputMessage("[ VKM_DEVICE ] ERROR\nFailed to create logical device!\nError code: {}\n", int32_t(result));
			return result;
		}
		commandPool = createCommandPool(queueFamilyIndices.graphics);

		return result;
	}

	uint32_t VKMDevice::getQueueFamilyIndex(vk::QueueFlags queueFlags) const
	{
		//for compute but not graphics
		if ((queueFlags & vk::QueueFlagBits::eCompute) == queueFlags)
		{
			for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
			{
				if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute) && (!(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics)))
				{
					return i;
				}
			}
		}
		//only for transfer
		if ((queueFlags & vk::QueueFlagBits::eTransfer) == queueFlags)
		{
			for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
			{
				if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eTransfer) && (!(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics)) && (!(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute)))
				{
					return i;
				}
			}
		}
		//use this
		for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
		{
			if ((queueFamilyProperties[i].queueFlags & queueFlags) == queueFlags)
			{
				return i;
			}
		}
		OutputMessage("Could not find a matching queue family index!");
	}

	bool VKMDevice::extensionSupported(std::string extension)
	{
		return (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end());
	}

	vkm_result VKMDevice::createBuffer(vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags memoryPropertyFlags, vk::DeviceSize size, vk::Buffer* buffer, vk::DeviceMemory* memory, void* data)
	{
		vk::BufferCreateInfo createInfo;
		createInfo.setUsage(usageFlags)
			.setSize(size)
			.setSharingMode(vk::SharingMode::eExclusive);
		VK_CHECK_RESULT(logicalDevice.createBuffer(&createInfo, nullptr, buffer));
		vk::MemoryRequirements memReqs;
		logicalDevice.getBufferMemoryRequirements(*buffer, &memReqs);
		vk::MemoryAllocateInfo memAlloc;
		memAlloc.setAllocationSize(memReqs.size)
			.setMemoryTypeIndex(queryMemTypeIndex(memReqs.memoryTypeBits, memoryPropertyFlags));
		vk::MemoryAllocateFlagsInfoKHR allocFlagsInfo;
		if (usageFlags & vk::BufferUsageFlagBits::eShaderDeviceAddress)
		{
			allocFlagsInfo.setFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);
			memAlloc.pNext = &allocFlagsInfo;
		}
		VK_CHECK_RESULT(logicalDevice.allocateMemory(&memAlloc, nullptr, memory));
		if (data != nullptr)
		{
			void* mapped;
			mapped = logicalDevice.mapMemory(*memory, 0, size);
			memcpy(mapped, data, size);
			if (!(memoryPropertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent))
			{
				vk::MappedMemoryRange mappedRange;
				mappedRange.setMemory(*memory)
					.setSize(size);
				VK_CHECK_RESULT(logicalDevice.flushMappedMemoryRanges(1, &mappedRange));
			}
			if(mapped)
				logicalDevice.unmapMemory(*memory);
		}
		logicalDevice.bindBufferMemory(*buffer, *memory, 0);
		return vkm_result();
	}
	//return vkm::Buffer
	vkm_result VKMDevice::createBuffer(vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags memoryPropertyFlags, vkm::Buffer* buffer, vk::DeviceSize size, void* data)
	{
		buffer->createBuffer(size, usageFlags, memoryPropertyFlags, data, this);
		return vkm_result();
	}
	void VKMDevice::copyBuffer(vkm::Buffer* src, vkm::Buffer* dst, vk::Queue queue, vk::BufferCopy* copyRegion)
	{
		assert(dst->size >= src->size);
		assert(src->buffer);
		vk::CommandBuffer copyCmd = createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
		vk::BufferCopy bufferCopy{};
		if (copyRegion == nullptr)
		{
			bufferCopy.size = src->size;
		}
		else
		{
			bufferCopy = *copyRegion;
		}

		copyCmd.copyBuffer(src->buffer, dst->buffer,1, &bufferCopy);

		flushCommandBuffer(copyCmd, queue);
	}

	vk::CommandPool VKMDevice::createCommandPool(uint32_t queueFamilyIndex, vk::CommandPoolCreateFlags createFlags)
	{
		vk::CommandPoolCreateInfo createInfo;
		createInfo.setQueueFamilyIndex(queueFamilyIndex)
			.setFlags(createFlags);

		vk::CommandPool cmdPool;
		auto result = logicalDevice.createCommandPool(&createInfo, nullptr, &cmdPool);
		if (result != vk::Result::eSuccess)
		{
			OutputMessage("[ VKM_DEVICE ] ERROR\nFailed to create cmdPool!\nError code: {}\n", int32_t(result));
		}
		return cmdPool;
	}

	vk::CommandBuffer VKMDevice::createCommandBuffer(vk::CommandBufferLevel level, vk::CommandPool pool, bool begin)
	{
		vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
		cmdBufAllocateInfo.setCommandBufferCount(1)
			.setLevel(level)
			.setCommandPool(pool);
		vk::CommandBuffer cmdBuffer;
		
		VK_CHECK_RESULT(logicalDevice.allocateCommandBuffers(&cmdBufAllocateInfo, &cmdBuffer));
		// If requested, also start recording for the new command buffer
		if (begin)
		{
			vk::CommandBufferBeginInfo beginInfo;
			VK_CHECK_RESULT(cmdBuffer.begin(&beginInfo));
		}
		return cmdBuffer;
	}

	vk::CommandBuffer VKMDevice::createCommandBuffer(vk::CommandBufferLevel level, bool begin)
	{
		return createCommandBuffer(level, commandPool, begin);
	}
	void VKMDevice::flushCommandBuffer(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::CommandPool pool, bool free)
	{
		if (commandBuffer == VK_NULL_HANDLE)
		{
			return;
		}
		commandBuffer.end();

		vk::SubmitInfo submitInfo;
		submitInfo.setCommandBufferCount(1)
			.setCommandBuffers(commandBuffer);

		// Create fence to ensure that the command buffer has finished executing
		vk::FenceCreateInfo fenceInfo;
		vk::Fence fence;
		VK_CHECK_RESULT(logicalDevice.createFence(&fenceInfo, nullptr, &fence));

		// Submit to the queue
		VK_CHECK_RESULT(queue.submit(1, &submitInfo, fence));

		// Wait for the fence to signal that command buffer has finished executing
		VK_CHECK_RESULT(logicalDevice.waitForFences(1, &fence, true, DEFAULT_FENCE_TIMEOUT));
		
		logicalDevice.destroyFence(fence);
		if (free)
		{
			logicalDevice.freeCommandBuffers(pool, 1, &commandBuffer);
		}
	}
	void VKMDevice::flushCommandBuffer(vk::CommandBuffer commandBuffer, vk::Queue queue, bool free)
	{
		return flushCommandBuffer(commandBuffer, queue, commandPool, free);
	}
	vk::Format VKMDevice::getSupportedDepthFormat(bool checkSamplingSupport)
	{
		std::vector<vk::Format> depthFormats = { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint, vk::Format::eD16UnormS8Uint, vk::Format::eD16Unorm };
		for (auto& format : depthFormats)
		{
			vk::FormatProperties formatProperties;
			physicalDevice.getFormatProperties(format, &formatProperties);
			// Format must support depth stencil attachment for optimal tiling
			if (formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
			{
				if (checkSamplingSupport) {
					if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage)) {
						continue;
					}
				}
				return format;
			}
		}
		throw std::runtime_error("Could not find a matching depth format");
	}
	void VKMDevice::AllocBindImageMem(vk::MemoryPropertyFlags property, vk::Image& image, vk::DeviceMemory& memory)
	{
		vk::MemoryRequirements memReqs = logicalDevice.getImageMemoryRequirements(image);
		vk::MemoryAllocateInfo memAllocInfo;
		memAllocInfo.setAllocationSize(memReqs.size);
		memAllocInfo.setMemoryTypeIndex(queryMemTypeIndex(memReqs.memoryTypeBits, property));
		VK_CHECK_RESULT(logicalDevice.allocateMemory(&memAllocInfo, nullptr, &memory));
		logicalDevice.bindImageMemory(image, memory, 0);
	}
}
