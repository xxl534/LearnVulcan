// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct AllocatedBufferUntyped {
	VkBuffer buffer{};
	VmaAllocation allocation{};
	VkDeviceSize size{ 0 };
	VkDescriptorBufferInfo GetInfo(VkDeviceSize offset = 0) {
		VkDescriptorBufferInfo info;
		info.buffer = buffer;
		info.offset = offset;
		info.range = size;
		return info;
	}
};

template<typename T>
struct AllocatedBuffer : public AllocatedBufferUntyped {
	void operator=(const AllocatedBufferUntyped& other)
	{
		buffer = other.buffer;
		allocation = other.allocation;
		size = other.size;
	}
	AllocatedBuffer(AllocatedBufferUntyped& other)
	{
		buffer = other.buffer;
		allocation = other.allocation;
		size = other.size;
	}
	AllocatedBuffer() = default;
};

struct AllocatedImage {
	VkImage image;
	VmaAllocation allocation;
	VkImageView defaultView;
	int mipLevels;
};

enum class MeshpassType : uint8_t {
	None = 0,
	Forward = 1,
	Transparency = 2,
	DirectionalShadow = 3,
	Count,
};

//we will add our main reusable types here