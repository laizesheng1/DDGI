#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <sstream>
#include <iomanip>

#include <vulkan/vulkan.h>
#include "VKM_Tools.h"
#include "Buffer.h"
#include "VKMDevice.h"

#include "imgui.h"

namespace vkm {
	class HUD {
	private:
		uint32_t subpass = 0;
		uint32_t currentBuffer = 0;

		struct PushConstBlock {
			glm::vec2 scale;
			glm::vec2 translate;
		} pushConstBlock;

		void text(const char* formatstr, ...);

	protected:
		vk::SampleCountFlagBits rasterizationSamples = vk::SampleCountFlagBits::e1;

		
		struct Buffers {
			vkm::Buffer vertexBuffer;
			vkm::Buffer indexBuffer;
			int32_t vertexCount = 0;
			int32_t indexCount = 0;
		};
		std::vector<Buffers> buffers;

		vk::DescriptorPool descriptorPool{ VK_NULL_HANDLE };
		vk::DescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
		vk::DescriptorSet descriptorSet{ VK_NULL_HANDLE };
		vk::PipelineLayout pipelineLayout{ VK_NULL_HANDLE };
		vk::Pipeline pipeline{ VK_NULL_HANDLE };

		vk::DeviceMemory fontMemory{ VK_NULL_HANDLE };
		vk::Image fontImage{ VK_NULL_HANDLE };
		vk::ImageView fontView{ VK_NULL_HANDLE };
		vk::Sampler sampler{ VK_NULL_HANDLE };

	public:
		uint32_t maxFrames = 0;
		vkm::VKMDevice* device = nullptr;
		vk::Queue queue;
		std::vector<vk::PipelineShaderStageCreateInfo> shaders;
		bool visible = true;
		float scale = 1.0f;

		HUD();
		~HUD();
		void prepareResources();
		void createPipeline(const vk::PipelineCache pipelineCache, const vk::RenderPass renderPass, const vk::Format colorFormat, const vk::Format depthFormat);
		void update(uint32_t currentBuffer);
		void draw(const vk::CommandBuffer commandBuffer, uint32_t currentBuffer);
		void resize(uint32_t width, uint32_t height);
		void freeResources();

		bool header(const char* caption);
		bool checkBox(const char* caption, bool* value);
		bool checkBox(const char* caption, int32_t* value);
		bool radioButton(const char* caption, bool value);
		bool inputFloat(const char* caption, float* value, float step, uint32_t precision);
		bool sliderFloat(const char* caption, float* value, float min, float max);
		bool sliderInt(const char* caption, int32_t* value, int32_t min, int32_t max);
		bool comboBox(const char* caption, int32_t* itemindex, std::vector<std::string> items);
		bool button(const char* caption);
		bool colorPicker(const char* caption, float* color);
	};
}
