#include <material_system.h>
#include <vk_shader.h>
#include <vk_engine.h>
#include <vk_initializers.h>
#include "vk_pipeline_builder.h"
void vkutil::MaterialSystem::Init(VulkanEngine* owner)
{
	m_Engine = owner;
	BuildDefaultTemplates();
}

void vkutil::MaterialSystem::Cleanup()
{
	for (auto it : m_MaterialCache)
	{
		delete it.second;
	}
	m_Materials.clear();
	m_MaterialCache.clear();
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

	effect->ReflectLayout(m_Engine->device(), overrides, 2);

	return effect;
}

void vkutil::MaterialSystem::BuildDefaultTemplates()
{
	FillBuilders();

	ShaderEffect* texturedLit = BuildEffect("tri_mesh_ssbo_instanced.vert.spv", "textured_lit.frag.spv");
	ShaderEffect* defaultLit = BuildEffect("tri_mesh_ssbo_instanced.vert.spv", "default_lit.frag.spv");
	ShaderEffect* opaqueShadowcast = BuildEffect("tri_mesh_ssbo_instanced_shadowcast.vert.spv", "");

	ShaderPass* texturedLitPass = BuildShader(m_Engine->GetRenderPass(PassType::Forward), m_ForwardBuilder, texturedLit);
	ShaderPass* defaultLitPass = BuildShader(m_Engine->GetRenderPass(PassType::Forward), m_ForwardBuilder, defaultLit);
	ShaderPass* opaqueShadowcastPass = BuildShader(m_Engine->GetRenderPass(PassType::Shadow), m_ShadowBuilder, opaqueShadowcast);

	{
		EffectTemplate defaultTextured;
		defaultTextured.passShaders[MeshpassType::Transparency] = nullptr;
		defaultTextured.passShaders[MeshpassType::Forward] = texturedLitPass;
		defaultTextured.passShaders[MeshpassType::DirectionalShadow] = opaqueShadowcastPass;
		
		defaultTextured.defaultParameters = nullptr;
		defaultTextured.transparency = assets::TransparencyMode::Opaque;

		m_TemplateCache["texturedPBR_opaque"] = defaultTextured;
	}
	{
		PipelineBuilder transparentForward = m_ForwardBuilder;
		transparentForward.colorBlendAttachment.blendEnable = VK_TRUE;
		transparentForward.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		transparentForward.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		transparentForward.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

		transparentForward.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;

		transparentForward.depthStencil.depthWriteEnable = false;
		transparentForward.rasterizer.cullMode = VK_CULL_MODE_NONE;

		ShaderPass* transparentLitPass = BuildShader(m_Engine->GetRenderPass(PassType::Forward), transparentForward, texturedLit);

		EffectTemplate defaultTextured;
		defaultTextured.passShaders[MeshpassType::Transparency] = transparentLitPass;
		defaultTextured.passShaders[MeshpassType::DirectionalShadow] = nullptr;
		defaultTextured.passShaders[MeshpassType::Forward] = nullptr;

		defaultTextured.defaultParameters = nullptr;
		defaultTextured.transparency = assets::TransparencyMode::Transparent;

		m_TemplateCache["texturedPBR_transparent"] = defaultTextured;
	}
	{
		EffectTemplate defaultColored;

		defaultColored.passShaders[MeshpassType::Transparency] = nullptr;
		defaultColored.passShaders[MeshpassType::DirectionalShadow] = opaqueShadowcastPass;
		defaultColored.passShaders[MeshpassType::Forward] = defaultLitPass;
		defaultColored.defaultParameters = nullptr;
		defaultColored.transparency = assets::TransparencyMode::Opaque;
		m_TemplateCache["colored_opaque"] = defaultColored;
	}
}

vkutil::ShaderPass* vkutil::MaterialSystem::BuildShader(VkRenderPass renderPass, PipelineBuilder& builder, ShaderEffect* effect)
{
	ShaderPass* pPass = new ShaderPass();
	pPass->effect = effect;
	pPass->layout = effect->builtLayout;

	PipelineBuilder pipBuilder = builder;
	pipBuilder.SetShaders(effect);
	pPass->pipeline = pipBuilder.BuildPipeline(m_Engine->device(), renderPass);

	return pPass;
}

vkutil::Material* vkutil::MaterialSystem::BuildMaterial(const std::string& materialName, const MaterialData& info)
{
	Material* pMat;

	auto matIt = m_Materials.find(materialName);
	if (matIt != m_Materials.end())
	{
		LOG_FATAL("Build material error, material is exist :{}", materialName);
		return (*matIt).second;
	}
	auto cacheIt = m_MaterialCache.find(info);
	if (cacheIt != m_MaterialCache.end())
	{
		pMat = (*cacheIt).second;
		
		m_Materials[materialName] = pMat;
	}
	else
	{
		Material* pNewMat = new Material();
		pNewMat->originalTemplate = &m_TemplateCache[info.baseTemplate];
		pNewMat->parameters = info.parameters;

		pNewMat->passSets[MeshpassType::DirectionalShadow] = VK_NULL_HANDLE;
		pNewMat->textures = info.textures;

		auto& db = vkutil::DescriptorBuilder::Begin(m_Engine->descriptorLayoutCache(), m_Engine->descriptorAllocator());
		for (int i = 0; i < info.textures.size(); ++i)
		{
			VkDescriptorImageInfo imageBufferInfo{};
			imageBufferInfo.sampler = info.textures[i].sampler;
			imageBufferInfo.imageView = info.textures[i].view;
			imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			db.BindImage(i, &imageBufferInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
		}

		db.Build(pNewMat->passSets[MeshpassType::Forward]);
		db.Build(pNewMat->passSets[MeshpassType::Transparency]);
		LOG_INFO("Built New Material {}", materialName);

		m_MaterialCache[info] = pNewMat;
		pMat = pNewMat;
		m_Materials[materialName] = pMat;
	}
	return pMat;
}

vkutil::Material* vkutil::MaterialSystem::GetMaterial(const std::string& materialName)
{
	auto it = m_Materials.find(materialName);
	if (it != m_Materials.end())
	{
		return(*it).second;
	}
	else {
		return nullptr;
	}
}

void vkutil::MaterialSystem::FillBuilders()
{
	{
		m_ShadowBuilder.vertexDescription = Vertex::get_vertex_description();
		m_ShadowBuilder.inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		m_ShadowBuilder.rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
		m_ShadowBuilder.rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
		m_ShadowBuilder.rasterizer.depthBiasEnable = VK_TRUE;
		m_ShadowBuilder.multisampling = vkinit::multisampling_state_create_info();
		m_ShadowBuilder.colorBlendAttachment = vkinit::color_blend_attachment_state();
		m_ShadowBuilder.depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS);
	}
	{
		m_ForwardBuilder.vertexDescription = Vertex::get_vertex_description();
		m_ForwardBuilder.inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		m_ForwardBuilder.rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
		m_ForwardBuilder.rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;

		m_ForwardBuilder.multisampling = vkinit::multisampling_state_create_info();
		m_ForwardBuilder.colorBlendAttachment = vkinit::color_blend_attachment_state();
		m_ForwardBuilder.depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_GREATER_OR_EQUAL);
	}
}

VkPipeline PipelineBuilder::BuildPipeline(VkDevice device, VkRenderPass pass)
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

VkPipeline ComputePipelineBuilder::BuildPipeline(VkDevice device)
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
