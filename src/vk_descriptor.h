#pragma once
#include <vk_types.h>
#include <vector>
#include <array>
#include <unordered_map>

namespace vkutil
{
	class DescriptorAllocator {
	public:
		struct PoolSizes {
			std::vector<std::pair<VkDescriptorType, float>> sizes = {
				{VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f},
				{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f},
				{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f},
				{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f},
				{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f},
				{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f},
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f},
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f},
				{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f},
			};
		};

		void reset_pools();

		bool allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);

		void init(VkDevice device);

		void cleanup();

		VkDevice device;
	private:
		VkDescriptorPool grab_pool();

		VkDescriptorPool m_CurrentPool{ VK_NULL_HANDLE };

		PoolSizes m_DescritorSizes;

		std::vector<VkDescriptorPool> m_UsedPools;

		std::vector<VkDescriptorPool> m_FreePools;

	};

	class DescriptorLayoutCache {
	public:
		void init(VkDevice device);

		void cleanup();

		VkDescriptorSetLayout CreateDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info);

		struct DescriptorLayoutInfo {
			std::vector<VkDescriptorSetLayoutBinding> bindings;

			bool operator==(const DescriptorLayoutInfo& other) const;

			size_t hash() const;
		};
	private:
		struct DescriptorLayoutHash {
			std::size_t operator()(const DescriptorLayoutInfo& k) const
			{
				return k.hash();
			}
		};

		std::unordered_map<DescriptorLayoutCache::DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> m_LayoutCache;

		VkDevice m_Device;
	};

	class DescriptorBuilder {
	public:
		static DescriptorBuilder Begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator);

		DescriptorBuilder& BindBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);

		DescriptorBuilder& BindImage(uint32_t binding, VkDescriptorImageInfo* imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);

		bool Build(VkDescriptorSet& set, VkDescriptorSetLayout& layout);

		bool Build(VkDescriptorSet& set);
	private:
		std::vector<VkWriteDescriptorSet> m_Writes;

		std::vector<VkDescriptorSetLayoutBinding> m_Bindings;

		DescriptorLayoutCache* m_pCache;

		DescriptorAllocator* m_pAllocator;
	};

	static bool operator==(const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b)
	{
		return a.binding == b.binding &&
			a.descriptorType == b.descriptorType &&
			a.descriptorCount == b.descriptorCount &&
			a.stageFlags == b.stageFlags;
	}

	static bool operator!=(const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b)
	{
		return !(a == b);
	}
}