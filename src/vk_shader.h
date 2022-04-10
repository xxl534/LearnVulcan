#pragma once

#include <vk_types.h>
#include <vector>
#include <array>
#include <unordered_map>
#include <vk_descriptor.h>

struct  ShaderModule
{
	std::vector<uint32_t> code;
	VkShaderModule module;
};

namespace vkutil
{
	//loads a shader module from a spir-v file. Returns false if it errors	
	bool LoadShaderModule(VkDevice device, const char* filePath, ShaderModule* outShaderModule);

	uint32_t HashDescriptorLayoutInfo(VkDescriptorSetLayoutCreateInfo* info);
}

class VulkanEngine;

//holds all information for a given shader set for pipeline
struct ShaderEffect {
	struct ReflectionOverrides {
		const char* name;
		VkDescriptorType overridenType;
	};

	void AddStage(ShaderModule* shaderModule, VkShaderStageFlagBits stage);
	void ReflectLayout(VkDevice device, ReflectionOverrides* overrides, int overrideCount);
	void FillStages(std::vector<VkPipelineShaderStageCreateInfo>& pipelineStages);

	VkPipelineLayout builtLayout;

	struct ReflectedBinding {
		uint32_t set;
		uint32_t binding;
		VkDescriptorType type;
	};

	std::unordered_map<std::string, ReflectedBinding> bindings;
	std::array<VkDescriptorSetLayout, 4> setLayouts;
	std::array<uint32_t, 4> setHashes;
private:
	struct ShaderStage {
		ShaderModule* shaderModule;
		VkShaderStageFlagBits stage;
	};

	std::vector<ShaderStage> stages;
};

struct ShaderDescritptorBinder{
	struct BufferWriteDescriptor {
		int dstSet;
		int dstBinding;
		VkDescriptorType descriptorType;
		VkDescriptorBufferInfo bufferInfo;

		uint32_t dynamicOffset;
	};

	void BindBuffer(const char* name, const VkDescriptorBufferInfo& bufferInfo);

	void BindDynamicBuffer(const char* name, uint32_t offset, const VkDescriptorBufferInfo& bufferInfo);

	void ApplyBinds(VkCommandBuffer cmd);

	void BuildSets(VkDevice device, vkutil::DescriptorAllocator& allocator);

	void SetShader(ShaderEffect* newShader);

	std::array<VkDescriptorSet, 4> cachedDescriptorSets;
private:
	struct DynamicOffsets {
		std::array<uint32_t, 16> offsets;
		uint32_t count{ 0 };
	};

	std::array<DynamicOffsets, 4> m_SetOffsets;

	ShaderEffect* m_ShaderEffect{ nullptr };
	std::vector<BufferWriteDescriptor> m_BufferWrites;
};

class ShaderCache {
public:
	ShaderModule* GetShader(const std::string& path);

	void Init(VkDevice device)
	{
		m_Device = device;
	}
private:
	VkDevice m_Device;
	std::unordered_map<std::string, ShaderModule> m_Cache;
};