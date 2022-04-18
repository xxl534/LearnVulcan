#pragma once
#include <vk_types.h>

namespace vkutil {
	struct PushBuffer {
		template<typename T>
		uint32_t Push(T& data);

		uint32_t Push(void* data, size_t size);

		void Init(VmaAllocator& allocator, AllocatedBufferUntyped sourceBuffer, uint32_t alignment);

		void PushBegin();
		void PushEnd();
		void reset();

		uint32_t pad_uniform_buffer_size(uint32_t originalSize);
		AllocatedBufferUntyped source;
		uint32_t align;
		uint32_t currentOffset;
		void* mapped;
	private:
		VmaAllocator* m_Allocator;
	};

	template<typename T>
	uint32_t PushBuffer::Push(T& data)
	{
		return Push(&data, sizeof(T));
	}
}