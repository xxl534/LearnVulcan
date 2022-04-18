#include <vk_pushbuffer.h>

uint32_t vkutil::PushBuffer::Push(void* data, size_t size)
{
	uint32_t offset = currentOffset;
	char* target = (char*)mapped;
	target += currentOffset;
	memcpy(target, data, size);
	currentOffset += size;
	currentOffset = pad_uniform_buffer_size(currentOffset);
}

void vkutil::PushBuffer::Init(VmaAllocator& allocator, AllocatedBufferUntyped sourceBuffer, uint32_t alignment)
{
	m_Allocator = &allocator;
	align = alignment;
	source = sourceBuffer;
	currentOffset = 0;
	mapped = nullptr;
}

void vkutil::PushBuffer::PushBegin()
{
	assert(mapped == nullptr);
	vmaMapMemory(*m_Allocator, source.allocation, &mapped);
}

void vkutil::PushBuffer::PushEnd()
{
	assert(mapped != nullptr);
	vmaUnmapMemory(*m_Allocator, source.allocation);
}

void vkutil::PushBuffer::reset()
{
	currentOffset = 0;
}

uint32_t vkutil::PushBuffer::pad_uniform_buffer_size(uint32_t originalSize)
{
	size_t minUboAlignment = align;
	size_t alignedSize = originalSize;
	if (minUboAlignment > 0) {
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}
	return static_cast<uint32_t>(alignedSize);
}

