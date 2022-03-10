
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

	init_command();

	init_default_renderpass();

	init_framebuffers();

	init_sync_structures();

	init_pipelines();

	load_meshes();

	//everything went fine
	_isInitialized = true;
}
void VulkanEngine::cleanup()
{	
	if (_isInitialized) {
		vkWaitForFences(_device, 1, &_renderFence, true, TIMEOUT_1SEC);

		_mainDeletionQueue.flush();

		clear_vulkan();

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
{
	VK_CHECK(vkWaitForFences(_device, 1, &_renderFence, true, TIMEOUT_1SEC));
	VK_CHECK(vkResetFences(_device, 1, &_renderFence));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapChain, TIMEOUT_1SEC, _presentSemaphore, nullptr, &swapchainImageIndex));

	VK_CHECK(vkResetCommandBuffer(_mainCommandBuffer, 0));

	VkCommandBuffer cmd = _mainCommandBuffer;

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

	glm::vec3 camPos = { 0.f, 0.f, -2.f };
	glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
	glm::mat4 projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 200.f);
	glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(_frameNumber * 0.4f), glm::vec3(0, 1, 0));
	glm::mat4 mesh_matrix = projection * view * model;

	PipelineConstants constants;
	constants.render_matrix = mesh_matrix;

	vkCmdPushConstants(cmd, _trianglePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PipelineConstants), &constants);
	if (_selectedShader == 0)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
		vkCmdDraw(cmd, 3, 1, 0, 0);
	}
	else if(_selectedShader == 1)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _redTrianglePipeline);
		vkCmdDraw(cmd, 3, 1, 0, 0);
	}
	else
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &_monkeyMesh._vertexBuffer._buffer, &offset);
		vkCmdDraw(cmd, _monkeyMesh._vertices.size(), 1, 0, 0);
	}

	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &_presentSemaphore;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &_renderSemaphore;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_swapChain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &_renderSemaphore;
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

	vkb::PhysicalDeviceSelector selector(vkb_inst);
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 1)
		.set_surface(_surface)
		.select()
		.value();

	vkb::DeviceBuilder	deviceBuilder(physicalDevice);
	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	vmaCreateAllocator(&allocatorInfo, &_allocator);
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

void VulkanEngine::init_command()
{
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_commandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_commandPool, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_mainCommandBuffer));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _commandPool, nullptr);
		});
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
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;

	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_renderFence));
	_mainDeletionQueue.push_function([=]() {
		vkDestroyFence(_device, _renderFence, nullptr);
		});

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_presentSemaphore));
	_mainDeletionQueue.push_function([=]() {
		vkDestroySemaphore(_device, _presentSemaphore, nullptr);
		});

	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphore));
	_mainDeletionQueue.push_function([=]() {
		vkDestroySemaphore(_device, _renderSemaphore, nullptr);
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
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info(constantRanges);

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));
	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
		});

	_trianglePipeline = build_pipeline("../../shaders/triangle.vert.spv", "../../shaders/triangle.frag.spv", nullptr);
	_redTrianglePipeline = build_pipeline("../../shaders/red_triangle.vert.spv", "../../shaders/red_triangle.frag.spv", nullptr);
	auto desc = Vertex::get_vertex_description();
	_meshPipeline = build_pipeline("../../shaders/tri_mesh.vert.spv", "../../shaders/triangle.frag.spv", &desc);
}

VkPipeline VulkanEngine::build_pipeline(const char* vertexShader, const char* fragmentShader, VertexInputDescription* pInputDesc)
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

	pipelineBuilder._pipelineLayout = _trianglePipelineLayout;

	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipeline pipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

	vkDestroyShaderModule(_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_device, pipeline, nullptr);
		});
	return pipeline;
}

void VulkanEngine::load_meshes()
{
	_triangleMesh._vertices.resize(3);

	_triangleMesh._vertices[0].position = { 1.f, 1.f, 0.0f };
	_triangleMesh._vertices[1].position = { -1.f, 1.f, 0.0f };
	_triangleMesh._vertices[2].position = { 0.f, -1.f, 0.0f };

	_triangleMesh._vertices[0].color = { 0.f, 1.f, 0.0f };
	_triangleMesh._vertices[1].color = { 0.f, 1.f, 0.0f };
	_triangleMesh._vertices[2].color = { 0.f, 1.f, 0.0f };

	upload_mesh(_triangleMesh);
	
	_monkeyMesh.load_from_obj("../../assets/monkey_smooth.obj");
	upload_mesh(_monkeyMesh);
}

void VulkanEngine::upload_mesh(Mesh& mesh)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;

	bufferInfo.size = mesh._vertices.size() * sizeof(Vertex);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo,
		&mesh._vertexBuffer._buffer,
		&mesh._vertexBuffer._allocation,
		nullptr));

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});

	void* data;
	vmaMapMemory(_allocator, mesh._vertexBuffer._allocation, &data);
	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(_allocator, mesh._vertexBuffer._allocation);
}

void VulkanEngine::clear_vulkan()
{
	vmaDestroyAllocator(_allocator);
	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);
	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	vkDestroyInstance(_instance, nullptr);
}


