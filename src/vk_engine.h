// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <deque>
#include <functional>
#include <unordered_map>

#include <vk_mem_alloc.h>
#include <vk_mesh.h>
#include <vk_shader.h>
#include <material_system.h>

#include <glm/glm.hpp>

struct UploadContext {
	VkFence uploadFence;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
};
struct GPUSceneData
{
	glm::vec4 fogColor;
	glm::vec4 fogDistance;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection;
	glm::vec4 sunlightColor;
};

struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
};

struct GPUObjectData {
	glm::vec4 color;
	glm::mat4 modelMatrix;
};

struct FrameData {
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptor;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;
};

struct PipelineConstants {
	glm::vec4 data;
	glm::mat4 render_matrix;
};

enum ShaderType {
	ShaderType_Fragment,
	ShaderType_Vertex,
	ShaderType_Compute,
};

struct Material {
	VkDescriptorSet textureSet{ VK_NULL_HANDLE };
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct RenderObject{
	Mesh* mesh;
	Material* material;
	glm::mat4 transformMatrix;
};

struct MeshObject {
	Mesh* mesh{ nullptr };
	vkutil::Material* material;
	uint32_t customSortKey;
	glm::mat4 transformMatrix;

	RenderBounds bounds;

	uint32_t bDrawForwardPass : 1;
	uint32_t bDrawShadowPass : 1;
};
struct Texture {
	AllocatedImage image;
	VkImageView imageView;
};

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		for (auto it = deletors.rbegin(); it != deletors.rend(); ++it)
		{
			(*it)();
		}

		deletors.clear();
	}
};

const unsigned int FRAME_OVERLAP = 2;
enum PassType {
	Forward,
	Shadow,
	Copy,
};

class VulkanEngine {
public:

	VkPhysicalDeviceProperties _gpuProperties;
	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _chosenGPU;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapChain;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	FrameData _frames[FRAME_OVERLAP];
	GPUSceneData _sceneParameters;
	AllocatedBuffer _sceneParameterBuffer;

	std::vector<VkFramebuffer> _framebuffers;

	VkImageView _depthImageView;
	AllocatedImage _depthImage;

	VkFormat _depthFormat;

	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout _singleTextureSetLayout;
	VkDescriptorPool _descriptorPool;

	VkPipeline meshPipeline;

	std::vector<RenderObject> _renderables;
	std::unordered_map<std::string, Material> _materials;
	std::unordered_map<std::string, Mesh> _meshes;

	UploadContext	_uploadContext;

	std::unordered_map<std::string, Texture> _loadedTextures;

	VmaAllocator _allocator;

	DeletionQueue _mainDeletionQueue;

	int _selectedShader{ 0 };

	bool _isInitialized{ false };
	int _frameNumber {0};

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);

	bool load_shader_module_with_log(const char* filePath, VkShaderModule* outShaderModule, ShaderType shaderType);

	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	Material* get_material(const std::string& name);

	Mesh* get_mesh(const std::string& name);

	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

	FrameData& get_current_frame();

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	ShaderModule* GetShaderModule(const std::string& path);

	bool LoadPrefab(const char* path, glm::mat4 root);

	inline VkDevice device() const;
	inline vkutil::DescriptorAllocator* descriptorAllocator() const;
	inline vkutil::DescriptorLayoutCache* descriptorLayoutCache() const;
	inline vkutil::MaterialSystem* materialSystem() const;
	inline VkRenderPass renderPass(PassType t) const;
public:
	static std::string ShaderPath(std::string_view path);
	static std::string AssetPath(std::string_view path);
private:
	void init_vulkan();

	void init_swapchain();

	void init_commands();

	void InitForwardRenderpass();

	void InitCopyRenderpass();

	void InitShadowRenderpass();

	void init_framebuffers();

	void init_sync_structures();

	void init_descriptors();

	void init_pipelines();

	void init_scene();

	void init_imgui();

	VkPipeline build_pipeline(const char* vertexShader, const char* fragmentShader, VkPipelineLayout layout, VertexInputDescription* pInputDesc);

	void load_meshes();

	void load_images();

	void upload_mesh(Mesh& mesh);

	size_t pad_uniform_buffer_size(size_t originalSize);
private:
	void clear_vulkan();

	VkDevice m_Device;

	ShaderCache m_ShaderCache;

	std::array<VkRenderPass, 3> m_Passes;

	vkutil::DescriptorAllocator* m_DescriptorAllocator;
	vkutil::DescriptorLayoutCache* m_DescritptorLayoutCache;
	vkutil::MaterialSystem* m_MaterialSystem;
};

inline VkDevice VulkanEngine::device() const
{
	return m_Device;
}

inline vkutil::DescriptorAllocator* VulkanEngine::descriptorAllocator() const
{
	return m_DescriptorAllocator;
}

inline vkutil::DescriptorLayoutCache* VulkanEngine::descriptorLayoutCache() const
{
	return m_DescritptorLayoutCache;
}

inline vkutil::MaterialSystem* VulkanEngine::materialSystem() const
{
	return m_MaterialSystem;
}

inline VkRenderPass VulkanEngine::renderPass(PassType t) const
{
	return m_Passes[t];
}
