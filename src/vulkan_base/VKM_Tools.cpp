#include "VKM_Tools.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vkm {
    namespace tools
    {
		vk::Bool32 getSupportedDepthFormat(vk::PhysicalDevice physicalDevice, vk::Format* depthFormat)
		{
			std::vector<vk::Format> formatList = {
				vk::Format::eD32SfloatS8Uint,
				vk::Format::eD32Sfloat,
				vk::Format::eD24UnormS8Uint,
				vk::Format::eD16UnormS8Uint,
				vk::Format::eD16Unorm
			};

			for (auto& format : formatList)
			{
				vk::FormatProperties formatProps;
				physicalDevice.getFormatProperties(format, &formatProps);
				if (formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
				{
					*depthFormat = format;
					return true;
				}
			}

			return false;
		}
		vk::Bool32 getSupportedDepthStencilFormat(vk::PhysicalDevice physicalDevice, vk::Format* depthStencilFormat)
		{
			std::vector<vk::Format> formatList = {
				vk::Format::eD32SfloatS8Uint,
				vk::Format::eD24UnormS8Uint,
				vk::Format::eD16UnormS8Uint
			};

			for (auto& format : formatList)
			{
				vk::FormatProperties formatProps;
				physicalDevice.getFormatProperties(format, &formatProps);
				if (formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
				{
					*depthStencilFormat = format;
					return true;
				}
			}

			return false;
		}
		vk::ShaderModule loadShader(const char* filename, vk::Device device)
		{
			std::ifstream is(filename, std::ios::binary | std::ios::in | std::ios::ate);

			if (is.is_open())
			{
				size_t size = is.tellg();
				is.seekg(0, std::ios::beg);
				char* shaderCode = new char[size];
				is.read(shaderCode, size);
				is.close();

				assert(size > 0);

				vk::ShaderModule shaderModule;
				vk::ShaderModuleCreateInfo moduleCreateInfo;
				moduleCreateInfo.codeSize = size;
				moduleCreateInfo.pCode = (uint32_t*)shaderCode;

				VK_CHECK_RESULT(device.createShaderModule(&moduleCreateInfo, NULL, &shaderModule));

				delete[] shaderCode;

				return shaderModule;
			}
			else
			{
				std::cerr << "Error: Could not open shader file \"" << filename << "\"" << "\n";
				return VK_NULL_HANDLE;
			}
		}
		void setImageLayout(vk::CommandBuffer cmdbuffer, vk::Image image, vk::ImageLayout oldImageLayout, vk::ImageLayout newImageLayout, vk::ImageSubresourceRange subresourceRange, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask)
		{
			vk::ImageMemoryBarrier imageMemoryBarrier;
			imageMemoryBarrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setOldLayout(oldImageLayout)
				.setNewLayout(newImageLayout)
				.setImage(image)
				.setSubresourceRange(subresourceRange);

			switch (oldImageLayout)
			{
			case vk::ImageLayout::eUndefined:
				imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
				break;
			case vk::ImageLayout::ePreinitialized:
				imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
				break;
			case vk::ImageLayout::eColorAttachmentOptimal:
				imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
				break;
			case vk::ImageLayout::eDepthStencilAttachmentOptimal:
				imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				break;
			case vk::ImageLayout::eTransferSrcOptimal:
				imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
				break;
			case vk::ImageLayout::eTransferDstOptimal:
				imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
				break;
			case vk::ImageLayout::eShaderReadOnlyOptimal:
				imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
				break;
			default:
				break;
			}
			switch (newImageLayout)
			{
			case vk::ImageLayout::eTransferDstOptimal:
				imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
				break;
			case vk::ImageLayout::eTransferSrcOptimal:
				imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
				break;
			case vk::ImageLayout::eColorAttachmentOptimal:
				imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
				break;
			case vk::ImageLayout::eDepthStencilAttachmentOptimal:
				imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				break;
			case vk::ImageLayout::eShaderReadOnlyOptimal:
				if (imageMemoryBarrier.srcAccessMask == vk::AccessFlagBits::eNone)
				{
					imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite | vk::AccessFlagBits::eTransferWrite;
				}
				imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
				break;
			default:
				break;
			}
			cmdbuffer.pipelineBarrier(
				srcStageMask,
				dstStageMask,
				{}, {}, nullptr,
				imageMemoryBarrier);
		}
		void setImageLayout(vk::CommandBuffer cmdbuffer, vk::Image image, vk::ImageAspectFlags aspectMask, vk::ImageLayout oldImageLayout, vk::ImageLayout newImageLayout, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask)
		{
			vk::ImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = aspectMask;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;
			setImageLayout(cmdbuffer, image, oldImageLayout, newImageLayout, subresourceRange, srcStageMask, dstStageMask);
		}
		vk::BufferImageCopy initBufferImageCopyInfo(vk::Extent3D imageExtent, uint32_t mipLevel, vk::DeviceSize bufferOffset)
		{
			vk::BufferImageCopy bufferCopyRegion;
			vk::ImageSubresourceLayers Layers;
			Layers.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setMipLevel(mipLevel)
				.setBaseArrayLayer(0)
				.setLayerCount(1);

			bufferCopyRegion.setBufferOffset(bufferOffset)
				.setImageSubresource(Layers)
				.setImageExtent(imageExtent);
			return bufferCopyRegion;
		}
		vk::Bool32 formatHasStencil(vk::Format format)
		{
			std::vector<vk::Format> stencilFormats = {
				vk::Format::eS8Uint,
				vk::Format::eD16UnormS8Uint,
				vk::Format::eD24UnormS8Uint,
				vk::Format::eD32SfloatS8Uint,
			};
			return std::find(stencilFormats.begin(), stencilFormats.end(), format) != stencilFormats.end();
		}
		bool fileExist(const std::string& filename)
		{
			std::ifstream f(filename.c_str());
			return !f.fail();
		}
		void exitFatal(const std::string& message, int32_t exitCode)
		{
#if defined(_WIN32)
			MessageBox(NULL, message.c_str(), NULL, MB_OK | MB_ICONERROR);
			exit(exitCode);
#endif
		}
		void exitFatal(const std::string& message, vk::Result resultCode)
		{
			exitFatal(message, (int32_t)resultCode);
		}
		const std::string getAssetPath()
		{
		#if defined VKM_ASSETS_DIR
			return VKM_ASSETS_DIR;
		#else 
			return "./../assets/";
		#endif
		}
		const std::string getShaderPath()
		{
		#if defined VKM_SHADERS_DIR
			return VKM_SHADERS_DIR;
		#else 
			return "./../shaders/";
		#endif
		}
    }

	namespace debug {

		vk::DebugUtilsMessengerEXT debugUtilsMessenger;

		VKAPI_ATTR vk::Bool32 VKAPI_CALL debugUtilsMessageCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageType, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
		{	
			// Select prefix depending on flags passed to the callback
			std::string prefix;

			if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
				prefix = "VERBOSE: ";
			}
			else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
				prefix = "INFO: ";
			}
			else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
				prefix = "WARNING: ";
			}
			else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
				prefix = "ERROR: ";
			}

			// Display message to default output (console/logcat)
			std::stringstream debugMessage;
			if (pCallbackData->pMessageIdName) {
				debugMessage << prefix << "[" << pCallbackData->messageIdNumber << "][" << pCallbackData->pMessageIdName << "] : " << pCallbackData->pMessage;
			}
			else {
				debugMessage << prefix << "[" << pCallbackData->messageIdNumber << "] : " << pCallbackData->pMessage;
			}

			if (messageSeverity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
				std::cerr << debugMessage.str() << "\n\n";
			}
			else {
				std::cout << debugMessage.str() << "\n\n";
			}
			fflush(stdout);

			// The return value of this callback controls whether the Vulkan call that caused the validation message will be aborted or not
			// We return VK_FALSE as we DON'T want Vulkan calls that cause a validation message to abort
			// If you instead want to have calls abort, pass in VK_TRUE and the function will return VK_ERROR_VALIDATION_FAILED_EXT
			return VK_FALSE;
		}

		void setupDebugingMessengerCreateInfo(vk::DebugUtilsMessengerCreateInfoEXT& debugUtilsMessengerCI)
		{
			debugUtilsMessengerCI.setMessageSeverity(
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
			);
			debugUtilsMessengerCI.setMessageType(
				vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
				vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
			);
			debugUtilsMessengerCI.setPfnUserCallback(debugUtilsMessageCallback);
		}

		void setupDebugging(vk::Instance instance)
		{
			vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI{};
			setupDebugingMessengerCreateInfo(debugUtilsMessengerCI);

			vk::Result result = instance.createDebugUtilsMessengerEXT(&debugUtilsMessengerCI, nullptr, &debugUtilsMessenger, VULKAN_HPP_DEFAULT_DISPATCHER);

			if (result != vk::Result::eSuccess) {
				std::cerr << "Failed to setup debug messenger!" << std::endl;
			}
		}

		void freeDebugCallback(vk::Instance instance)
		{
			if (debugUtilsMessenger) {
				instance.destroyDebugUtilsMessengerEXT(debugUtilsMessenger, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
			}
		}

		void Init(vk::Instance instance)
		{
			auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
			VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
			VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
		}
	}

	namespace  initializers {
		vk::WriteDescriptorSet writeDescriptorSet(vk::DescriptorSet dstSet, vk::DescriptorType type, uint32_t binding, vk::DescriptorBufferInfo* bufferInfo, uint32_t descriptorCount)
		{
			vk::WriteDescriptorSet writeDescriptorSet;
			writeDescriptorSet.setDstSet(dstSet)
				.setDescriptorType(type)
				.setDstBinding(binding)
				.setPBufferInfo(bufferInfo)
				.setDescriptorCount(descriptorCount);
			return writeDescriptorSet;
		}
		vk::WriteDescriptorSet writeDescriptorSet(vk::DescriptorSet dstSet, vk::DescriptorType type, uint32_t binding, vk::DescriptorImageInfo* imageInfo, uint32_t descriptorCount)
		{
			vk::WriteDescriptorSet writeDescriptorSet;
			writeDescriptorSet.setDstSet(dstSet)
				.setDescriptorType(type)
				.setDstBinding(binding)
				.setPImageInfo(imageInfo)
				.setDescriptorCount(descriptorCount);
			return writeDescriptorSet;
		}
		void createImageSubresourceRange(vk::ImageSubresourceRange& subresourceRange, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount)
		{
			subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(baseMipLevel)
				.setLevelCount(levelCount)
				.setBaseArrayLayer(baseArrayLayer)
				.setLayerCount(layerCount);
		}
	}
}
