#include <vk_shader.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <assert.h>
#include <iostream>
#include <spirv_reflect.h>
#include <string_utils.h>
#include <vk_initializers.h>

bool vkutil::LoadShaderModule(VkDevice device, const char* filePath, ShaderModule* outShaderModule)
{
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
		return false;

	size_t fileSize = (size_t)file.tellg();

	//spirv expects the buffer to be on uint32, so make sure to reserve a int vector big enough for the entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}

	outShaderModule->code = std::move(buffer);
	outShaderModule->module = shaderModule;
	return true;
}

uint32_t vkutil::HashDescriptorLayoutInfo(VkDescriptorSetLayoutCreateInfo* info)
{
	std::stringstream ss;
	ss << info->flags;
	ss << info->bindingCount;

	for (uint32_t i = 0u; i < info->bindingCount; ++i)
	{
		const VkDescriptorSetLayoutBinding& binding = info->pBindings[i];
		ss << binding.binding;
		ss << binding.descriptorCount;
		ss << binding.descriptorType;
		ss << binding.stageFlags;
	}

	auto str = ss.str();

	return StringUtils::fnv1a_32(str.c_str(), str.length());
}

void ShaderEffect::AddStage(ShaderModule* shaderModule, VkShaderStageFlagBits stage)
{
	ShaderStage newStage = { shaderModule, stage };
	stages.push_back(newStage);
}

struct DescriptorSetLayoutData {
	uint32_t setNumber;
	VkDescriptorSetLayoutCreateInfo createInfo;
	std::vector<VkDescriptorSetLayoutBinding> bindings;
};

void ShaderEffect::ReflectLayout(VkDevice device, ReflectionOverrides* overrides, int overrideCount)
{
	std::vector<DescriptorSetLayoutData> setLayoutArray;
	std::vector<VkPushConstantRange> constantRanges;

	for (auto& s : stages)
	{
		SpvReflectShaderModule spvModule;
		SpvReflectResult result = spvReflectCreateShaderModule(s.shaderModule->code.size() * sizeof(uint32_t), s.shaderModule->code.data(), &spvModule);

		uint32_t count = 0;
		result = spvReflectEnumerateDescriptorSets(&spvModule, &count, NULL);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		std::vector<SpvReflectDescriptorSet*> sets(count);
		result = spvReflectEnumerateDescriptorSets(&spvModule, &count, sets.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		for (size_t setIdx = 0; setIdx < sets.size(); ++setIdx)
		{
			const SpvReflectDescriptorSet& reflectSet = *(sets[setIdx]);
			DescriptorSetLayoutData layout{};
			layout.bindings.resize(reflectSet.binding_count);
			for (uint32_t bindingIdx = 0; bindingIdx < reflectSet.binding_count; ++bindingIdx)
			{
				const SpvReflectDescriptorBinding& reflectBinding = *(reflectSet.bindings[bindingIdx]);
				VkDescriptorSetLayoutBinding& layoutBinding = layout.bindings[bindingIdx];
				layoutBinding.binding = reflectBinding.binding;
				layoutBinding.descriptorType = static_cast<VkDescriptorType>(reflectBinding.descriptor_type);
				for (int ov = 0; ov < overrideCount; ++ov)
				{
					if (strcmp(overrides[ov].name, reflectBinding.name) == 0)
					{
						layoutBinding.descriptorType = overrides[ov].overridenType;
					}
				} 

				layoutBinding.descriptorCount = 1;
				for (uint32_t dim = 0; dim < reflectBinding.array.dims_count; ++dim)
				{
					layoutBinding.descriptorCount *= reflectBinding.array.dims[dim];
				}
				layoutBinding.stageFlags = static_cast<VkShaderStageFlagBits>(spvModule.shader_stage);

				ReflectedBinding reflected;
				reflected.binding = layoutBinding.binding;
				reflected.set = reflectSet.set;
				reflected.type = layoutBinding.descriptorType;
				bindings[reflectBinding.name] = reflected;
			}
			layout.setNumber = reflectSet.set;
			layout.createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layout.createInfo.bindingCount = reflectSet.binding_count;
			layout.createInfo.pBindings = layout.bindings.data();

			setLayoutArray.push_back(layout);
		}

		//push_constant
		result = spvReflectEnumeratePushConstantBlocks(&spvModule, &count, nullptr);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		std::vector<SpvReflectBlockVariable*> constants(count);
		result = spvReflectEnumeratePushConstantBlocks(&spvModule, &count, constants.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		//spvReflectEnumeratePushConstantBlocks only get the push constant blocks for the first one
		if (count > 0)
		{
			VkPushConstantRange pushConstant{};
			pushConstant.offset = constants[0]->offset;
			pushConstant.size = constants[0]->size;
			pushConstant.stageFlags = s.stage;

			constantRanges.push_back(pushConstant);
		}
	}

	std::array<DescriptorSetLayoutData, 4> mergedLayouts;

	for (int i = 0; i < 4; ++i)
	{
		DescriptorSetLayoutData& layout = mergedLayouts[i];
		layout.setNumber = i;
		layout.createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

		std::unordered_map<int, VkDescriptorSetLayoutBinding> binds;
		for (auto& s : setLayoutArray)
		{
			if (s.setNumber == i)
			{
				for (auto& b : s.bindings)
				{
					auto it = binds.find(b.binding);
					if (it == binds.end())
					{
						binds[b.binding] = b;
					}
					else
					{
						//merge flags
						binds[b.binding].stageFlags |= b.stageFlags;
					}
				}
			}
		}
		for (auto [k, v] : binds)
		{
			layout.bindings.push_back(v);
		}

		std::sort(layout.bindings.begin(), layout.bindings.end(), [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b) {
			return a.binding < b.binding;
			});

		layout.createInfo.bindingCount = (uint32_t)layout.bindings.size();
		layout.createInfo.pBindings = layout.bindings.data();
		layout.createInfo.flags = 0;
		layout.createInfo.pNext = nullptr;

		if (layout.createInfo.bindingCount > 0)
		{
			setHashes[i] = vkutil::HashDescriptorLayoutInfo(&layout.createInfo);
			vkCreateDescriptorSetLayout(device, &layout.createInfo, nullptr, &setLayouts[i]);
		}
		else
		{
			setHashes[i] = 0;
			setLayouts[i] = VK_NULL_HANDLE;
		}
	}

	std::array<VkDescriptorSetLayout, 4> compactedLayouts;
	uint32_t compactedCount = 0;
	for (int i = 0; i < 4; ++i)
	{
		if (setLayouts[i] != VK_NULL_HANDLE)
		{
			compactedLayouts[compactedCount] = setLayouts[i];
			++compactedCount;
		}
	}
	VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info(constantRanges.data(), constantRanges.size(), compactedLayouts.data(), compactedCount);
	vkCreatePipelineLayout(device, &layoutInfo, nullptr, &builtLayout);
}

void ShaderEffect::FillStages(std::vector<VkPipelineShaderStageCreateInfo>& pipelineStages)
{

}

void ShaderDescritptorBinder::BindBuffer(const char* name, const VkDescriptorBufferInfo& bufferInfo)
{

}

void ShaderDescritptorBinder::BindDynamicBuffer(const char* name, uint32_t offset, const VkDescriptorBufferInfo& bufferInfo)
{

}

void ShaderDescritptorBinder::ApplyBinds(VkCommandBuffer cmd)
{

}

void ShaderDescritptorBinder::BuildSets(VkDevice device, vkutil::DescriptorAllocator& allocator)
{

}

void ShaderDescritptorBinder::SetShader(ShaderEffect* newShader)
{

}

ShaderModule* ShaderCache::GetShader(const std::string& path)
{

}
