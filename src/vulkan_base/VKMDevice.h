#pragma once
#include "VKM_Tools.h"
#include "Buffer.h"
#include "vulkan/vulkan.h"

namespace vkm {
	class VKMDevice {
	private:
		uint32_t		  getQueueFamilyIndex(vk::QueueFlags queueFlags) const;
		bool              extensionSupported(std::string extension);
		vk::CommandPool   createCommandPool(uint32_t queueFamilyIndex, vk::CommandPoolCreateFlags createFlags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
		vk::CommandBuffer createCommandBuffer(vk::CommandBufferLevel level, vk::CommandPool pool, bool begin = false);
		void              flushCommandBuffer(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::CommandPool pool, bool free = true);

	protected:

	public:
		vk::PhysicalDevice physicalDevice{ VK_NULL_HANDLE };
		vk::Device logicalDevice{ VK_NULL_HANDLE };
		vk::PhysicalDeviceProperties properties{};
		vk::PhysicalDeviceMemoryProperties memoryProperties{};
		vk::PhysicalDeviceFeatures features{};
		vk::PhysicalDeviceFeatures enabledFeatures{};

		std::vector<vk::QueueFamilyProperties> queueFamilyProperties{};
		std::vector<std::string> supportedExtensions{};
		vk::CommandPool commandPool{ VK_NULL_HANDLE };
		//
		struct
		{
			uint32_t graphics;
			uint32_t compute;
			uint32_t transfer;
		} queueFamilyIndices;

		operator vk::Device() const
		{
			return logicalDevice;
		};

		explicit VKMDevice(vk::PhysicalDevice physicalDevice);
		~VKMDevice();
		uint32_t          queryMemTypeIndex(uint32_t type, vk::MemoryPropertyFlags properties, vk::Bool32* memTypeFound = nullptr) const;
		vkm_result        createLogicalDevice(vk::PhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, void* pNextChain, bool useSwapChain = true, vk::QueueFlags requestedQueueTypes = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute);
		vkm_result        createBuffer(vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags memoryPropertyFlags, vk::DeviceSize size, vk::Buffer* buffer,  vk::DeviceMemory* memory,void* data = nullptr);
		vkm_result        createBuffer(vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags memoryPropertyFlags, vkm::Buffer* buffer, vk::DeviceSize size, void* data = nullptr);
		void              copyBuffer(vkm::Buffer* src, vkm::Buffer* dst, vk::Queue queue, vk::BufferCopy* copyRegion = nullptr);

		vk::CommandBuffer createCommandBuffer(vk::CommandBufferLevel level, bool begin = false);
		void              flushCommandBuffer(vk::CommandBuffer commandBuffer, vk::Queue queue, bool free = true);
		vk::Format        getSupportedDepthFormat(bool checkSamplingSupport);
		void			  AllocBindImageMem(vk::MemoryPropertyFlags property, vk::Image& image, vk::DeviceMemory& memory);
	};
}