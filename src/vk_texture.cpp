#include "vk_texture.h"
#include <iostream>
#include <vk_initializers.h>
#include <texture_asset.h>
#include <Tracy.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

bool vkutil::LoadImageFromFile(VulkanEngine& engine, const char* file, AllocatedImage& outImage)
{
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
	{
		std::cout << "Failed to load texture file :" << file << std::endl;
		return false;
	}

	void* pPixel = pixels;
	VkDeviceSize imageSize = texWidth * texHeight * texChannels;

	VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

	AllocatedBufferUntyped stagingBuffer = engine.CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = engine.MapBuffer(stagingBuffer);
	memcpy(data, pPixel, static_cast<size_t>(imageSize));
	engine.UnmapBuffer(stagingBuffer);

	stbi_image_free(pPixel);

	std::vector<MipmapInfo> mips;
	mips.push_back(MipmapInfo{ imageSize, 0 });
	outImage = UploadImage(texWidth, texHeight, format, engine, stagingBuffer, mips);

	engine.DestroyBuffer(stagingBuffer);

	LOG_INFO("Texture loaded successfully");

	return true;
}

bool vkutil::LoadImageFromAsset(VulkanEngine& engine, const char* filename, AllocatedImage& outImage)
{
	assets::AssetFile file;
	bool loaded = assets::LoadBinaryFile(filename, file);
	if (!loaded)
	{
		LOG_ERROR("Erroe when loading texture {}", filename);
		return false;
	}

	assets::TextureInfo textureInfo = assets::ReadTextureInfo(&file);

	VkDeviceSize imageSize = textureInfo.textureSize;
	VkFormat format;
	switch (textureInfo.textureFormat)
	{
	case assets::TextureFormat::RGBA8:
			format = VK_FORMAT_R8G8B8A8_UNORM;
			break;
	default:
		LOG_ERROR("Error when read texture format {}", filename);
		return false;
		break;
	}

	AllocatedBufferUntyped stagingBuffer = engine.CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = engine.MapBuffer(stagingBuffer);

	std::vector<MipmapInfo> mips;
	size_t offset = 0;
	for (int i = 0; i < textureInfo.pages.size(); ++i)
	{
		ZoneScopedNC("Unpack Texture", tracy::Color::Magenta);
		MipmapInfo mip{ textureInfo.pages[i].originalSize, offset };
		mips.push_back(mip);
		assets::unpack_texture_page(&textureInfo, i, file.binaryBlob.data(), (char*)data + offset);
		offset += mip.dataSize;
	}
	engine.UnmapBuffer(stagingBuffer);

	outImage = UploadImage(textureInfo.pages[0].width, textureInfo.pages[0].height, format, engine, stagingBuffer, mips);

	engine.DestroyBuffer(stagingBuffer);

	LOG_INFO("Texture loaded successfully");

	return true;
}

AllocatedImage vkutil::UploadImage(int width, int height, VkFormat format, VulkanEngine& engine, AllocatedBufferUntyped& stagingBuffer, std::vector<MipmapInfo> mips)
{
	AllocatedImage outImage;

	VkExtent3D imageExtent;
	imageExtent.width = static_cast<uint32_t>(width);
	imageExtent.height = static_cast<uint32_t>(height);
	imageExtent.depth = 1;

	VkImageCreateInfo imageInfo = vkinit::image_create_info(format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	AllocatedImage newImage = engine.CreateImage(&imageInfo, &imgAllocInfo, format, VK_IMAGE_ASPECT_COLOR_BIT);

	engine.ImmediateSubmit([&](VkCommandBuffer cmd) {
		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = (uint32_t)mips.size();
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		VkImageMemoryBarrier imageBarrierToTransfer{};
		imageBarrierToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageBarrierToTransfer.pNext = nullptr;
		imageBarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrierToTransfer.image = newImage.image;
		imageBarrierToTransfer.subresourceRange = range;

		imageBarrierToTransfer.srcAccessMask = 0;
		imageBarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrierToTransfer);

		for (int i = 0; i < mips.size(); ++i)
		{
			VkBufferImageCopy copyRegion{};
			copyRegion.bufferOffset = mips[i].dataOffset;
			copyRegion.bufferRowLength = 0;
			copyRegion.bufferImageHeight = 0;
			copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.imageSubresource.mipLevel = i;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageExtent = imageExtent;

			vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
			imageExtent.width /= 2;
			imageExtent.height /= 2;
		}

		VkImageMemoryBarrier imageBarrierToReadable = imageBarrierToTransfer;
		imageBarrierToReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrierToReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageBarrierToReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrierToReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrierToReadable);
		}
	);

	outImage = newImage;
}


