
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include <iostream>
#include <fstream>

//bootstrap library
#include "VkBootstrap.h"
#include "vk_pipeline_builder.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "vk_texture.h"

#include <glm/gtx/transform.hpp>

const char* ShaderTypeNames[3] = {
	"Fragment",
	"Vertex",
	"Compute"
};

#define VK_CHECK(x)				\
	do							\
	{							\
		VkResult err = x;		\
		if (err)				\
		{						\
			std::cout << "Detected Vulkan Error:" << err << std::endl;\
			abort();			\
		}						\
	}							\
	while(0)					\

#define TIMEOUT_1SEC 1000000000

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	
	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);
	
	init_vulkan();

	init_swapchain();

	init_commands();

	init_default_renderpass();

	init_framebuffers();

	init_sync_structures();

	init_descriptors();

	init_pipelines();

	load_images();

	load_meshes();

	init_scene();

	//everything went fine
	_isInitialized = true;
}
void VulkanEngine::cleanup()
{	
	if (_isInitialized) {
		for (int i = 0; i < FRAME_OVERLAP; ++i)
		{
			vkWaitForFences(_device, 1, &_frames[i]._renderFence, true, TIMEOUT_1SEC);
		}
		

		_mainDeletionQueue.flush();

		clear_vulkan();

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
{
	FrameData& currentFrame = get_current_frame();
	VK_CHECK(vkWaitForFences(_device, 1, &currentFrame._renderFence, true, TIMEOUT_1SEC));
	VK_CHECK(vkResetFences(_device, 1, &currentFrame._renderFence));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapChain, TIMEOUT_1SEC, currentFrame._presentSemaphore, nullptr, &swapchainImageIndex));

	VK_CHECK(vkResetCommandBuffer(currentFrame._mainCommandBuffer, 0));

	VkCommandBuffer cmd = currentFrame._mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBegineInfo = {};
	cmdBegineInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBegineInfo.pNext = nullptr;

	cmdBegineInfo.pInheritanceInfo = nullptr;
	cmdBegineInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBegineInfo));

	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { {0.0f, 0.0f, flash, 1.0f} };
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;
	VkClearValue clearValues[] = { clearValue, depthClear };

	VkRenderPassBeginInfo rpInfo = {};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpInfo.pNext = nullptr;

	rpInfo.renderPass = _renderPass;
	rpInfo.renderArea.offset.x = 0;
	rpInfo.renderArea.offset.y = 0;
	rpInfo.renderArea.extent = _windowExtent;
	rpInfo.framebuffer = _framebuffers[swapchainImageIndex];
	rpInfo.clearValueCount = 2;
	rpInfo.pClearValues = clearValues;

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	draw_objects(cmd, _renderables.data(), _renderables.size());

	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &currentFrame._presentSemaphore;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &currentFrame._renderSemaphore;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, currentFrame._renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_swapChain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &currentFrame._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &swapchainImageIndex;
	
	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	++_frameNumber;
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	//main loop
	while (!bQuit)
	{
		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			//close the window when user alt-f4s or clicks the X button			
			if (e.type == SDL_QUIT)
			{
				bQuit = true;
			}
			else if (e.type == SDL_KEYDOWN)
			{
				if (e.key.keysym.sym == SDLK_SPACE)
				{
					_selectedShader += 1;
					if (_selectedShader > 2)
					{
						_selectedShader = 0;
					}
				}
			}
		}

		draw();
	}
}

bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* outShaderModule)
{
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		return false;
	}

	size_t fileSize = (size_t)file.tellg();

	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}
	*outShaderModule = shaderModule;

	return true;
}

bool VulkanEngine::load_shader_module_with_log(const char* filePath, VkShaderModule* outShaderModule, ShaderType shaderType)
{
	if (load_shader_module(filePath, outShaderModule))
	{
		std::cout << "Triangle "<<ShaderTypeNames[shaderType]<<" shader successfully loaded" << std::endl;
		return true;
	}
	else {
		std::cout << "Error when building the triangle "<< ShaderTypeNames[shaderType] <<" shader module" << std::endl;
		return false;
	}
}

Material* VulkanEngine::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = layout;
	_materials[name] = mat;
	return &_materials[name];
}

Material* VulkanEngine::get_material(const std::string& name)
{
	auto it = _materials.find(name);
	if (it == _materials.end())
		return nullptr;
	else
		return &(*it).second;
}

Mesh* VulkanEngine::get_mesh(const std::string& name)
{
	auto it = _meshes.find(name);
	if (it == _meshes.end())
		return nullptr;
	else
		return &(*it).second;
}

void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject* first, int count)
{
	glm::vec3 camPos{ 0.f, -6.f, -10.f };
	glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
	glm::mat4 projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 200.f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);

	char* sceneData;
	_sceneParameters.ambientColor = { sin(_frameNumber / 120.f), 0, cos(_frameNumber / 120.f), 1 };
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);
	int frameIndex = _frameNumber % FRAME_OVERLAP;
	sceneData += pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;
	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));
	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;

	void* objectData;
	vmaMapMemory(_allocator, get_current_frame().objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;
	for (int i = 0; i < count; ++i)
	{
		RenderObject& object = first[i];
		objectSSBO[i].color = glm::vec4((i + 0.1f)/ 255.f);
		objectSSBO[i].modelMatrix = object.transformMatrix;
	}

	vmaUnmapMemory(_allocator, get_current_frame().objectBuffer._allocation);
	for (int i = 0; i < count; ++i)
	{
		RenderObject& obj = first[i];

		if (obj.material != lastMaterial)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.material->pipeline);
			lastMaterial = obj.material;

			uint32_t uniformOffset = pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.material->pipelineLayout, 0, 1, &get_current_frame().globalDescriptor, 1, &uniformOffset);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.material->pipelineLayout, 1, 1, &get_current_frame().objectDescriptor, 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.material->pipelineLayout, 2, 1, &obj.material->textureSet, 0, nullptr);
		}

		int k = 0;
		glm::mat4 mesh_matrix = obj.transformMatrix;

		PipelineConstants constants;
		constants.render_matrix = mesh_matrix;
		vkCmdPushConstants(cmd, obj.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PipelineConstants), &constants);

		if (obj.mesh != lastMesh)
		{
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &obj.mesh->_vertexBuffer._buffer, &offset);
			lastMesh = obj.mesh;
		}

		vkCmdDraw(cmd, obj.mesh->_vertices.size(), 1, 0, i);
	}
}

FrameData& VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.pNext = nullptr;

	info.size = allocSize;
	info.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo{};
	vmaallocInfo.usage = memoryUsage;

	AllocatedBuffer buffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &info, &vmaallocInfo, &buffer._buffer, &buffer._allocation, nullptr));

	return buffer;
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("Example Vulkan Application")
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	_instance = vkb_inst.instance;

	_debug_messenger = vkb_inst.debug_messenger;


	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);


	VkPhysicalDeviceShaderDrawParametersFeatures drawParamtersFeature;
	drawParamtersFeature.shaderDrawParameters = true;
	drawParamtersFeature.pNext = NULL;
	drawParamtersFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;

	vkb::PhysicalDeviceSelector selector(vkb_inst);
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 1)
		.add_required_extension_features(drawParamtersFeature)
		.set_surface(_surface)
		.select()
		.value();


	vkb::DeviceBuilder	deviceBuilder(physicalDevice);
	_chosenGPU = physicalDevice.physical_device;
	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	_gpuProperties = vkbDevice.physical_device.properties;
	std::cout << "The GPU has a minimum buffer alignment of " << _gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;
}

void VulkanEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder(_chosenGPU,_device,_surface);
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build().value();

	_swapChain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
	_swapchainImageFormat = vkbSwapchain.image_format;

	_mainDeletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(_device, _swapChain, nullptr);
		});

	_mainDeletionQueue.push_function([=]() {
		for (int i = 0; i < _swapchainImageViews.size(); ++i)
		{
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
		}
		});

	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_depthFormat = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo depthImageInfo = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

	VmaAllocationCreateInfo depthAllocationInfo{};
	depthAllocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	depthAllocationInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &depthImageInfo, &depthAllocationInfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	VkImageViewCreateInfo depthViewInfo = vkinit::imageview_create_info(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);
	VK_CHECK(vkCreateImageView(_device, &depthViewInfo, nullptr, &_depthImageView));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, _depthImageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage._image, _depthImage._allocation);
		});
}

void VulkanEngine::init_commands()
{
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
			});
	}
	
	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily);

	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext.commandPool));
	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _uploadContext.commandPool, nullptr);
		});

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext.commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_uploadContext.commandBuffer));
}

void VulkanEngine::init_default_renderpass()
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = _swapchainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.flags = 0;
	depthAttachment.format = _depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.pNext = nullptr;

	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = &attachments[0];
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	VkSubpassDependency colorDependency{};
	colorDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	colorDependency.dstSubpass = 0;
	colorDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	colorDependency.srcAccessMask = 0;
	colorDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	colorDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depthDependency{};
	depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depthDependency.dstSubpass = 0;
	depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.srcAccessMask = 0;
	depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency dependencies[2] = { colorDependency, depthDependency };
	renderPassInfo.dependencyCount = 2;
	renderPassInfo.pDependencies = &dependencies[0];


	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_renderPass));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(_device, _renderPass, nullptr);
		});
}

void VulkanEngine::init_framebuffers()
{
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.pNext = nullptr;

	fbInfo.renderPass = _renderPass;
	fbInfo.attachmentCount = 2;
	fbInfo.width = _windowExtent.width;
	fbInfo.height = _windowExtent.height;
	fbInfo.layers = 1;

	const uint32_t swapchainImageCount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchainImageCount);

	for (int i = 0; i < swapchainImageCount; ++i)
	{
		VkImageView attachments[2];
		attachments[0] = _swapchainImageViews[i];
		attachments[1] = _depthImageView;
		fbInfo.pAttachments = attachments;

		VK_CHECK(vkCreateFramebuffer(_device, &fbInfo, nullptr, &_framebuffers[i]));
	}


	_mainDeletionQueue.push_function([=]() {
		for (int i = 0; i < _framebuffers.size(); ++i)
		{
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
		}
		});
}

void VulkanEngine::init_sync_structures()
{
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info();

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
		_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
			});

		VkSemaphoreCreateInfo semaphoreCreateInfo = {};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		semaphoreCreateInfo.pNext = nullptr;
		semaphoreCreateInfo.flags = 0;

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
		_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
			});
	}

	VkFenceCreateInfo uploadFenceCreateInfo = vkinit::fence_create_info();
	VK_CHECK(vkCreateFence(_device, &uploadFenceCreateInfo, nullptr, &_uploadContext.uploadFence));
	vkResetFences(_device, 1, &_uploadContext.uploadFence);
	_mainDeletionQueue.push_function([=] {
		vkDestroyFence(_device, _uploadContext.uploadFence, nullptr);
		});

}

void VulkanEngine::init_descriptors()
{
	std::vector<VkDescriptorPoolSize> sizes = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
	};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.pNext = nullptr;
	poolInfo.maxSets = 10;
	poolInfo.poolSizeCount = (uint32_t)sizes.size();
	poolInfo.pPoolSizes = sizes.data();
	vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool);

	{
		VkDescriptorSetLayoutBinding camBufferBinding = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
		VkDescriptorSetLayoutBinding sceneBinding = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);
		VkDescriptorSetLayoutBinding bindings[] = { camBufferBinding, sceneBinding };

		VkDescriptorSetLayoutCreateInfo setInfo{};
		setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setInfo.pNext = nullptr;

		setInfo.bindingCount = 2;
		setInfo.flags = 0;
		setInfo.pBindings = bindings;

		vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_globalSetLayout);
	}
	{
		VkDescriptorSetLayoutBinding objectBinding = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
		VkDescriptorSetLayoutCreateInfo set2Info{};
		set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		set2Info.pNext = nullptr;

		set2Info.bindingCount = 1;
		set2Info.flags = 0;
		set2Info.pBindings = &objectBinding;

		vkCreateDescriptorSetLayout(_device, &set2Info, nullptr, &_objectSetLayout);
	}
	{
		VkDescriptorSetLayoutBinding textureBinding = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutCreateInfo.pNext = nullptr;

		layoutCreateInfo.bindingCount = 1;
		layoutCreateInfo.flags = 0;
		layoutCreateInfo.pBindings = &textureBinding;
		vkCreateDescriptorSetLayout(_device, &layoutCreateInfo, nullptr, &_singleTextureSetLayout);
	}
	

	const size_t sceneParamBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(GPUSceneData));
	_sceneParameterBuffer = create_buffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		_frames[i].cameraBuffer = create_buffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		const int MAX_OBJECTS = 10000;
		_frames[i].objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		
		{
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.pNext = nullptr;

			allocInfo.descriptorPool = _descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &_globalSetLayout;

			vkAllocateDescriptorSets(_device, &allocInfo, &_frames[i].globalDescriptor);

			VkDescriptorSetAllocateInfo objAllocInfo{};
			objAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			objAllocInfo.pNext = nullptr;

			objAllocInfo.descriptorPool = _descriptorPool;
			objAllocInfo.descriptorSetCount = 1;
			objAllocInfo.pSetLayouts = &_objectSetLayout;

			vkAllocateDescriptorSets(_device, &objAllocInfo, &_frames[i].objectDescriptor);
			

			VkDescriptorBufferInfo cameraInfo{};
			cameraInfo.buffer = _frames[i].cameraBuffer._buffer;
			cameraInfo.offset = 0;
			cameraInfo.range = sizeof(GPUCameraData);

			VkDescriptorBufferInfo sceneInfo{};
			sceneInfo.buffer = _sceneParameterBuffer._buffer;
			sceneInfo.offset = 0;
			sceneInfo.range = sizeof(GPUSceneData);

			VkDescriptorBufferInfo objectInfo{};
			objectInfo.buffer = _frames[i].objectBuffer._buffer;
			objectInfo.offset = 0;
			objectInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

			VkWriteDescriptorSet cameraWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i].globalDescriptor, &cameraInfo, 0);
			VkWriteDescriptorSet sceneWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, _frames[i].globalDescriptor, &sceneInfo, 1);
			VkWriteDescriptorSet objectWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frames[i].objectDescriptor, &objectInfo, 0);
			VkWriteDescriptorSet setWrites[] = { cameraWrite, sceneWrite, objectWrite };
			vkUpdateDescriptorSets(_device, 3, setWrites, 0, nullptr);
		}

		
	}

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		_mainDeletionQueue.push_function([&]() {
			vmaDestroyBuffer(_allocator, _frames[i].cameraBuffer._buffer, _frames[i].cameraBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].objectBuffer._buffer, _frames[i].objectBuffer._allocation);
			});
	}

	_mainDeletionQueue.push_function([&]() {
		vkDestroyDescriptorSetLayout(_device, _globalSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _objectSetLayout, nullptr);
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
		});

	_mainDeletionQueue.push_function([&]() {
		vmaDestroyBuffer(_allocator, _sceneParameterBuffer._buffer, _sceneParameterBuffer._allocation);
		});
}

void VulkanEngine::init_pipelines()
{
	VkPushConstantRange push_constant;
	push_constant.offset = 0;
	push_constant.size = sizeof(PipelineConstants);
	push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	std::vector<VkPushConstantRange> constantRanges{};
	constantRanges.push_back(push_constant);
	std::vector<VkDescriptorSetLayout> setLayouts{};
	setLayouts.push_back(_globalSetLayout);
	setLayouts.push_back(_objectSetLayout);
	setLayouts.push_back(_singleTextureSetLayout);
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info(constantRanges, setLayouts);

	VkPipelineLayout defaultPipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &defaultPipelineLayout));
	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipelineLayout(_device, defaultPipelineLayout, nullptr);
		});

	auto desc = Vertex::get_vertex_description();
	VkPipeline meshPipeline = build_pipeline("../../shaders/tri_mesh.vert.spv", "../../shaders/triangle.frag.spv", defaultPipelineLayout, &desc);

	create_material(meshPipeline, defaultPipelineLayout, "defaultmesh");

	VkPipeline texturedPipeline = build_pipeline("../../shaders/tri_mesh.vert.spv", "../../shaders/textured_lit.frag.spv", defaultPipelineLayout, &desc);

	create_material(texturedPipeline, defaultPipelineLayout, "texturedMesh");
}

void VulkanEngine::init_scene()
{
	/*float height = 5.f;
	RenderObject monkey;
	monkey.mesh = get_mesh("monkey");
	monkey.material = get_material("defaultmesh");
	monkey.transformMatrix = glm::translate(glm::mat4{ 1.0f }, glm::vec3(0, height, 0)) * glm::scale(glm::mat4{ 1.0 }, glm::vec3(2));

	_renderables.push_back(monkey);

	for (int x = -20; x <= 20; ++x)
	{
		for (int y = -20; y <= 20; ++y)
		{
			RenderObject tri;
			tri.mesh = get_mesh("triangle");
			tri.material = get_material("defaultmesh");
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0f }, glm::vec3(x, height, y));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2));
			tri.transformMatrix = translation * scale;
			_renderables.push_back(tri);
		}
	}*/

	RenderObject map;
	map.mesh = get_mesh("empire");
	map.material = get_material("texturedMesh");
	map.transformMatrix = glm::translate(glm::vec3(5, -10, 0));

	_renderables.push_back(map);

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
	VkSampler blockySampler;
	vkCreateSampler(_device, &samplerInfo, nullptr, &blockySampler);

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_singleTextureSetLayout;
	
	vkAllocateDescriptorSets(_device, &allocInfo, &map.material->textureSet);

	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = blockySampler;
	imageBufferInfo.imageView = _loadedTextures["empire_diffuse"].imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = vkinit::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, map.material->textureSet, &imageBufferInfo, 0);
	vkUpdateDescriptorSets(_device, 1, &texture1, 0, nullptr);
}

VkPipeline VulkanEngine::build_pipeline(const char* vertexShader, const char* fragmentShader, VkPipelineLayout layout,  VertexInputDescription* pInputDesc)
{
	VkShaderModule triangleFragShader;
	load_shader_module_with_log(fragmentShader, &triangleFragShader, ShaderType_Fragment);

	VkShaderModule triangleVertexShader;
	load_shader_module_with_log(vertexShader, &triangleVertexShader, ShaderType_Vertex);

	PipelineBuilder pipelineBuilder;
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, triangleVertexShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info(pInputDesc);
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.f;
	pipelineBuilder._viewport.y = 0.f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0,0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	pipelineBuilder._pipelineLayout = layout;

	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipeline pipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

	vkDestroyShaderModule(_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_device, pipeline, nullptr);
		});
	return pipeline;
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VkCommandBuffer cmd = _uploadContext.commandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _uploadContext.uploadFence));

	vkWaitForFences(_device, 1, &_uploadContext.uploadFence, true, 9999999999);
	vkResetFences(_device, 1, &_uploadContext.uploadFence);

	vkResetCommandPool(_device, _uploadContext.commandPool, 0);
}

void VulkanEngine::load_meshes()
{
	Mesh triangleMesh;
	triangleMesh._vertices.resize(3);

	triangleMesh._vertices[0].position = { 1.f, 1.f, 0.0f };
	triangleMesh._vertices[1].position = { -1.f, 1.f, 0.0f };
	triangleMesh._vertices[2].position = { 0.f, -1.f, 0.0f };

	triangleMesh._vertices[0].color = { 0.f, 1.f, 0.0f };
	triangleMesh._vertices[1].color = { 0.f, 1.f, 0.0f };
	triangleMesh._vertices[2].color = { 0.f, 1.f, 0.0f };

	upload_mesh(triangleMesh);
	
	Mesh monkeyMesh;
	monkeyMesh.load_from_obj("../../assets/monkey_smooth.obj");
	upload_mesh(monkeyMesh);

	_meshes["monkey"] = monkeyMesh;
	_meshes["triangle"] = triangleMesh;

	Mesh lostEmpire{};
	lostEmpire.load_from_obj("../../assets/lost_empire.obj");
	upload_mesh(lostEmpire);
	_meshes["empire"] = lostEmpire;
}

void VulkanEngine::load_images()
{
	Texture tex;
	vkutil::load_image_from_file(*this, "../../assets/lost_empire-RGBA.png", tex.image);
	VkImageViewCreateInfo imageCreateInfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_SRGB, tex.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(_device, &imageCreateInfo, nullptr, &tex.imageView);

	_loadedTextures["empire_diffuse"] = tex;
}

void VulkanEngine::upload_mesh(Mesh& mesh)
{
	const size_t bufferSize = mesh._vertices.size() * sizeof(Vertex);
	VkBufferCreateInfo stagingBufferInfo{};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.pNext = nullptr;

	stagingBufferInfo.size = bufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &stagingBufferInfo, &vmaAllocInfo,
		&stagingBuffer._buffer,
		&stagingBuffer._allocation,
		nullptr));


	void* data;
	vmaMapMemory(_allocator, stagingBuffer._allocation, &data);
	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(_allocator, stagingBuffer._allocation);

	VkBufferCreateInfo vertexBufferInfo{};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;

	vertexBufferInfo.size = bufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	VK_CHECK(vmaCreateBuffer(_allocator, &vertexBufferInfo, &vmaAllocInfo, &mesh._vertexBuffer._buffer, &mesh._vertexBuffer._allocation, nullptr));

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize;
		vkCmdCopyBuffer(cmd, stagingBuffer._buffer, mesh._vertexBuffer._buffer, 1, &copy);
		});


	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer._buffer, stagingBuffer._allocation);
}

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize)
{
	size_t minAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignSize = originalSize;
	if (minAlignment > 0)
	{
		alignSize = (alignSize + minAlignment - 1) & ~(minAlignment - 1);
	}
	return alignSize;
}

void VulkanEngine::clear_vulkan()
{
	vmaDestroyAllocator(_allocator);
	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);
	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	vkDestroyInstance(_instance, nullptr);
}


