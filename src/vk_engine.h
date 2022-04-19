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
#include <vk_scene.h>
#include <material_system.h>
#include <vk_pushbuffer.h>
#include <player_camera.h>

#include <glm/glm.hpp>

namespace assets {
	struct PrefabInfo;
}

namespace vkutil
{
	class VulkanProfiler;
}

struct DirectionalLight {
	glm::vec3 lightPosition;
	glm::vec3 lightDirection;
	glm::vec3 shadowExtent;
	glm::mat4 GetProjection();
	glm::mat4 GetView();
};

struct UploadContext {
	VkFence uploadFence;
	VkCommandPool commandPool;
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
	glm::mat4 modelMatrix;
	glm::vec4 originRadius;
	glm::vec4 extents;
};

struct FrameData {
	VkSemaphore presentSemaphore, renderSemaphore;
	VkFence renderFence;

	DeletionQueue frameDeletionQueue;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	vkutil::PushBuffer dynamicData;

	AllocatedBufferUntyped debugOutputBuffer;

	vkutil::DescriptorAllocator* dynamicDescriptorAllocator;

	std::vector<uint32_t> debugDataOffsets;
	std::vector<std::string> debugDataNames;
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

	GPUSceneData _sceneParameters;

	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout m_SingleTextureSetLayout;
	VkDescriptorPool _descriptorPool;

	bool _isInitialized{ false };

	//initializes everything in the engine
	void Init();

	//shuts down the engine
	void Cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

	FrameData& GetCurrentFrame();

	AllocatedBufferUntyped CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags requiredFlag = 0);

	void DestroyBuffer(AllocatedBufferUntyped buffer);

	void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	ShaderModule* GetShaderModule(const std::string& path);

	bool LoadPrefab(const char* path, glm::mat4 root);

	void RefreshRenderBounds(MeshObject* object);

	inline VkDevice device() const;
	inline vkutil::DescriptorAllocator* descriptorAllocator() const;
	inline vkutil::DescriptorLayoutCache* descriptorLayoutCache() const;
	inline vkutil::MaterialSystem* materialSystem() const;
	inline VkRenderPass GetRenderPass(PassType t) const;
public:
	static std::string ShaderPath(std::string_view path);
	static std::string AssetPath(std::string_view path);
private:
	void InitVulkan();

	void InitSwapchain();

	void InitCommands();

	void InitForwardRenderpass();

	void InitCopyRenderpass();

	void InitShadowRenderpass();

	void InitFramebuffers();

	void InitSyncStructures();

	void InitDescriptors();

	void InitPipelines();

	void InitScene();

	void InitImgui();

	bool LoadComputeShader(const char* shaderPath, VkPipeline& pipeline, VkPipelineLayout& layout);

	void LoadMeshes();

	void LoadImages();

	bool LoadImageToCache(const char* name, const char* path);

	void UploadMesh(Mesh& mesh);

	size_t pad_uniform_buffer_size(size_t originalSize);

	Mesh* GetMesh(const std::string& name);

	bool LoadImageToCache(const std::string& name, const std::string& path);
private:
	void ClearVulkan();

	struct SDL_Window* m_Window{ nullptr };
	VkInstance m_Instance;
	VkPhysicalDevice m_ChosenGPU;
	VkDebugUtilsMessengerEXT m_DebugMessenger;
	VkSurfaceKHR m_Surface;
	VkDevice m_Device;
	VkPhysicalDeviceProperties m_GpuPropertices;

	VmaAllocator m_Allocator;
	DeletionQueue m_MainDeletionQueue;

	vkutil::VulkanProfiler* m_Profiler;

	VkQueue m_GraphicsQueue;
	uint32_t m_GraphicsQueueFamily;

	FrameData m_Frames[FRAME_OVERLAP];
	std::vector<VkFramebuffer> m_FrameBuffers;
	int m_FrameNumber{ 0 };

	VkSwapchainKHR m_SwapChain;
	VkFormat m_SwapchainImageFormat;
	std::vector<VkImage> m_SwapchainImages;
	std::vector<VkImageView> m_SwapchainImageViews;	

	VkExtent2D m_WindowExtent{ 1700 , 900 };
	VkExtent2D m_ShadowExtent{ 1024 * 4,1024 * 4 };
	VkFormat m_depthFormat;
	uint32_t m_DepthPyramidWidth, m_DepthPyramidHeight, m_DepthPyramidLevels;

	VkFormat m_RenderFormat;
	AllocatedImage m_RawRenderImage;

	AllocatedImage m_DepthImage;
	AllocatedImage m_DepthPyramidImage;
	VkImageView m_DepthPyramidMips[16] = {};

	AllocatedImage m_ShadowImage;

	VkSampler m_SmoothSampler;
	VkSampler m_DepthSampler;
	VkSampler m_ShadowSampler;

	VkFramebuffer m_ForwardFramebuffer;
	VkFramebuffer m_ShadowFramebuffer;

	UploadContext m_UploadContext;

	VkPipeline m_CullPipeline;
	VkPipelineLayout m_CullLayout;

	VkPipeline m_DepthReducePipeline;
	VkPipelineLayout m_DepthReduceLayout;

	VkPipeline m_SparseUploadPipeline;
	VkPipelineLayout m_SparseUploadLayout;

	VkPipeline m_BlitPipeline;
	VkPipelineLayout m_BlitLayout;
	
	ShaderCache m_ShaderCache;

	std::array<VkRenderPass, 3> m_Passes;

	vkutil::DescriptorAllocator* m_DescriptorAllocator;
	vkutil::DescriptorLayoutCache* m_DescritptorLayoutCache;
	vkutil::MaterialSystem* m_MaterialSystem;

	std::unordered_map<std::string, Mesh> m_Meshes;
	std::unordered_map<std::string, assets::PrefabInfo*> m_PrefabCache;
	std::unordered_map<std::string, Texture> m_LoadedTextures;

	RenderScene m_RenderScene;
	AllocatedBufferUntyped m_SceneParameterBuffer;

	PlayerCamera m_Camera;
	DirectionalLight m_MainLight;
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

inline VkRenderPass VulkanEngine::GetRenderPass(PassType t) const
{
	return m_Passes[t];
}
