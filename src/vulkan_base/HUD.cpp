#include "HUD.h"

namespace vkm {
	HUD::HUD()
	{
		ImGui::CreateContext();
		// Color scheme
		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.0f, 0.0f, 0.0f, 0.1f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.8f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
		style.Colors[ImGuiCol_Button] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
		// Dimensions
		ImGuiIO& io = ImGui::GetIO();
		io.FontGlobalScale = scale;
	}
	HUD::~HUD()
	{
		if (ImGui::GetCurrentContext()) {
			ImGui::DestroyContext();
		}
	}

	void HUD::prepareResources()
	{
		assert(maxFrames > 0);
		ImGuiIO& io = ImGui::GetIO();

		// Create font texture
		unsigned char* fontData;
		int texWidth, texHeight;
		if (io.Fonts->Fonts.empty()) {
			io.Fonts->AddFontDefault();
		}
		io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
		vk::DeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);

		// Set ImGui style scale factor to handle retina and other HiDPI displays (same as font scaling above)
		ImGuiStyle& style = ImGui::GetStyle();
		style.ScaleAllSizes(scale);

		// Create target image for copy
		vk::ImageCreateInfo imageInfo;
		imageInfo.setImageType(vk::ImageType::e2D)
			.setFormat(vk::Format::eR8G8B8A8Unorm)
			.setExtent({ (uint32_t)texWidth , (uint32_t)texHeight ,1 })
			.setMipLevels(1)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);

		VK_CHECK_RESULT(device->logicalDevice.createImage(&imageInfo, nullptr, &fontImage));
		vk::MemoryRequirements memReqs;
		device->logicalDevice.getImageMemoryRequirements(fontImage, &memReqs);
		vk::MemoryAllocateInfo memAllocInfo;
		memAllocInfo.setAllocationSize(memReqs.size)
			.setMemoryTypeIndex(device->queryMemTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));

		VK_CHECK_RESULT(device->logicalDevice.allocateMemory(&memAllocInfo, nullptr, &fontMemory));
		device->logicalDevice.bindImageMemory(fontImage, fontMemory, 0);

		// Image view
		vk::ImageViewCreateInfo viewInfo;
		vk::ImageSubresourceRange ranges;
		ranges.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setLevelCount(1)
			.setLayerCount(1);
		viewInfo.setImage(fontImage)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(vk::Format::eR8G8B8A8Unorm)
			.setSubresourceRange(ranges);
		VK_CHECK_RESULT(device->logicalDevice.createImageView(&viewInfo, nullptr, &fontView));

		// Staging buffers for font data upload
		vkm::Buffer stagingBuffer;
		VK_CHECK_RESULT(device->createBuffer(vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, &stagingBuffer, uploadSize));
		stagingBuffer.map();
		memcpy(stagingBuffer.mapped, fontData, uploadSize);

		// Copy buffer data to font image
		vk::CommandBuffer copyCmd = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
		// Prepare for transfer
		vkm::tools::setImageLayout(
			copyCmd,
			fontImage,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits::eHost,
			vk::PipelineStageFlagBits::eTransfer);
		// Copy
		vk::BufferImageCopy bufferCopyRegion;
		vk::ImageSubresourceLayers layers;
		layers.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setLayerCount(1);
		bufferCopyRegion.setImageSubresource(layers)
			.setImageExtent({ (uint32_t)texWidth , (uint32_t)texHeight ,1 });

		copyCmd.copyBufferToImage(stagingBuffer.buffer, fontImage, vk::ImageLayout::eTransferDstOptimal, 1, &bufferCopyRegion);
		// Prepare for shader read
		vkm::tools::setImageLayout(
			copyCmd,
			fontImage,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader);
		device->flushCommandBuffer(copyCmd, queue, true);

		stagingBuffer.destroy();

		// Font texture Sampler
		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.setMagFilter(vk::Filter::eLinear)
			.setMinFilter(vk::Filter::eLinear)
			.setMipmapMode(vk::SamplerMipmapMode::eLinear)
			.setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
			.setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
			.setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
			.setMaxAnisotropy(1.0f)
			.setBorderColor(vk::BorderColor::eFloatOpaqueWhite);

		VK_CHECK_RESULT(device->logicalDevice.createSampler(&samplerInfo, nullptr, &sampler));

		// Descriptor pool
		vk::DescriptorPoolSize poolSize;
		poolSize.setType(vk::DescriptorType::eCombinedImageSampler)
			.setDescriptorCount(1);

		vk::DescriptorPoolCreateInfo descriptorPoolInfo;
		descriptorPoolInfo.setMaxSets(2)
			.setPoolSizes(poolSize);
		VK_CHECK_RESULT(device->logicalDevice.createDescriptorPool(&descriptorPoolInfo, nullptr, &descriptorPool));

		// Descriptor set layout
		vk::DescriptorSetLayoutBinding setLayoutBinding;
		setLayoutBinding.setBinding(0)
			.setDescriptorCount(1)
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment);
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.setBindings(setLayoutBinding);
		VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(&descriptorLayout, nullptr, &descriptorSetLayout));

		// Descriptor set
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.setDescriptorPool(descriptorPool)
			.setSetLayouts(descriptorSetLayout);

		VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&allocInfo, &descriptorSet));
		vk::DescriptorImageInfo fontDescriptor;
		fontDescriptor.setSampler(sampler)
			.setImageView(fontView)
			.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

		vk::WriteDescriptorSet writeDescriptorSets;
		writeDescriptorSets.setDstSet(descriptorSet)
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setDstBinding(0)
			.setImageInfo(fontDescriptor)
			.setDescriptorCount(1);
		device->logicalDevice.updateDescriptorSets(1, &writeDescriptorSets, 0, nullptr);

		// Buffers per max. frames-in-flight
		buffers.resize(maxFrames);
	}

	void HUD::createPipeline(const vk::PipelineCache pipelineCache, const vk::RenderPass renderPass, const vk::Format colorFormat, const vk::Format depthFormat)
	{
		// Pipeline layout
		// Push constants for UI rendering parameters
		vk::PushConstantRange pushConstantRange;
		pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eVertex)
			.setSize(sizeof(PushConstBlock));
		vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
		pipelineLayoutCreateInfo.setSetLayoutCount(1)
			.setSetLayouts(descriptorSetLayout)
			.setPushConstantRangeCount(1)
			.setPushConstantRanges(pushConstantRange);
		VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(&pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		//vertext input
		std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
			{ 0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex } // : binding, stride, inputRate
		};
		std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {		//location, binding, format, offset
			{0,  0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos) },
			{1,  0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv) },
			{2,  0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col) },
		};
		vk::PipelineVertexInputStateCreateInfo vertexInputState;
		vertexInputState.setVertexAttributeDescriptions(vertexInputAttributes)
			.setVertexBindingDescriptions(vertexInputBindings);
		//assemble
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
		inputAssemblyState.setTopology(vk::PrimitiveTopology::eTriangleList)
			.setPrimitiveRestartEnable(VK_FALSE);
		//viewport
		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.setScissorCount(1)
			.setViewportCount(1);
		//rasterization
		vk::PipelineRasterizationStateCreateInfo rasterizationState;
		rasterizationState.setPolygonMode(vk::PolygonMode::eFill)
			.setCullMode(vk::CullModeFlagBits::eNone)
			.setFrontFace(vk::FrontFace::eCounterClockwise)
			.setDepthClampEnable(VK_FALSE)
			.setLineWidth(1.f);
		//multisample
		vk::PipelineMultisampleStateCreateInfo multisampleState;
		multisampleState.setRasterizationSamples(rasterizationSamples);
		//depth test
		vk::PipelineDepthStencilStateCreateInfo depthStencilState;
		depthStencilState.setDepthTestEnable(VK_FALSE)
			.setDepthWriteEnable(VK_FALSE)
			.setDepthCompareOp(vk::CompareOp::eAlways);
		//blend
		vk::PipelineColorBlendAttachmentState blendAttachmentState;
		blendAttachmentState.setBlendEnable(VK_TRUE)
			.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
			.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
			.setDstAlphaBlendFactor(vk::BlendFactor::eZero)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		//color blend
		vk::PipelineColorBlendStateCreateInfo colorBlendState;
		colorBlendState.setAttachments(blendAttachmentState);
		//dynamic
		std::vector<vk::DynamicState> dynamicStateEnables = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState;
		dynamicState.setDynamicStates(dynamicStateEnables);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
		pipelineCreateInfo.setStageCount(static_cast<uint32_t>(shaders.size()))
			.setPStages(shaders.data())
			.setPVertexInputState(&vertexInputState)
			.setPInputAssemblyState(&inputAssemblyState)
			.setPViewportState(&viewportState)
			.setPRasterizationState(&rasterizationState)
			.setPMultisampleState(&multisampleState)
			.setPDepthStencilState(&depthStencilState)
			.setPColorBlendState(&colorBlendState)
			.setPDynamicState(&dynamicState)
			.setLayout(pipelineLayout)
			.setRenderPass(renderPass)
			.setSubpass(subpass);
#if defined(VK_KHR_dynamic_rendering)
		// If we are using dynamic rendering (renderPass is null), we must define color, depth and stencil attachments at pipeline create time
		vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
		if (renderPass == VK_NULL_HANDLE) {
			pipelineRenderingCreateInfo.setColorAttachmentFormats(colorFormat)
				.setDepthAttachmentFormat(depthFormat)
				.setStencilAttachmentFormat(depthFormat);
			pipelineCreateInfo.pNext = &pipelineRenderingCreateInfo;
		}
#endif
		VK_CHECK_RESULT(device->logicalDevice.createGraphicsPipelines(pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
	}

	void HUD::update(uint32_t currentBuffer)
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();
		// Note: Alignment is done inside buffer creation
		vk::DeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
		vk::DeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

		// Update buffers only if vertex or index count has been changed compared to current buffer size
		if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
			return;
		}

		// Create buffers with multiple of a chunk size to minimize the need to recreate them
		const vk::DeviceSize chunkSize = 16384;
		vertexBufferSize = ((vertexBufferSize + chunkSize - 1) / chunkSize) * chunkSize;
		indexBufferSize = ((indexBufferSize + chunkSize - 1) / chunkSize) * chunkSize;

		// Recreate vertex buffer only if necessary
		if ((buffers[currentBuffer].vertexBuffer.buffer == VK_NULL_HANDLE) || (buffers[currentBuffer].vertexBuffer.size < vertexBufferSize)) {
			buffers[currentBuffer].vertexBuffer.unmap();
			buffers[currentBuffer].vertexBuffer.destroy();
			VK_CHECK_RESULT(device->createBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible, &buffers[currentBuffer].vertexBuffer, vertexBufferSize));
			buffers[currentBuffer].vertexCount = imDrawData->TotalVtxCount;
			buffers[currentBuffer].vertexBuffer.map();
		}

		// Recreate index buffer only if necessary
		if ((buffers[currentBuffer].indexBuffer.buffer == VK_NULL_HANDLE) || (buffers[currentBuffer].indexBuffer.size < indexBufferSize)) {
			buffers[currentBuffer].indexBuffer.unmap();
			buffers[currentBuffer].indexBuffer.destroy();
			VK_CHECK_RESULT(device->createBuffer(vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eHostVisible, &buffers[currentBuffer].indexBuffer, indexBufferSize));
			buffers[currentBuffer].indexCount = imDrawData->TotalIdxCount;
			buffers[currentBuffer].indexBuffer.map();
		}

		// Upload data
		ImDrawVert* vtxDst = (ImDrawVert*)buffers[currentBuffer].vertexBuffer.mapped;
		ImDrawIdx* idxDst = (ImDrawIdx*)buffers[currentBuffer].indexBuffer.mapped;

		for (int n = 0; n < imDrawData->CmdListsCount; n++) {
			const ImDrawList* cmd_list = imDrawData->CmdLists[n];
			memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtxDst += cmd_list->VtxBuffer.Size;
			idxDst += cmd_list->IdxBuffer.Size;
		}

		// Flush to make writes visible to GPU
		buffers[currentBuffer].vertexBuffer.flush();
		buffers[currentBuffer].indexBuffer.flush();
	}

	void HUD::draw(const vk::CommandBuffer commandBuffer, uint32_t currentBuffer)
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();
		int32_t vertexOffset = 0;
		int32_t indexOffset = 0;

		if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
			return;
		}

		if (buffers[currentBuffer].vertexBuffer.buffer == VK_NULL_HANDLE || buffers[currentBuffer].indexBuffer.buffer == VK_NULL_HANDLE) {
			return;
		}

		ImGuiIO& io = ImGui::GetIO();

		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

		pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
		pushConstBlock.translate = glm::vec2(-1.0f);
		commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstBlock), &pushConstBlock);

		assert(buffers[currentBuffer].vertexBuffer.buffer != VK_NULL_HANDLE && buffers[currentBuffer].indexBuffer.buffer != VK_NULL_HANDLE);

		vk::DeviceSize offsets[1] = { 0 };
		commandBuffer.bindVertexBuffers(0, 1, &buffers[currentBuffer].vertexBuffer.buffer, offsets);
		commandBuffer.bindIndexBuffer(buffers[currentBuffer].indexBuffer.buffer, 0, vk::IndexType::eUint16);

		for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
			const ImDrawList* cmd_list = imDrawData->CmdLists[i];
			for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
				vk::Rect2D scissorRect{
					{std::max((int32_t)(pcmd->ClipRect.x), 0),std::max((int32_t)(pcmd->ClipRect.y), 0) },			//offset
					{(uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x),(uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y) }		//extent
				};
				commandBuffer.setScissor( 0, 1, &scissorRect);
				commandBuffer.drawIndexed(pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
				indexOffset += pcmd->ElemCount;
			}
			vertexOffset += cmd_list->VtxBuffer.Size;
		}
	}

	void HUD::resize(uint32_t width, uint32_t height)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)(width), (float)(height));
	}

	void HUD::freeResources()
	{
		for (auto& buffer : buffers) {
			buffer.vertexBuffer.destroy();
			buffer.indexBuffer.destroy();
		}
		device->logicalDevice.destroyImageView(fontView, nullptr);
		device->logicalDevice.destroyImage(fontImage, nullptr);
		device->logicalDevice.freeMemory(fontMemory, nullptr);
		device->logicalDevice.destroySampler(sampler, nullptr);
		device->logicalDevice.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);
		device->logicalDevice.destroyDescriptorPool(descriptorPool, nullptr);
		device->logicalDevice.destroyPipelineLayout(pipelineLayout, nullptr);
		device->logicalDevice.destroyPipeline(pipeline, nullptr);
	}

	bool HUD::header(const char* caption)
	{
		return ImGui::CollapsingHeader(caption, ImGuiTreeNodeFlags_DefaultOpen);
	}

	bool HUD::checkBox(const char* caption, bool* value)
	{
		return ImGui::Checkbox(caption, value);
	}

	bool HUD::checkBox(const char* caption, int32_t* value)
	{
		bool val = (*value == 1);
		bool res = ImGui::Checkbox(caption, &val);
		*value = val;
		return res;
	}

	bool HUD::radioButton(const char* caption, bool value)
	{
		return ImGui::RadioButton(caption, value);
	}

	bool HUD::inputFloat(const char* caption, float* value, float step, uint32_t precision)
	{
		return ImGui::InputFloat(caption, value, step, step * 10.0f, precision);
	}

	bool HUD::sliderFloat(const char* caption, float* value, float min, float max)
	{
		return ImGui::SliderFloat(caption, value, min, max);
	}

	bool HUD::sliderInt(const char* caption, int32_t* value, int32_t min, int32_t max)
	{
		return ImGui::SliderInt(caption, value, min, max);
	}

	bool HUD::comboBox(const char* caption, int32_t* itemindex, std::vector<std::string> items)
	{
		if (items.empty()) {
			return false;
		}
		std::vector<const char*> charitems;
		charitems.reserve(items.size());
		for (size_t i = 0; i < items.size(); i++) {
			charitems.push_back(items[i].c_str());
		}
		uint32_t itemCount = static_cast<uint32_t>(charitems.size());
		return ImGui::Combo(caption, itemindex, &charitems[0], itemCount, itemCount);
	}

	bool HUD::button(const char* caption)
	{
		return ImGui::Button(caption);
	}

	bool HUD::colorPicker(const char* caption, float* color) {
		return ImGui::ColorEdit4(caption, color, ImGuiColorEditFlags_NoInputs);
	}

	void HUD::text(const char* formatstr, ...)
	{
		va_list args;
		va_start(args, formatstr);
		ImGui::TextV(formatstr, args);
		va_end(args);
	}
}