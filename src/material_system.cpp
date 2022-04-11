#include <material_system.h>
#include <vk_shader.h>
#include <vk_engine.h>
#include <vk_initializers.h>
void vkutil::MaterialSystem::Init(VulkanEngine* owner)
{
	m_Engine = owner;
	BuildDefaultTemplates();
}

void vkutil::MaterialSystem::Cleanup()
{

}

ShaderEffect* vkutil::MaterialSystem::BuildEffect(std::string_view vertexShader, std::string_view fragmentShader)
{
	ShaderEffect::ReflectionOverrides overrides[] = {
		{"sceneData", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
		{"cameraData", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
	};

	ShaderEffect* effect = new ShaderEffect();

	effect->AddStage(m_Engine->GetShaderModule(VulkanEngine::ShaderPath(vertexShader)), VK_SHADER_STAGE_VERTEX_BIT);
	if (fragmentShader.size() > 2)
	{
		effect->AddStage(m_Engine->GetShaderModule(VulkanEngine::ShaderPath(fragmentShader)), VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	effect->ReflectLayout(m_Engine->GetDevice(), overrides, 2);

	return effect;
}

void vkutil::MaterialSystem::BuildDefaultTemplates()
{

}

vkutil::ShaderPass* vkutil::MaterialSystem::BuildShader(VkRenderPass renderPass, PipelineBuilder& builder, ShaderEffect* effect)
{
	ShaderPass* pPass = new ShaderPass();
	pPass->effect = effect;
	pPass->layout = effect->builtLayout;

	PipelineBuilder pipBuilder = builder;
	pipBuilder.SetShaders(effect);
	pPass->pipeline = pipBuilder.buildPipeline(m_Engine->GetDevice(), renderPass);

	return pPass;
}

vkutil::Material* vkutil::MaterialSystem::BuildMaterial(const std::string& materialName, const MaterialData& info)
{
	Material* pMat;

	auto it = m_MaterialCache.find(info);
	if (it != m_MaterialCache.end())
	{
		pMat = (*it).second;
		m_Materials[materialName] = pMat;
	}
	else
	{
		Material* pNewMat = new Material();
		pNewMat->original = &m_TemplateCache[info.baseTemplate];
		pNewMat->parameters = info.parameters;

		pNewMat->passSets[MeshpassType::DirectionalShadow] = VK_NULL_HANDLE;
		pNewMat->textures = info.textures;

		auto& db = vkutil::DescriptorBuilder::begin(m_Engine->descriptorLayoutCache, m_Engine->descriptorAllocator);
		for (int i = 0; i < info.textures.size(); ++i)
		{

		}
	}
	return pMat;
}

vkutil::Material* vkutil::MaterialSystem::GetMaterial(const std::string& materialName)
{

}

void vkutil::MaterialSystem::FillBuilders()
{

}

VkPipeline PipelineBuilder::buildPipeline(VkDevice device, VkRenderPass pass)
{
	vertexInputInfo = vkinit::vertex_input_state_create_info();
	vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)vertexDescription.attributes.size();

	vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineColorBlendStateCreateInfo colorBlend{};
	colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlend.pNext = nullptr;
	colorBlend.logicOpEnable = VK_FALSE;
	colorBlend.logicOp = VK_LOGIC_OP_COPY;
	colorBlend.attachmentCount = 1;
	colorBlend.pAttachments = &colorBlendAttachment;

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlend;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.pNext = nullptr;

	std::vector<VkDynamicState> dynamicStates;
	dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
	dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
	dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
	dynamicStateInfo.pDynamicStates = dynamicStates.data();
	dynamicStateInfo.dynamicStateCount = dynamicStates.size();

	pipelineInfo.pDynamicState = &dynamicStateInfo;

	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		LOG_FATAL("Failed to build graphics pipeline");
		return VK_NULL_HANDLE;
	}
	else
	{
		return newPipeline;
	}
}

void PipelineBuilder::ClearVertexInput()
{
	vertexInputInfo.pVertexAttributeDescriptions = nullptr;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;

	vertexInputInfo.pVertexBindingDescriptions = nullptr;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
}

void PipelineBuilder::SetShaders(ShaderEffect* effect)
{
	shaderStages.clear();
	effect->FillStages(shaderStages);

	pipelineLayout = effect->builtLayout;
}

VkPipeline ComputePipelineBuilder::build_pipeline(VkDevice device)
{
	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stage = shaderStage;
	pipelineInfo.layout = pipelineLayout;

	VkPipeline newPipeline;
	if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		LOG_FATAL("Failed to build compute pipeline");
	}
	else
	{
		return newPipeline;
	}
}

bool vkutil::MaterialData::operator==(const MaterialData& other) const
{
	if (other.baseTemplate.compare(baseTemplate) != 0 || other.parameters != parameters || other.textures.size() != textures.size())
		return false;
	else
	{
		bool comp = memcmp(other.textures.data(), textures.data(), textures.size() * sizeof(textures[0])) == 0;
		return comp;
	}
}

size_t vkutil::MaterialData::hash() const
{
	using std::size_t;
	using std::hash;

	size_t result = hash<std::string>()(baseTemplate);
	for (const auto& b : textures)
	{
		size_t textureHash = (std::hash<size_t>()((size_t)b.sampler) << 3) && (std::hash<size_t>()((size_t)b.view) >> 7);
		result ^= std::hash<size_t>()(textureHash);
	}
	return result;
}
