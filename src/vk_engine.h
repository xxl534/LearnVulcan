﻿// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <deque>
#include <functional>
#include <unordered_map>

#include "vk_mem_alloc.h"
#include "vk_mesh.h"

#include <glm/glm.hpp>

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
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct RenderObject{
	Mesh* mesh;
	Material* material;
	glm::mat4 transformMatrix;
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

class VulkanEngine {
public:

	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _chosenGPU;
	VkDevice _device;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapChain;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkImageView _depthImageView;
	AllocatedImage _depthImage;

	VkFormat _depthFormat;

	VkPipeline meshPipeline;

	std::vector<RenderObject> _renderables;
	std::unordered_map<std::string, Material> _materials;
	std::unordered_map<std::string, Mesh> _meshes;

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
private:
	void init_vulkan();

	void init_swapchain();

	void init_command();

	void init_default_renderpass();

	void init_framebuffers();

	void init_sync_structures();

	void init_pipelines();

	void init_scene();

	VkPipeline build_pipeline(const char* vertexShader, const char* fragmentShader, VkPipelineLayout layout, VertexInputDescription* pInputDesc);

	void load_meshes();

	void upload_mesh(Mesh& mesh);
private:
	void clear_vulkan();
};