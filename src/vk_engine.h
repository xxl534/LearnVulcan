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
	glm::mat4 sunlightShadowMatrix;
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

struct CullParams {
	glm::mat4 viewMat;
	glm::mat4 projMat;
	bool occlusionCull;
	bool frustrumCull;
	float drawDist;
	bool aabb;
	glm::vec3 aabbMin;
	glm::vec3 aabbMax;
};

struct EngineStats {
	float frametime;
	int objects;
	int drawcalls;
	int draws;
	int triangles;
};

struct DrawCullData
{
	glm::mat4 viewMat;
	float p00, p11, znear, zfar;// symmetric projection parameters
	float frustum[4];	// data for left/right/top/bottom frustum planes
	float lodBase, lodStep;	// lod distance i = base * pow(step, i)
	float pyramidWidth, pyramidHeight; // depth pyramid size in texels

	uint32_t drawCount;
	int cullingEnabled;
	int lodEnabled;
	int occlusionEnabled;

	int distanceCheck;
	int AABBCheck;
	float aabbMinX;
	float aabbMinY;

	float aabbMinZ;
	float aabbMaxX;
	float aabbMaxY;
	float aabbMaxZ;
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

	FrameData& GetCurrentFrame();

	AllocatedBufferUntyped CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags requiredFlag = 0);

	void DestroyBuffer(AllocatedBufferUntyped& buffer);

	void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	ShaderModule* GetShaderModule(const std::string& path);

	bool LoadPrefab(const char* path, glm::mat4 root);

	void RefreshRenderBounds(MeshObject* object);

	inline VkDevice device() const;
	inline vkutil::DescriptorAllocator* descriptorAllocator() const;
	inline vkutil::DescriptorLayoutCache* descriptorLayoutCache() const;
	inline vkutil::MaterialSystem* materialSystem() const;
	inline VkRenderPass GetRenderPass(PassType t) const;

	template<typename T>
	T* MapBuffer(AllocatedBuffer<T>& buffer);
	void* MapBuffer(AllocatedBufferUntyped& buffer);
	void UnmapBuffer(AllocatedBufferUntyped& buffer);

	AllocatedImage CreateImage(VkImageCreateInfo* createInfo, VmaAllocationCreateInfo* allocInfo, VkFormat format, VkImageAspectFlags aspectFlags, int mip = 1);
	void DestroyImage(AllocatedImage& image);
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

	void ReadyMeshDraw(VkCommandBuffer cmd);

	void ReadyCullData(RenderScene::MeshPass& pass, VkCommandBuffer cmd);

	void DrawObjectsForward(VkCommandBuffer cmd, RenderScene::MeshPass& pass);

	void DrawObjectsShadow(VkCommandBuffer cmd, RenderScene::MeshPass& pass);

	void ExecuteDrawCommands(VkCommandBuffer cmd, RenderScene::MeshPass& passs, VkDescriptorSet objectDataSet, std::vector<uint32_t> dynamicOffsets, VkDescriptorSet globalSet);

	void ForwardPass(VkClearValue clearValue, VkCommandBuffer cmd);

	void ShadowPass(VkCommandBuffer cmd);

	void CopyRenderToSwapchain(uint32_t swapchainImageIndex, VkCommandBuffer cmd);

	void ReduceDepth(VkCommandBuffer cmd);

	void ExecuteComputeCull(VkCommandBuffer cmd, RenderScene::MeshPass& pass, CullParams& params);

	void ReallocateBuffer(AllocatedBufferUntyped& buffer, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags requiredFlags = 0);
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
	EngineStats m_Stats;

	VkQueue m_GraphicsQueue;
	uint32_t m_GraphicsQueueFamily;
	tracy::VkCtx* m_GraphicQueueContext;

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
	GPUSceneData m_SceneParameters;
	AllocatedBufferUntyped m_SceneParameterBuffer;

	PlayerCamera m_Camera;
	DirectionalLight m_MainLight;

	std::vector<VkBufferMemoryBarrier> m_UploadBarriers;
	std::vector<VkBufferMemoryBarrier> m_CullReadyBarriers;
	std::vector<VkBufferMemoryBarrier> m_PostCullBarriers;
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

template<typename T>
inline T* VulkanEngine::MapBuffer(AllocatedBuffer<T>& buffer)
{
	void* data;
	vmaMapMemory(m_Allocator, buffer.allocation, &data);
	return (T*)data;
}
