#pragma once

#include <vk_types.h>
#include <vector>
#include <array>
#include <unordered_map>
#include <material_asset.h>

#include <vk_mesh.h>

struct ShaderEffect;
class VulkanEngine;

class PipelineBuilder {
public :
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	VertexInputDescription vertexDescription;
	VkPipelineVertexInputStateCreateInfo vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	VkViewport viewport;
	VkRect2D scissor;
	VkPipelineRasterizationStateCreateInfo rasterizer;
	VkPipelineColorBlendAttachmentState colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo multisampling;
	VkPipelineLayout pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo depthStencil;
	VkPipeline BuildPipeline(VkDevice device, VkRenderPass pass);
	void ClearVertexInput();
	void SetShaders(ShaderEffect* effect);

};

enum class VertexAttributeTemplate {
	DefaultVertex,
	DefaultVertexPosOnly,
};

class EffectBuilder {
	VertexAttributeTemplate vertexAttrib;
	ShaderEffect* effect{ nullptr };

	VkPrimitiveTopology topology;
	VkPipelineRasterizationStateCreateInfo rasterizationInfo;
	VkPipelineColorBlendAttachmentState colorBlendAttachmentInfo;
	VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
};

class ComputePipelineBuilder {
public:
	VkPipelineShaderStageCreateInfo shaderStage;
	VkPipelineLayout pipelineLayout;
	VkPipeline BuildPipeline(VkDevice device);
};

namespace vkutil
{
	struct ShaderPass {
		ShaderEffect* effect{ nullptr };
		VkPipeline pipeline{ VK_NULL_HANDLE };
		VkPipelineLayout layout{ VK_NULL_HANDLE };
	};

	struct SampledTexture {
		VkSampler sampler;
		VkImageView view;
	};

	struct ShaderParameters
	{

	};

	template<typename T>
	struct PerPassData
	{
	public:
		T& operator[](MeshpassType pass)
		{
			switch (switch_on)
			{
			case MeshpassType::Forward:
				return m_Datas[0];
			case MeshpassType::Transparency:
				return m_Datas[1];
			case MeshpassType::DirectionalShadow:
				return m_Datas[2];
			}
			assert(false);
			return m_Datas[0];
		}

		void clear(T&& val)
		{
			for (int i = 0; i < 3; ++i)
			{
				m_Datas[i] = val;
			}
		}
	private:
		std::array<T, 3> m_Datas;
	};

	struct EffectTemplate {
		PerPassData<ShaderPass*> passShaders;
		ShaderParameters* defaultParameters;
		assets::TransparencyMode transparency;
	};

	struct MaterialData
	{
		std::vector<SampledTexture> textures;
		ShaderParameters* parameters;
		std::string baseTemplate;

		bool operator==(const MaterialData& other) const;

		size_t hash() const;
	};

	struct Material {
		EffectTemplate* originalTemplate;
		PerPassData<VkDescriptorSet> passSets;
		std::vector<SampledTexture> textures;
		ShaderParameters* parameters;
		Material& operator=(const Material& other) = default;
	};

	class MaterialSystem {
	public :
		void Init(VulkanEngine* owner);
		void Cleanup();
		void BuildDefaultTemplates();
		ShaderEffect* BuildEffect(std::string_view vertexShader, std::string_view fragmentShader);
		ShaderPass* BuildShader(VkRenderPass renderPass, PipelineBuilder& builder, ShaderEffect* effect);
		Material* BuildMaterial(const std::string& materialName, const MaterialData& info);
		Material* GetMaterial(const std::string& materialName);

		void FillBuilders();
	private:
		struct MaterialInfoHash
		{
			std::size_t operator()(const MaterialData& data) const
			{
				return data.hash();
			}
		};

		PipelineBuilder m_ForwardBuilder;
		PipelineBuilder m_ShadowBuilder;

		std::unordered_map<std::string, EffectTemplate> m_TemplateCache;
		std::unordered_map<std::string, Material*> m_Materials;
		std::unordered_map<MaterialData, Material*, MaterialInfoHash> m_MaterialCache;
		VulkanEngine* m_Engine;
	};
}