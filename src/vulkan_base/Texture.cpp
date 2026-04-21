#include "Texture.h"
#include "VK_Base.h"

namespace vkm {
	void Texture::updateDescriptor()
	{
		descriptorImageInfo.sampler = sampler;
		descriptorImageInfo.imageView = imageView;
		descriptorImageInfo.imageLayout = imageLayout;
	}

	void Texture::destroy()
	{
		if (device == nullptr || device->logicalDevice == VK_NULL_HANDLE)
		{
			return;
		}
		if (imageView)
		{
			device->logicalDevice.destroyImageView(imageView);
			imageView = VK_NULL_HANDLE;
		}
		if (image)
		{
			device->logicalDevice.destroyImage(image);
			image = VK_NULL_HANDLE;
		}
		if (sampler)
		{
			device->logicalDevice.destroySampler(sampler);
			sampler = VK_NULL_HANDLE;
		}
		if (deviceMemory)
		{
			device->logicalDevice.freeMemory(deviceMemory);
			deviceMemory = VK_NULL_HANDLE;
		}
	}

	ktxResult Texture::loadKTXFile(std::string filename, ktxTexture** target)
	{
		ktxResult result = KTX_SUCCESS;
		if (!vkm::tools::fileExist(filename))
		{
			vkm::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
		}
		result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, target);
		return result;
	}

	void Texture::initImageCreateInfo(vk::ImageCreateInfo& imageCreateInfo, vk::Format format, const vk::ImageUsageFlags& imageUsageFlags, uint32_t arrayLevels)
	{
		imageCreateInfo.setImageType(vk::ImageType::e2D)
			.setFormat(format)
			.setExtent({ width,height,1 })
			.setMipLevels(mipLevels)
			.setArrayLayers(arrayLevels)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(imageUsageFlags)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);
	}

	void Texture::allocImageDeviceMem(vk::MemoryPropertyFlagBits property)
	{
		vk::MemoryRequirements memReqs = device->logicalDevice.getImageMemoryRequirements(image);
		vk::MemoryAllocateInfo memAllocInfo;
		memAllocInfo.setAllocationSize(memReqs.size);
		memAllocInfo.setMemoryTypeIndex(device->queryMemTypeIndex(memReqs.memoryTypeBits, property));
		VK_CHECK_RESULT(device->logicalDevice.allocateMemory(&memAllocInfo, nullptr, &deviceMemory));
		device->logicalDevice.bindImageMemory(image, deviceMemory, 0);
	}

	void Texture::CreateImageview(vk::ImageSubresourceRange& subresourceRange, vk::Format format, vk::ImageViewType viewType)
	{
		vk::ImageViewCreateInfo viewCreateInfo;
		viewCreateInfo.setImage(image)
			.setViewType(viewType)
			.setFormat(format)
			.setSubresourceRange(subresourceRange);
		VK_CHECK_RESULT(device->logicalDevice.createImageView(&viewCreateInfo, nullptr, &imageView));
	}

	void Texture::CreateDefaultSampler(vk::SamplerAddressMode samplerMipmapMode)
	{
		vk::SamplerCreateInfo samplerCreateInfo;
		samplerCreateInfo.setMagFilter(vk::Filter::eLinear)
			.setMinFilter(vk::Filter::eLinear)
			.setMipmapMode(vk::SamplerMipmapMode::eLinear)
			.setAddressModeU(samplerMipmapMode)
			.setAddressModeV(samplerMipmapMode)
			.setAddressModeW(samplerMipmapMode)
			.setMipLodBias(0.f)
			.setAnisotropyEnable(device->enabledFeatures.samplerAnisotropy)
			.setMaxAnisotropy(device->enabledFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f)
			.setCompareOp(vk::CompareOp::eNever)
			.setMinLod(0.f)
			.setMaxLod((float)mipLevels)
			.setBorderColor(vk::BorderColor::eFloatOpaqueWhite);
		VK_CHECK_RESULT(device->logicalDevice.createSampler(&samplerCreateInfo, nullptr, &sampler));
	}

	//Load a 2D texture including all mip levels
	void Texture2D::loadFromFile(std::string filename, vk::Format format, vk::Queue copyQueue, vk::ImageUsageFlags imageUsageFlags, vk::ImageLayout imageLayout, bool updateDes)
	{
		ktxTexture* ktxTexture;
		ktxResult result = loadKTXFile(filename, &ktxTexture);
		assert(result == KTX_SUCCESS);

		width = ktxTexture->baseWidth;
		height = ktxTexture->baseHeight;
		mipLevels = ktxTexture->numLevels;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

		vk::FormatProperties formatProperties = device->physicalDevice.getFormatProperties(format);
		vk::CommandBuffer copyCmd = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
		
		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;
		//create and bind buffer | copy data
		device->createBuffer(vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			ktxTextureSize,
			&stagingBuffer,
			&stagingMemory,
			ktxTextureData
		);
		// Setup buffer copy regions for each mip level
		std::vector<vk::BufferImageCopy> bufferCopyRegions;
		for (uint32_t i = 0; i < mipLevels; i++) {
			ktx_size_t offset;
			KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
			assert(result == KTX_SUCCESS);

			vk::BufferImageCopy bufferCopyRegion = vkm::tools::initBufferImageCopyInfo(
				{ std::max(1u, ktxTexture->baseWidth >> i),	std::max(1u, ktxTexture->baseHeight >> i), 1 },
				i, offset);
			bufferCopyRegions.push_back(bufferCopyRegion);
		}

		// Create optimal tiled target image
		vk::ImageCreateInfo imageCreateInfo;
		initImageCreateInfo(imageCreateInfo, format, imageUsageFlags);
		if (!(imageCreateInfo.usage & vk::ImageUsageFlagBits::eTransferDst)) {
			imageCreateInfo.usage |= vk::ImageUsageFlagBits::eTransferDst;
		}
		VK_CHECK_RESULT(device->logicalDevice.createImage(&imageCreateInfo, nullptr, &image));

		allocImageDeviceMem();

		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseMipLevel(0)
			.setLayerCount(1)
			.setLevelCount(mipLevels);
		// Image barrier for optimal image (target)
		// Optimal image will be used as destination for the copy
		vkm::tools::setImageLayout(
			copyCmd,
			image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			subresourceRange
		);
		// Copy mip levels from staging buffer
		copyCmd.copyBufferToImage(
			stagingBuffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			bufferCopyRegions
		);
		// Change texture image layout to shader read after all mip levels have been copied
		this->imageLayout = imageLayout;
		vkm::tools::setImageLayout(
			copyCmd,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			imageLayout,
			subresourceRange
		);
		device->flushCommandBuffer(copyCmd, copyQueue);
		//clean up
		device->logicalDevice.destroyBuffer(stagingBuffer, nullptr);
		device->logicalDevice.freeMemory(stagingMemory, nullptr);
		ktxTexture_Destroy(ktxTexture);

		if(updateDes)
		{
			// Create a default sampler
			CreateDefaultSampler();
			// Create image view
			vkm::initializers::createImageSubresourceRange(subresourceRange, 0, mipLevels, 0, 1);
			CreateImageview(subresourceRange, format);

			updateDescriptor();
		}
	}

	//Creates a 2D texture from a buffer
	void Texture2D::fromBuffer(void* buffer, vk::DeviceSize bufferSize, vk::Format format, uint32_t texWidth, uint32_t texHeight, vk::Queue copyQueue, vk::Filter filter, vk::ImageUsageFlags imageUsageFlags, vk::ImageLayout imageLayout)
	{
		assert(buffer);
		this->device = device;
		width = texWidth;
		height = texHeight;
		mipLevels = 1;

		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;
		//create stagingBuffer and bind stagingMemory | copy buffer data
		device->createBuffer(vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			bufferSize,
			&stagingBuffer,
			&stagingMemory,
			buffer
		);
		
		vk::BufferImageCopy bufferCopyRegion = vkm::tools::initBufferImageCopyInfo({ width,height,1 });
		vk::ImageCreateInfo imageCreateInfo;
		initImageCreateInfo(imageCreateInfo, format, imageUsageFlags);
		if (!(imageCreateInfo.usage & vk::ImageUsageFlagBits::eTransferDst)) {
			imageCreateInfo.usage |= vk::ImageUsageFlagBits::eTransferDst;
		}

		VK_CHECK_RESULT(device->logicalDevice.createImage(&imageCreateInfo, nullptr, &image));

		allocImageDeviceMem();

		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseMipLevel(0)
			.setLayerCount(1)
			.setLevelCount(mipLevels);

		vk::CommandBuffer copyCmd = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
		vkm::tools::setImageLayout(
			copyCmd,
			image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			subresourceRange
		);
		// Copy mip levels from staging buffer
		copyCmd.copyBufferToImage(
			stagingBuffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			bufferCopyRegion
		);
		// Change texture image layout to shader read after all mip levels have been copied
		this->imageLayout = imageLayout;
		vkm::tools::setImageLayout(
			copyCmd,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			imageLayout,
			subresourceRange
		);
		device->flushCommandBuffer(copyCmd, copyQueue);

		device->logicalDevice.destroyBuffer(stagingBuffer, nullptr);
		device->logicalDevice.freeMemory(stagingMemory, nullptr);

		vk::SamplerCreateInfo samplerCreateInfo;
		samplerCreateInfo.setMagFilter(filter)
			.setMinFilter(filter)
			.setMipmapMode(vk::SamplerMipmapMode::eLinear)
			.setAddressModeU(vk::SamplerAddressMode::eRepeat)
			.setAddressModeV(vk::SamplerAddressMode::eRepeat)
			.setAddressModeW(vk::SamplerAddressMode::eRepeat)
			.setMipLodBias(0.f)
			.setMaxAnisotropy(1.0f)
			.setCompareOp(vk::CompareOp::eNever)
			.setMinLod(0.f)
			.setMaxLod(0.f);
		VK_CHECK_RESULT(device->logicalDevice.createSampler(&samplerCreateInfo, nullptr, &sampler));
		// Create image view
		vk::ImageViewCreateInfo viewCreateInfo;
		subresourceRange.setBaseArrayLayer(0);
		viewCreateInfo.setImage(image)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(format)
			.setSubresourceRange(subresourceRange);
		VK_CHECK_RESULT(device->logicalDevice.createImageView(&viewCreateInfo, nullptr, &imageView));

		updateDescriptor();
	}

	void Texture2D::fromglTFimage(tinygltf::Image& gltfimage, std::string path, vk::Queue copyQueue)
	{
		assert(device);
		bool isKtx = false;
		if (gltfimage.uri.find_last_of(".") != std::string::npos) {
			if (gltfimage.uri.substr(gltfimage.uri.find_last_of(".") + 1) == "ktx") {
				isKtx = true;
			}
		}
		vk::Format format;

		if (!isKtx)
		{
			// Texture was loaded using STB_Image
			unsigned char* buffer = nullptr;
			vk::DeviceSize bufferSize = 0;
			bool deleteBuffer = false;
			if (gltfimage.component == 3) {
				// Most devices don't support RGB only on Vulkan so convert if necessary
				// TODO: Check actual format support and transform only if required
				bufferSize = gltfimage.width * gltfimage.height * 4;
				buffer = new unsigned char[bufferSize];
				unsigned char* rgba = buffer;
				unsigned char* rgb = &gltfimage.image[0];
				for (size_t i = 0; i < gltfimage.width * gltfimage.height; ++i) {
					for (int32_t j = 0; j < 3; ++j) {
						rgba[j] = rgb[j];
					}
					rgba += 4;
					rgb += 3;
				}
				deleteBuffer = true;
			}
			else {
				buffer = &gltfimage.image[0];
				bufferSize = gltfimage.image.size();
			}
			assert(buffer);

			format = vk::Format::eR8G8B8A8Unorm;
			width = gltfimage.width;
			height = gltfimage.height;
			mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

			vk::FormatProperties formatProperties = device->physicalDevice.getFormatProperties(format);
			assert(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc);
			assert(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);

			vk::Buffer stagingBuffer;
			vk::DeviceMemory stagingMemory;
			//create stagingBuffer and bind stagingMemory | copy buffer data
			device->createBuffer(vk::BufferUsageFlagBits::eTransferSrc,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
				bufferSize,
				&stagingBuffer,
				&stagingMemory,
				buffer
			);

			vk::ImageCreateInfo imageCreateInfo;
			initImageCreateInfo(imageCreateInfo, format, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled);
			allocImageDeviceMem();
			//
			vk::CommandBuffer copyCmd = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setLayerCount(1)
				.setLevelCount(1);

			vkm::tools::setImageLayout(
				copyCmd,
				image,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eTransferDstOptimal,
				subresourceRange,
				vk::PipelineStageFlagBits::eTopOfPipe,
				vk::PipelineStageFlagBits::eTransfer
			);

			vk::BufferImageCopy bufferCopyRegion = vkm::tools::initBufferImageCopyInfo({ width,height,1 });
			copyCmd.copyBufferToImage(
				stagingBuffer,
				image,
				vk::ImageLayout::eTransferDstOptimal,
				bufferCopyRegion
			);
			vkm::tools::setImageLayout(
				copyCmd,
				image,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eTransferSrcOptimal,
				subresourceRange,
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eTransfer
			);
			device->flushCommandBuffer(copyCmd, copyQueue, true);

			device->logicalDevice.destroyBuffer( stagingBuffer, nullptr);
			device->logicalDevice.freeMemory(stagingMemory, nullptr);

			//Generate the mip chain
			vk::CommandBuffer blitCmd = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
			for (uint32_t i = 0; i < mipLevels; i++)
			{
				vk::ImageBlit imageBlit;
				vk::ImageSubresourceLayers SrcLayers;
				SrcLayers.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setMipLevel(i - 1)
					.setLayerCount(1);
				vk::ImageSubresourceLayers DstLayers;
				DstLayers.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setMipLevel(i)
					.setLayerCount(1);
				std::array<vk::Offset3D, 2> srcOffset;
				srcOffset[0] = vk::Offset3D(0, 0, 0);
				srcOffset[1] = vk::Offset3D(int32_t(width >> (i - 1)), int32_t(height >> (i - 1)), 1);
				std::array<vk::Offset3D, 2> dstOffset;
				dstOffset[0] = vk::Offset3D(0, 0, 0);
				dstOffset[1] = vk::Offset3D(int32_t(width >> i), int32_t(height >> i), 1);
				imageBlit.setDstOffsets(dstOffset)
					.setSrcOffsets(srcOffset)
					.setDstSubresource(DstLayers)
					.setSrcSubresource(SrcLayers);
				vk::ImageSubresourceRange mipSubRange;
				mipSubRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(i)
					.setLevelCount(1)
					.setLayerCount(1);
				vkm::tools::setImageLayout(
					blitCmd,
					image,
					vk::ImageLayout::eUndefined,
					vk::ImageLayout::eTransferDstOptimal,
					mipSubRange,
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eTransfer
				);

				blitCmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal,
					imageBlit, vk::Filter::eLinear);
				{
					vk::ImageMemoryBarrier barrier;
					barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)	
						.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
						.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)			
						.setDstAccessMask(vk::AccessFlagBits::eTransferRead)
						.setImage(image)
						.setSubresourceRange(mipSubRange);
					blitCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
						{}, {}, nullptr, barrier);
				}
			}

			subresourceRange.setLevelCount(mipLevels);
			imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			{
				vk::ImageMemoryBarrier barrier;
				barrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
					.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
					.setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
					.setDstAccessMask(vk::AccessFlagBits::eShaderRead)
					.setImage(image)
					.setSubresourceRange(subresourceRange);
				blitCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
					{}, {}, nullptr, barrier);
			}
			if (deleteBuffer)
			{
				delete[] buffer;
			}

			device->flushCommandBuffer(blitCmd, copyQueue, true);
		}
		else
		{
			// Texture is stored in an external ktx file
			std::string filename = path + "/" + gltfimage.uri;
			ktxTexture* ktxTexture;
			//ktxResult result = loadKTXFile(filename, &ktxTexture);
			format = vk::Format(ktxTexture_GetVkFormat(ktxTexture));
			loadFromFile(filename, format, copyQueue, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
				vk::ImageLayout::eShaderReadOnlyOptimal);
		}
		vk::ImageSubresourceRange range;
		range.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setLevelCount(mipLevels)
			.setLayerCount(1);
		CreateDefaultSampler();
		CreateImageview(range, format);
		updateDescriptor();
	}

	void TextureCubeMap::loadFromFile(std::string filename, vk::Format format, vk::Queue copyQueue, vk::ImageUsageFlags imageUsageFlags, vk::ImageLayout imageLayout)
	{
		ktxTexture* ktxTexture;
		ktxResult result = loadKTXFile(filename, &ktxTexture);
		assert(result == KTX_SUCCESS);

		width = ktxTexture->baseWidth;
		height = ktxTexture->baseHeight;
		mipLevels = ktxTexture->numLevels;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;
		//create and bind buffer | copy data
		device->createBuffer(vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			ktxTextureSize,
			&stagingBuffer,
			&stagingMemory,
			ktxTextureData
		);
		// Setup buffer copy regions for each mip level
		std::vector<vk::BufferImageCopy> bufferCopyRegions;
		for (uint32_t face = 0; face < 6; face++)
		{
			for (uint32_t i = 0; i < mipLevels; i++) {
				ktx_size_t offset;
				// get offset
				KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, face, &offset);
				assert(result == KTX_SUCCESS);

				vk::Extent3D extent;
				extent.setWidth(ktxTexture->baseWidth >> i)
					.setHeight(ktxTexture->baseHeight >> i)
					.setDepth(1);
				vk::ImageSubresourceLayers Layers;
				Layers.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setMipLevel(i)
					.setBaseArrayLayer(face)
					.setLayerCount(1);
				vk::BufferImageCopy bufferCopyRegion;
				bufferCopyRegion.setBufferOffset(offset)
					.setImageExtent(extent)
					.setImageSubresource(Layers);
				bufferCopyRegions.push_back(bufferCopyRegion);
			}
		}

		// Create optimal tiled target image
		vk::ImageCreateInfo imageCreateInfo;
		initImageCreateInfo(imageCreateInfo, format, imageUsageFlags, 6);
		imageCreateInfo.setFlags(vk::ImageCreateFlagBits::eCubeCompatible);		/*diff*/
		if (!(imageCreateInfo.usage & vk::ImageUsageFlagBits::eTransferDst)) {
			imageCreateInfo.usage |= vk::ImageUsageFlagBits::eTransferDst;
		}
		VK_CHECK_RESULT(device->logicalDevice.createImage(&imageCreateInfo, nullptr, &image));

		allocImageDeviceMem();

		vk::CommandBuffer copyCmd = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseMipLevel(0)
			.setLayerCount(6)				/*diff*/
			.setLevelCount(mipLevels);
		// Image barrier for optimal image (target)
		// Optimal image will be used as destination for the copy
		vkm::tools::setImageLayout(
			copyCmd,
			image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			subresourceRange
		);
		// Copy mip levels from staging buffer
		copyCmd.copyBufferToImage(
			stagingBuffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			bufferCopyRegions
		);
		// Change texture image layout to shader read after all mip levels have been copied
		this->imageLayout = imageLayout;
		vkm::tools::setImageLayout(
			copyCmd,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			imageLayout,
			subresourceRange
		);
		device->flushCommandBuffer(copyCmd, copyQueue);
		//clean up
		device->logicalDevice.destroyBuffer(stagingBuffer, nullptr);
		device->logicalDevice.freeMemory(stagingMemory, nullptr);
		ktxTexture_Destroy(ktxTexture);

		// Create a default sampler
		CreateDefaultSampler(vk::SamplerAddressMode::eClampToEdge);
		// Create image view
		vkm::initializers::createImageSubresourceRange(subresourceRange, 0, mipLevels, 0, 6);
		CreateImageview(subresourceRange, format, vk::ImageViewType::eCube);

		updateDescriptor();
	}
}
