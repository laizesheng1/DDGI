#pragma once
#include "VKM_Tools.h"

namespace vkm {
	class VKMDevice;
	struct Buffer {
		using QueryFunc = std::function<uint32_t(uint32_t, vk::MemoryPropertyFlags, vk::Bool32*)>;
	public:
		vk::Device device = VK_NULL_HANDLE;
		vk::DescriptorBufferInfo descriptor;
		vk::DeviceSize size = 0;
		vk::DeviceSize alignment = 0;
		vk::BufferUsageFlags usageFlags;
		vk::MemoryPropertyFlags memoryPropertyFlags;
		uint32_t memoryTypeIndex;

		vk::Buffer buffer = VK_NULL_HANDLE;
		vk::DeviceMemory memory = VK_NULL_HANDLE;
		uint64_t deviceAddress;

		void* mapped = nullptr;
		void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags property, void* data, VKMDevice* vkmdevice);
		vkm_result map(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
		void unmap();
		void destroy();
		vkm_result flush(vk::DeviceSize size= VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
		void setupDescriptor(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0);
	};
}