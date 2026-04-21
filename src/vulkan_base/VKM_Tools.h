#pragma once
#ifndef VKM_TOOLS_H
#define VKM_TOOLS_H
#include <math.h>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <fstream>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <stack>
#include <map>
#include <unordered_map>
#include <span>
#include <memory>
#include <functional>
#include <concepts>
#include <format>
#include <chrono>
#include <numeric>
#include <numbers>
#include <print>
#include <string.h>
#include <array>
#include <ctime>
#include <random>
#include <sys/stat.h>
//glm
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
//#define CONSOLE 1
//#pragma comment(linker, "/subsystem:console")		//use console 
#pragma comment(linker, "/subsystem:windows")		//use windows
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1		/*use dynamic dispatch*/
#include <vulkan/vulkan.hpp>

#define VK_RESULT_THROW
#define DEFAULT_FENCE_TIMEOUT 100000000000

#ifdef VK_RESULT_THROW
class vkm_result {
	vk::Result result;
public:
	static void (*callback_func)(vk::Result);
	vkm_result() :result(vk::Result::eSuccess) {}
	vkm_result(vk::Result result) : result(result) {}
	vkm_result(vkm_result&& other) noexcept :result(other.result) {}
	~vkm_result() noexcept(false) {
		if (uint32_t(result) < VK_RESULT_MAX_ENUM)
			return;
		if (callback_func)
			callback_func(result);
		throw result;
	}
	operator vk::Result()
	{
		vk::Result result = this->result;
		this->result = vk::Result::eSuccess;
		return result;
	}
};
inline void (*vkm_result::callback_func)(vk::Result);
#else
using vkm_result = vk::Result;
#endif // VK_RESULT_THROW

#define ExecuteOnce(...) { static bool executed = false; if (executed) return __VA_ARGS__; executed = true; }

template<typename... Ts>
void OutputMessage(const std::format_string<Ts...> format, Ts&&... arguments) {
	std::print(format, std::forward<Ts>(arguments)...);
}

#define VK_CHECK_RESULT(f)																				\
{																										\
	vk::Result res = (f);																					\
	if (res != vk::Result::eSuccess)																				\
	{																									\
		OutputMessage("Fatal : Result is false. Error code: {} in {} at line {}\n", int32_t(res), __FILE__, __LINE__); \
		/*assert(res == vk::Result::eSuccess);*/																		\
	}																									\
}


namespace vkm {
	namespace tools {
		vk::Bool32 getSupportedDepthFormat(vk::PhysicalDevice physicalDevice, vk::Format* depthFormat);
		vk::Bool32 getSupportedDepthStencilFormat(vk::PhysicalDevice physicalDevice, vk::Format* depthStencilFormat);
		vk::ShaderModule loadShader(const char* filename, vk::Device device);
		// Put an image memory barrier for setting an image layout on the sub resource into the given command buffer
		void setImageLayout(
			vk::CommandBuffer cmdbuffer,
			vk::Image image,
			vk::ImageLayout oldImageLayout,
			vk::ImageLayout newImageLayout,
			vk::ImageSubresourceRange subresourceRange,
			vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eAllCommands,
			vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eAllCommands);
		// Uses a fixed sub resource layout with first mip level and layer
		void setImageLayout(
			vk::CommandBuffer cmdbuffer,
			vk::Image image,
			vk::ImageAspectFlags aspectMask,
			vk::ImageLayout oldImageLayout,
			vk::ImageLayout newImageLayout,
			vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eAllCommands,
			vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eAllCommands);
		vk::BufferImageCopy initBufferImageCopyInfo(vk::Extent3D imageExtent, uint32_t mipLevel = 0, vk::DeviceSize bufferOffset = 0);
		vk::Bool32 formatHasStencil(vk::Format format);

		bool fileExist(const std::string& filename);
		void exitFatal(const std::string& message, int32_t exitCode);
		void exitFatal(const std::string& message, vk::Result resultCode);
		const std::string getAssetPath();
		const std::string getShaderPath();
	}

    namespace debug {
		static vk::detail::DynamicLoader dl;

		VKAPI_ATTR vk::Bool32 VKAPI_CALL debugUtilsMessageCallback(
			vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			vk::DebugUtilsMessageTypeFlagsEXT messageType,
			const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData);

		void setupDebugingMessengerCreateInfo(vk::DebugUtilsMessengerCreateInfoEXT& debugUtilsMessengerCI);
        
		void setupDebugging(vk::Instance instance);

		void freeDebugCallback(vk::Instance instance);

		void Init(vk::Instance instance);
    }

	namespace initializers {
		vk::WriteDescriptorSet writeDescriptorSet(
			vk::DescriptorSet dstSet,
			vk::DescriptorType type,
			uint32_t binding,
			vk::DescriptorBufferInfo* bufferInfo,
			uint32_t descriptorCount = 1
		);
		vk::WriteDescriptorSet writeDescriptorSet(
			vk::DescriptorSet dstSet,
			vk::DescriptorType type,
			uint32_t binding,
			vk::DescriptorImageInfo* imageInfo,
			uint32_t descriptorCount = 1
		);
		void createImageSubresourceRange(vk::ImageSubresourceRange& subresourceRange,
			uint32_t baseMipLevel,
			uint32_t levelCount,
			uint32_t baseArrayLayer,
			uint32_t layerCount
		);
	}
}

#endif
