#include <vk_descriptor.h>
#include <algorithm>

namespace vkutil
{
	VkDescriptorPool createPool(VkDevice device, const DescriptorAllocator::PoolSizes& poolSizes, int count, VkDescriptorPoolCreateFlags flags)
	{
		std::vector<VkDescriptorPoolSize> sizes;
		sizes.reserve(poolSizes.sizes.size());
		for (auto sz : poolSizes.sizes)
		{
			sizes.push_back({ sz.first, uint32_t(sz.second * count) });
		}
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.pNext = NULL;
		poolInfo.flags = false;
		poolInfo.maxSets = count;
		poolInfo.poolSizeCount = (uint32_t)sizes.size();
		poolInfo.pPoolSizes = sizes.data();

		VkDescriptorPool descriptorPool;
		vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

		return descriptorPool;
	}

	void DescriptorAllocator::reset_pools()
	{
		for (auto pool : m_UsedPools)
		{
			vkResetDescriptorPool(device, pool, 0);
		}

		m_FreePools = m_UsedPools;
		m_UsedPools.clear();
		m_CurrentPool = VK_NULL_HANDLE;
	}

	bool DescriptorAllocator::allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout)
	{
		if (m_CurrentPool == VK_NULL_HANDLE)
		{
			m_CurrentPool = grab_pool();
			m_UsedPools.push_back(m_CurrentPool);
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;

		allocInfo.pSetLayouts = &layout;
		allocInfo.descriptorPool = m_CurrentPool;
		allocInfo.descriptorSetCount = 1;

		VkResult allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);
		bool needReallocated = false;

		switch (allocResult)
		{
		case VK_SUCCESS:
			return true;
		case VK_ERROR_FRAGMENTED_POOL:
		case VK_ERROR_OUT_OF_POOL_MEMORY:
			needReallocated = true;
			break;
		default:
			return false;
		}

		if (needReallocated)
		{
			m_CurrentPool = grab_pool();
			m_UsedPools.push_back(m_CurrentPool);

			allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);

			if (allocResult == VK_SUCCESS)
			{
				return true;
			}
		}
		return false;
	}

	void DescriptorAllocator::init(VkDevice device)
	{
		this->device = device;
	}

	void DescriptorAllocator::cleanup()
	{
		for (auto pool : m_FreePools)
		{
			vkDestroyDescriptorPool(device, pool, nullptr);
		}
		for (auto pool : m_UsedPools)
		{
			vkDestroyDescriptorPool(device, pool, nullptr);
		}
	}

	VkDescriptorPool DescriptorAllocator::grab_pool()
	{
		if (m_FreePools.size() > 0)
		{
			VkDescriptorPool pool = m_FreePools.back();
			m_FreePools.pop_back();
			return pool;
		}
		else
		{
			return createPool(device, m_DescritorSizes, 1000, 0);
		}
	}

	void DescriptorLayoutCache::init(VkDevice device)
	{
		m_Device = device;
	}

	void DescriptorLayoutCache::cleanup()
	{
		for (auto pair : m_LayoutCache)
		{
			vkDestroyDescriptorSetLayout(m_Device, pair.second, nullptr);
		}
	}

	VkDescriptorSetLayout DescriptorLayoutCache::create_descriptor_layout(VkDescriptorSetLayoutCreateInfo* info)
	{
		DescriptorLayoutInfo layoutInfo;
		layoutInfo.bindings.reserve(info->bindingCount);
		bool isSorted = true;
		int32_t lastBinding = -1;
		for (uint32_t i = 0; i < info->bindingCount; ++i)
		{
			layoutInfo.bindings.push_back(info->pBindings[i]);

			if (static_cast<int32_t>(info->pBindings[i].binding) > lastBinding)
			{
				lastBinding = info->pBindings[i].binding;
			}
			else
			{
				isSorted = false;
			}
		}
		if (!isSorted)
		{
			std::sort(layoutInfo.bindings.begin(), layoutInfo.bindings.end(), [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b) {
				return a.binding < b.binding;
				});
		}

		auto it = m_LayoutCache.find(layoutInfo);
		if (it != m_LayoutCache.end())
		{
			return (*it).second;
		}
		else
		{
			VkDescriptorSetLayout layout;
			vkCreateDescriptorSetLayout(m_Device, info, nullptr, &layout);

			m_LayoutCache[layoutInfo] = layout;
			return layout;
		}
	}

	bool DescriptorLayoutCache::DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const
	{
		if (other.bindings.size() != bindings.size())
			return false;
		else
		{
			for (int i = 0; i < bindings.size(); ++i)
			{
				if (other.bindings[i] != bindings[i])
				{
					return false;
				}
			}
			return true;
		}
	}

	size_t DescriptorLayoutCache::DescriptorLayoutInfo::hash() const
	{
		using std::size_t;
		using std::hash;

		size_t result = hash<size_t>()(bindings.size());

		for (const VkDescriptorSetLayoutBinding& binding : bindings)
		{
			size_t bindingHash = binding.binding | binding.descriptorType << 8 | binding.descriptorCount << 16 | binding.stageFlags << 24;

			result ^= hash<size_t>()(bindingHash);
		}
		return result;
	}

	DescriptorBuilder DescriptorBuilder::begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator)
	{
		DescriptorBuilder builder;
		builder.m_pCache = layoutCache;
		builder.m_pAllocator = allocator;
		return builder;
	}

	DescriptorBuilder& DescriptorBuilder::bind_buffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags)
	{
		VkDescriptorSetLayoutBinding newBinding{};
		newBinding.descriptorCount = 1;
		newBinding.descriptorType = type;
		newBinding.pImmutableSamplers = nullptr;
		newBinding.stageFlags = stageFlags;
		newBinding.binding = binding;

		m_Bindings.push_back(newBinding);

		VkWriteDescriptorSet newWrite{};
		newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		newWrite.pNext = NULL;

		newWrite.descriptorCount = 1;
		newWrite.descriptorType = type;
		newWrite.pBufferInfo = bufferInfo;
		newWrite.dstBinding = binding;

		m_Writes.push_back(newWrite);
		return *this;
	}

	DescriptorBuilder& DescriptorBuilder::bind_image(uint32_t binding, VkDescriptorImageInfo* imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags)
	{
		VkDescriptorSetLayoutBinding newBinding{};
		newBinding.descriptorCount = 1;
		newBinding.descriptorType = type;
		newBinding.pImmutableSamplers = nullptr;
		newBinding.stageFlags = stageFlags;
		newBinding.binding = binding;

		m_Bindings.push_back(newBinding);

		VkWriteDescriptorSet newWrite{};
		newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		newWrite.pNext = NULL;

		newWrite.descriptorCount = 1;
		newWrite.descriptorType = type;
		newWrite.pImageInfo = imageInfo;
		newWrite.dstBinding = binding;

		m_Writes.push_back(newWrite);
		return *this;
	}

	bool DescriptorBuilder::build(VkDescriptorSet& set, VkDescriptorSetLayout& layout)
	{
		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.pNext = nullptr;

		layoutInfo.pBindings = m_Bindings.data();
		layoutInfo.bindingCount = static_cast<uint32_t>(m_Bindings.size());

		layout = m_pCache->create_descriptor_layout(&layoutInfo);
		
		bool success = m_pAllocator->allocate(&set, layout);
		if (!success) {
			return false;
		}

		for (VkWriteDescriptorSet& write : m_Writes)
		{
			write.dstSet = set;
		}

		vkUpdateDescriptorSets(m_pAllocator->device, static_cast<uint32_t>(m_Writes.size()), m_Writes.data(), 0, nullptr);

		return true;
	}

	bool DescriptorBuilder::build(VkDescriptorSet& set)
	{
		VkDescriptorSetLayout layout;
		return build(set, layout);
	}

}
