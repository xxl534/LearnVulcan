#include <material_system.h>

void vkutil::MaterialSystem::Init(VulkanEngine* owner)
{
	m_Engine = owner;
	BuildDefaultTemplates();
}

void vkutil::MaterialSystem::Cleanup()
{

}

ShaderEffect* buildEffect(VulkanEngine* engine, std::string_view vertexShader, std::string_view fragmentShader)
{
	
}
void vkutil::MaterialSystem::BuildDefaultTemplates()
{

}

vkutil::ShaderPass* vkutil::MaterialSystem::BuildShader(VkRenderPass renderPass, PipelineBuilder& builder, ShaderEffect* effect)
{

}

vkutil::Material* vkutil::MaterialSystem::BuildMaterial(const std::string& materialName, const MaterialData& info)
{

}

vkutil::Material* vkutil::MaterialSystem::GetMaterial(const std::string& materialName)
{

}

void vkutil::MaterialSystem::FillBuilders()
{

}

