
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_profiler.h>

#include <iostream>
#include <fstream>

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_vulkan.h"

#include <cvar.h>
#include <Tracy.hpp>
#include <TracyVulkan.hpp>

#include <prefab_asset.h>
#include <material_asset.h>
//bootstrap library
#include "VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <vk_texture.h>
#include <glm/gtx/transform.hpp>
#include <fmt/os.h>

AutoCVar_Int CVAR_OcclusionCullGPU("culling.enableOcclusionGPU", "Perform occlusion culling in gpu", 1, CVarFlags::EditCheckbox);


AutoCVar_Int CVAR_CamLock("camera.lock", "Locks the camera", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_OutputIndirectToFile("culling.outputIndirectBufferToFile", "output the indirect data to a file. Autoresets", 0, CVarFlags::EditCheckbox);

AutoCVar_Float CVAR_DrawDistance("gpu.drawDistance", "Distance cull", 5000);

AutoCVar_Int CVAR_FreezeShadows("gpu.freezeShadows", "Stop the rendering of shadows", 0, CVarFlags::EditCheckbox);

constexpr bool bUseValidationLayers = true;

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

void VulkanEngine::Init()
{
	ZoneScopedN("Engine Init");

	LogHandler::Get().set_time();

	LOG_INFO("Engine Init");
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);
	LOG_SUCCESS("SDL inited");

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	
	m_Window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		m_WindowExtent.width,
		m_WindowExtent.height,
		window_flags
	);

	InitVulkan();

	m_Profiler = new vkutil::VulkanProfiler();
	m_Profiler->init(m_Device, m_GpuPropertices.limits.timestampPeriod);

	m_ShaderCache.Init(m_Device);


	InitSwapchain();

	InitCommands();

	InitForwardRenderpass();
	InitCopyRenderpass();
	InitShadowRenderpass();

	InitFramebuffers();

	InitSyncStructures();

	InitDescriptors();

	InitPipelines();

	LoadImages();

	LoadMeshes();

	InitScene();

	InitImgui();


	m_Camera = {};
	m_Camera.position = { 0.f, 6.f, 5.f };

	m_MainLight.lightPosition = { 0,0,0 };
	m_MainLight.lightDirection = glm::vec3(0.3, -1, 0.3);
	m_MainLight.shadowExtent = { 100 ,100 ,100 };

	//everything went fine
	_isInitialized = true;
}
void VulkanEngine::Cleanup()
{	
	if (_isInitialized) {
		for (int i = 0; i < FRAME_OVERLAP; ++i)
		{
			vkWaitForFences(m_Device, 1, &m_Frames[i].renderFence, true, TIMEOUT_1SEC);
		}
		
		m_MainDeletionQueue.flush();

		for (auto& frame : m_Frames)
		{
			frame.dynamicDescriptorAllocator->Cleanup();
		}

		m_MaterialSystem->Cleanup();
		//descriptor
		m_DescriptorAllocator->Cleanup();
		delete m_DescriptorAllocator;
		m_DescriptorAllocator = nullptr;

		m_DescritptorLayoutCache->Cleanup();
		delete m_DescritptorLayoutCache;
		m_DescritptorLayoutCache = nullptr;

		ClearVulkan();

		SDL_DestroyWindow(m_Window);
	}
}

void VulkanEngine::draw()
{
	ZoneScopedN("Engine Draw");
	ImGui::Render();

	FrameData& currentFrame = GetCurrentFrame();
	{
		ZoneScopedN("Fence wait");
		VK_CHECK(vkWaitForFences(m_Device, 1, &currentFrame.renderFence, true, TIMEOUT_1SEC));
		VK_CHECK(vkResetFences(m_Device, 1, &currentFrame.renderFence));

		currentFrame.dynamicData.Reset();

		m_RenderScene.BuildBatches();

		void* data;
		vmaMapMemory(m_Allocator, currentFrame.debugOutputBuffer.allocation, &data);
		for (int i = 1; i < currentFrame.debugDataNames.size(); ++i)
		{
			uint32_t begin = currentFrame.debugDataOffsets[i - 1];
			uint32_t end = currentFrame.debugDataOffsets[i];

			auto name = currentFrame.debugDataNames[i];
			if (name.compare("Cull Indirect Output") == 0)
			{
				void* buffer = malloc(end - begin);
				memcpy(buffer, (uint8_t*)data + begin, end - begin);

				GPUIndirectObject* objects = (GPUIndirectObject*)buffer;
				int objectCount = (end - begin) / sizeof(GPUIndirectObject);

				std::string filename = fmt::format("{}_CULLDATA_{}.text", m_FrameNumber, i);
				auto out = fmt::output_file(filename);

				for (int o = 0; o < objectCount; ++o)
				{
					out.print(" Draw:{}-------------\n", o);
					out.print(" Object Graphics Count:{}\n", m_RenderScene.m_Passes[MeshpassType::Forward].indirectBatches[o].count);
					out.print(" Visible Count:{}\n", objects[o].command.instanceCount);
					out.print(" First: {}\n", objects[o].command.firstInstance);
					out.print(" Indices: {}\n", objects[o].command.indexCount);
				}

				free(buffer);
			}
		}
		vmaUnmapMemory(m_Allocator, currentFrame.debugOutputBuffer.allocation);
		currentFrame.debugDataNames.clear();
		currentFrame.debugDataOffsets.clear();

		currentFrame.debugDataNames.push_back("");
		currentFrame.debugDataOffsets.push_back(0);
	}
	currentFrame.frameDeletionQueue.flush();
	currentFrame.dynamicDescriptorAllocator->ResetPools();


	uint32_t swapchainImageIndex;
	{
		ZoneScopedN("Aquire Image");
		VK_CHECK(vkAcquireNextImageKHR(m_Device, m_SwapChain, TIMEOUT_1SEC, currentFrame.presentSemaphore, nullptr, &swapchainImageIndex));
	}
	

	VK_CHECK(vkResetCommandBuffer(currentFrame.mainCommandBuffer, 0));

	VkCommandBuffer cmd = currentFrame.mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBegineInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBegineInfo));

	VkClearValue clearValue;
	clearValue.color = { {0.1f, 0.1f, 0.1f, 1.0f} };

	m_Profiler->grab_queries(cmd);
	{
		m_PostCullBarriers.clear();
		m_CullReadyBarriers.clear();

		TracyVkZone(m_GraphicQueueContext, currentFrame.mainCommandBuffer, "All Frame");

		vkutil::VulkanScopeTimer timerAllFrame(cmd, m_Profiler, "All Frame");
		{
			vkutil::VulkanScopeTimer timerReadyFrame(cmd, m_Profiler, "Ready Frame");

			ReadyMeshDraw(cmd);

			ReadyCullData(m_RenderScene.GetMeshPass(MeshpassType::Forward), cmd);
			ReadyCullData(m_RenderScene.GetMeshPass(MeshpassType::Transparency), cmd);
			ReadyCullData(m_RenderScene.GetMeshPass(MeshpassType::DirectionalShadow), cmd);

			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, (uint32_t)m_CullReadyBarriers.size(), m_CullReadyBarriers.data(), 0, nullptr);
		}
	}

	CullParams forwardCull;
	forwardCull.projMat = m_Camera.get_projection_matrix(true);
	forwardCull.viewMat = m_Camera.get_view_matrix();
	forwardCull.frustrumCull = true;
	forwardCull.occlusionCull = true;
	forwardCull.drawDist = (float)CVAR_DrawDistance.Get();
	forwardCull.aabb = false;

	ExecuteComputeCull(cmd, m_RenderScene.GetMeshPass(MeshpassType::Forward), forwardCull);
	ExecuteComputeCull(cmd, m_RenderScene.GetMeshPass(MeshpassType::Transparency), forwardCull);

	CullParams shadowCull;
	shadowCull.projMat = m_MainLight.GetProjection();
	shadowCull.viewMat = m_MainLight.GetView();
	shadowCull.frustrumCull = true;
	shadowCull.occlusionCull = false;
	shadowCull.drawDist = 999999;
	shadowCull.aabb = true;

	glm::vec3 aabbcenter = m_MainLight.lightPosition;
	glm::vec3 aabbExtent = m_MainLight.shadowExtent * 1.5f;
	shadowCull.aabbMax = aabbcenter + aabbExtent;
	shadowCull.aabbMin = aabbcenter - aabbExtent;
	{
		vkutil::VulkanScopeTimer timerShadowCull(cmd, m_Profiler, "Shadow Cull");
		if (*CVarSystem::Get()->GetIntCVar("gpu.shadowcast"))
		{
			ExecuteComputeCull(cmd, m_RenderScene.GetMeshPass(MeshpassType::DirectionalShadow), shadowCull);
		}
	}

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, (uint32_t)m_PostCullBarriers.size(), m_PostCullBarriers.data(), 0, nullptr);

	m_Stats.drawcalls = 0;
	m_Stats.draws = 0;
	m_Stats.objects = 0;
	m_Stats.triangles = 0;

	ShadowPass(cmd);
	ForwardPass(clearValue, cmd);
	ReduceDepth(cmd);
	CopyRenderToSwapchain(swapchainImageIndex, cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));
	VkSubmitInfo submit = vkinit::submit_info(&cmd);
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &currentFrame.presentSemaphore;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &currentFrame.renderSemaphore;
	{
		ZoneScopedN("Queue Submit");
		VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submit, currentFrame.renderFence));
	}

	VkPresentInfoKHR presentInfo = vkinit::present_info();

	presentInfo.pSwapchains = &m_SwapChain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &swapchainImageIndex;

	{
		ZoneScopedN("Queue Present");
		VK_CHECK(vkQueuePresentKHR(m_GraphicsQueue, &presentInfo));
	}

	++m_FrameNumber;
}


void VulkanEngine::ForwardPass(VkClearValue clearValue, VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, m_Profiler, "Forward Pass");
	vkutil::VulkanPipelineStatRecorder recorder(cmd, m_Profiler, "Forward Primitives");

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 0.f;

	VkClearValue clearValues[] = { clearValue, depthClear };
	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(m_Passes[PassType::Forward], m_WindowExtent, 2, clearValues, m_ForwardFramebuffer);

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = (float)m_WindowExtent.width;
	viewport.height = (float)m_WindowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0,0 };
	scissor.extent = m_WindowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetDepthBias(cmd, 0, 0, 0);

	{
		TracyVkZone(m_GraphicQueueContext, GetCurrentFrame().mainCommandBuffer, "Forward Pass");
		DrawObjectsForward(cmd, m_RenderScene.GetMeshPass(MeshpassType::Forward));
		DrawObjectsForward(cmd, m_RenderScene.GetMeshPass(MeshpassType::Transparency));
	}
	{
		TracyVkZone(m_GraphicQueueContext, GetCurrentFrame().mainCommandBuffer, "Imgui Draw");
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}
	vkCmdEndRenderPass(cmd);
}

void VulkanEngine::ShadowPass(VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, m_Profiler, "Shadow Pass");
	vkutil::VulkanPipelineStatRecorder recorder(cmd, m_Profiler, "Shadow Primitives");

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(m_Passes[PassType::Shadow], m_ShadowExtent, 1, &depthClear, m_ShadowFramebuffer);

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = (float)m_ShadowExtent.width;
	viewport.height = (float)m_ShadowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0,0 };
	scissor.extent = m_ShadowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	if(m_RenderScene.GetMeshPass(MeshpassType::DirectionalShadow).indirectBatches.size() > 0)
	{
		TracyVkZone(m_GraphicQueueContext, GetCurrentFrame().mainCommandBuffer, "Shadow Pass");
		DrawObjectsShadow(cmd, m_RenderScene.GetMeshPass(MeshpassType::DirectionalShadow));
	}
	vkCmdEndRenderPass(cmd);
}

void VulkanEngine::CopyRenderToSwapchain(uint32_t swapchainImageIndex, VkCommandBuffer cmd)
{
	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(m_Passes[PassType::Copy], m_WindowExtent, 0, nullptr, m_FrameBuffers[swapchainImageIndex]);

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = (float)m_WindowExtent.width;
	viewport.height = (float)m_WindowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0,0 };
	scissor.extent = m_WindowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetDepthBias(cmd, 0, 0, 0);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlitPipeline);

	VkDescriptorImageInfo sourceImage;
	sourceImage.sampler = m_SmoothSampler;
	sourceImage.imageView = m_RawRenderImage.defaultView;
	sourceImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorSet blitSet;
	vkutil::DescriptorBuilder::Begin(m_DescritptorLayoutCache, GetCurrentFrame().dynamicDescriptorAllocator)
		.BindImage(0, &sourceImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.Build(blitSet);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlitLayout, 0, 1, &blitSet, 0, nullptr);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	// Using time point and system_clock 
	std::chrono::time_point<std::chrono::system_clock> start, end;

	start = std::chrono::system_clock::now();
	end = std::chrono::system_clock::now();
	//main loop
	while (!bQuit)
	{
		ZoneScopedN("Main Loop");
		end = std::chrono::system_clock::now();
		std::chrono::duration<float> elapsed_seconds = end - start;
		m_Stats.frametime = elapsed_seconds.count() * 1000.f;

		start = std::chrono::system_clock::now();
		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			ImGui_ImplSDL2_ProcessEvent(&e);
			m_Camera.process_input_event(&e);
			//close the window when user alt-f4s or clicks the X button			
			if (e.type == SDL_QUIT)
			{
				bQuit = true;
			}
			else if (e.type == SDL_KEYDOWN)
			{
				if (e.key.keysym.sym == SDLK_TAB)
				{
					if (CVAR_CamLock.Get())
					{
						LOG_INFO("Mouselook disabled");
						CVAR_CamLock.Set(false);
					}
					else {
						LOG_INFO("Mouselook enabled");
						CVAR_CamLock.Set(true);
					}
				}
			}
		}

		{
			ZoneScopedNC("Imgui Logic", tracy::Color::Grey);

			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplSDL2_NewFrame(m_Window);

			ImGui::NewFrame();

			if (ImGui::BeginMainMenuBar())
			{

				if (ImGui::BeginMenu("Debug"))
				{
					if (ImGui::BeginMenu("CVAR"))
					{
						CVarSystem::Get()->DrawImguiEditor();
						ImGui::EndMenu();
					}
					ImGui::EndMenu();
				}
				ImGui::EndMainMenuBar();
			}

			ImGui::Begin("engine");

			ImGui::Text("Frametimes: %f", m_Stats.frametime);
			ImGui::Text("Objects: %d", m_Stats.objects);
			//ImGui::Text("Drawcalls: %d", m_Stats.drawcalls);
			ImGui::Text("Batches: %d", m_Stats.draws);
			//ImGui::Text("Triangles: %d", m_Stats.triangles);		

			CVAR_OutputIndirectToFile.Set(false);
			if (ImGui::Button("Output Indirect"))
			{
				CVAR_OutputIndirectToFile.Set(true);
			}


			ImGui::Separator();

			for (auto& [k, v] : m_Profiler->timing)
			{
				ImGui::Text("TIME %s %f ms", k.c_str(), v);
			}
			for (auto& [k, v] : m_Profiler->stats)
			{
				ImGui::Text("STAT %s %d", k.c_str(), v);
			}


			ImGui::End();
		}
		

		{
			ZoneScopedNC("Flag Objects", tracy::Color::Blue);
			//test flagging some objects for changes

			int N_changes = 1000;
			for (int i = 0; i < N_changes; i++)
			{
				int rng = rand() % m_RenderScene.renderables.size();

				Handle<RenderObject> h;
				h.handle = rng;
				m_RenderScene.UpdateObject(h);
			}
			m_Camera.bLocked = CVAR_CamLock.Get();

			m_Camera.update_camera(m_Stats.frametime);

			m_MainLight.lightPosition = m_Camera.position;
		}

		draw();
	}
}

FrameData& VulkanEngine::GetCurrentFrame()
{
	return m_Frames[m_FrameNumber % FRAME_OVERLAP];
}

AllocatedBufferUntyped VulkanEngine::CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags requiredFlag)
{
	VkBufferCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.pNext = nullptr;
	info.size = allocSize;
	info.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo{};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.requiredFlags = requiredFlag;

	AllocatedBufferUntyped buffer;

	VK_CHECK(vmaCreateBuffer(m_Allocator, &info, &vmaallocInfo, &buffer.buffer, &buffer.allocation, nullptr));

	return buffer;
}

void VulkanEngine::DestroyBuffer(AllocatedBufferUntyped buffer)
{
	vmaDestroyBuffer(m_Allocator, buffer.buffer, buffer.allocation);
}

void VulkanEngine::InitVulkan()
{
	vkb::InstanceBuilder builder;

	auto builtInst = builder.set_app_name("Example Vulkan Application")
		.request_validation_layers(bUseValidationLayers)
		.use_default_debug_messenger()
		.build();

	LOG_SUCCESS("Vulkan Instance initialized");

	vkb::Instance vkbInst = builtInst.value();

	m_Instance = vkbInst.instance;

	m_DebugMessenger = vkbInst.debug_messenger;


	SDL_Vulkan_CreateSurface(m_Window, m_Instance, &m_Surface);

	LOG_SUCCESS("SDL Surface initialized");

	VkPhysicalDeviceShaderDrawParametersFeatures drawParamtersFeature;
	drawParamtersFeature.shaderDrawParameters = true;
	drawParamtersFeature.pNext = NULL;
	drawParamtersFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;

	VkPhysicalDeviceFeatures features{};
	features.pipelineStatisticsQuery = true;
	features.multiDrawIndirect = true;
	features.drawIndirectFirstInstance = true;
	features.samplerAnisotropy = true;

	vkb::PhysicalDeviceSelector selector(vkbInst);
	vkb::PhysicalDevice physicalDevice = selector
		.set_required_features(features)
		.set_minimum_version(1, 1)
		.add_required_extension(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME)
		.add_required_extension_features(drawParamtersFeature)
		.set_surface(m_Surface)
		.select()
		.value();

	LOG_SUCCESS("GPU found");

	m_ChosenGPU = physicalDevice.physical_device;

	vkb::DeviceBuilder	deviceBuilder(physicalDevice);
	vkb::Device vkbDevice = deviceBuilder.build().value();

	m_Device = vkbDevice.device;

	m_GraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	m_GraphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = m_ChosenGPU;
	allocatorInfo.device = m_Device;
	allocatorInfo.instance = m_Instance;
	vmaCreateAllocator(&allocatorInfo, &m_Allocator);

	vkGetPhysicalDeviceProperties(m_ChosenGPU, &m_GpuPropertices);
	LOG_INFO("The GPU has a minimum buffer alignment of {}", m_GpuPropertices.limits.minUniformBufferOffsetAlignment);
}

uint32_t PreviousPow2(uint32_t x)
{
	if (x == 0)
	{
		return 0;
	}

	x = x | (x >> 1);
	x = x | (x >> 2);
	x = x | (x >> 4);
	x = x | (x >> 8);
	x = x | (x >> 16);
	return x - (x >> 1);
}

uint32_t GetImageMipLevels(uint32_t width, uint32_t height)
{
	uint32_t result = 1;

	while (width > 1 || height > 1)
	{
		result++;
		width = width >> 1;
		height = height >> 1;
	}

	return result;
}

void VulkanEngine::InitSwapchain()
{
	vkb::SwapchainBuilder swapchainBuilder(m_ChosenGPU,m_Device,m_Surface);
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(m_WindowExtent.width, m_WindowExtent.height)
		.build().value();

	m_SwapChain = vkbSwapchain.swapchain;
	m_SwapchainImages = vkbSwapchain.get_images().value();
	m_SwapchainImageViews = vkbSwapchain.get_image_views().value();
	m_SwapchainImageFormat = vkbSwapchain.image_format;

	m_MainDeletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
		});

	m_MainDeletionQueue.push_function([=]() {
		for (int i = 0; i < m_SwapchainImageViews.size(); ++i)
		{
			vkDestroyImageView(m_Device, m_SwapchainImageViews[i], nullptr);
		}
		});

	//render image
	{
		VkExtent3D renderImageExt{ m_WindowExtent.width, m_WindowExtent.height, 1 };
		m_RenderFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		VkImageCreateInfo rawImageInfo = vkinit::image_create_info(m_RenderFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, renderImageExt);

		VmaAllocationCreateInfo imgAllocInfo{};
		imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		vmaCreateImage(m_Allocator, &rawImageInfo, &imgAllocInfo, &m_RawRenderImage.image, &m_RawRenderImage.allocation, nullptr);

		VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(m_RenderFormat, m_RawRenderImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

		VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_RawRenderImage.defaultView));

		m_MainDeletionQueue.push_function([=]() {
			vkDestroyImageView(m_Device, m_RawRenderImage.defaultView, nullptr);
			vmaDestroyImage(m_Allocator, m_RawRenderImage.image, m_RawRenderImage.allocation);
			});
	}

	m_depthFormat = VK_FORMAT_D32_SFLOAT;
	VmaAllocationCreateInfo depthAllocationInfo{};
	depthAllocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	depthAllocationInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//depth image
	{
		VkExtent3D depthImageExtent = {
			m_WindowExtent.width,
			m_WindowExtent.height,
			1
		};
		VkImageCreateInfo depthImageInfo = vkinit::image_create_info(m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

		vmaCreateImage(m_Allocator, &depthImageInfo, &depthAllocationInfo, &m_DepthImage.image, &m_DepthImage.allocation, nullptr);

		VkImageViewCreateInfo depthViewInfo = vkinit::imageview_create_info(m_depthFormat, m_DepthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
		VK_CHECK(vkCreateImageView(m_Device, &depthViewInfo, nullptr, &m_DepthImage.defaultView));

		m_MainDeletionQueue.push_function([=]() {
			vkDestroyImageView(m_Device, m_DepthImage.defaultView, nullptr);
			vmaDestroyImage(m_Allocator, m_DepthImage.image, m_DepthImage.allocation);
			});
	}
	//shadow image
	{
		VkExtent3D shadowExtent = {
			m_ShadowExtent.width,
			m_ShadowExtent.height,
			1
		};
		VkImageCreateInfo shadowImgInfo = vkinit::image_create_info(m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, shadowExtent);
		vmaCreateImage(m_Allocator, &shadowImgInfo, &depthAllocationInfo, &m_ShadowImage.image, &m_ShadowImage.allocation, nullptr);

		VkImageViewCreateInfo shadowViewInfo = vkinit::imageview_create_info(m_depthFormat, m_ShadowImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
		VK_CHECK(vkCreateImageView(m_Device, &shadowViewInfo, nullptr, &m_ShadowImage.defaultView));

		m_MainDeletionQueue.push_function([=]() {
			vkDestroyImageView(m_Device, m_ShadowImage.defaultView, nullptr);
			vmaDestroyImage(m_Allocator, m_ShadowImage.image, m_ShadowImage.allocation);
			});
	}

	//depth pyramid
	{
		VkFormat pyramidFmt = VK_FORMAT_R32_SFLOAT;
		m_DepthPyramidWidth = PreviousPow2(m_WindowExtent.width);
		m_DepthPyramidHeight = PreviousPow2(m_WindowExtent.height);
		m_DepthPyramidLevels = GetImageMipLevels(m_DepthPyramidWidth, m_DepthPyramidHeight);

		VkExtent3D pyramidExt{ m_DepthPyramidWidth, m_DepthPyramidHeight, 1 };
		VkImageCreateInfo pyramidInfo = vkinit::image_create_info(pyramidFmt, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, pyramidExt);
		pyramidInfo.mipLevels = m_DepthPyramidLevels;

		vmaCreateImage(m_Allocator, &pyramidInfo, &depthAllocationInfo, &m_DepthPyramidImage.image, &m_DepthPyramidImage.allocation, nullptr);

		VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(pyramidFmt, m_DepthPyramidImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		viewInfo.subresourceRange.levelCount = m_DepthPyramidLevels;

		VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_DepthPyramidImage.defaultView));

		for (uint32_t i = 0; i < m_DepthPyramidLevels; ++i)
		{
			VkImageViewCreateInfo levelInfo = vkinit::imageview_create_info(pyramidFmt, m_DepthPyramidImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
			levelInfo.subresourceRange.levelCount = 1;
			levelInfo.subresourceRange.baseMipLevel = i;

			VkImageView pyramidView;
			vkCreateImageView(m_Device, &levelInfo, nullptr, &pyramidView);
			m_DepthPyramidMips[i] = pyramidView;
		}

		m_MainDeletionQueue.push_function([=]() {
			for (uint32_t i = 0; i < m_DepthPyramidLevels; ++i)
			{
				vkDestroyImageView(m_Device, m_DepthPyramidMips[i], nullptr);
			}
			});
	}

	//Samplers
	{
		VkSamplerCreateInfo depthSamplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE);
		depthSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		depthSamplerInfo.minLod = 0;
		depthSamplerInfo.maxLod = 16.f;

		auto reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN;
		if (reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT)
		{
			VkSamplerReductionModeCreateInfoEXT reductionExt{ VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT };
			reductionExt.pNext = nullptr;
			reductionExt.reductionMode = reductionMode;
			depthSamplerInfo.pNext = &reductionExt;
		}

		VK_CHECK(vkCreateSampler(m_Device, &depthSamplerInfo, nullptr, &m_DepthSampler));
		
		VkSamplerCreateInfo smoothSamplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE);
		smoothSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

		VK_CHECK(vkCreateSampler(m_Device, &smoothSamplerInfo, nullptr, &m_SmoothSampler));

		VkSamplerCreateInfo shadowSamplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE);
		shadowSamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		shadowSamplerInfo.compareEnable = true;
		shadowSamplerInfo.compareOp = VK_COMPARE_OP_LESS;
		vkCreateSampler(m_Device, &shadowSamplerInfo, nullptr, &m_ShadowSampler);

		m_MainDeletionQueue.push_function([=]() {
			vkDestroySampler(m_Device, m_DepthSampler, nullptr);
			vkDestroySampler(m_Device, m_SmoothSampler, nullptr);
			vkDestroySampler(m_Device, m_ShadowSampler, nullptr);
			});
	}

}

void VulkanEngine::InitCommands()
{
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(m_GraphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		VK_CHECK(vkCreateCommandPool(m_Device, &commandPoolInfo, nullptr, &m_Frames[i].commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_Frames[i].commandPool, 1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

		VK_CHECK(vkAllocateCommandBuffers(m_Device, &cmdAllocInfo, &m_Frames[i].mainCommandBuffer));

		m_MainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(m_Device, m_Frames[i].commandPool, nullptr);
			});
	}

	m_GraphicQueueContext = TracyVkContext(m_ChosenGPU, m_Device, m_GraphicsQueue, m_Frames[0].mainCommandBuffer);
	
	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(m_GraphicsQueueFamily);

	VK_CHECK(vkCreateCommandPool(m_Device, &uploadCommandPoolInfo, nullptr, &m_UploadContext.commandPool));
	m_MainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(m_Device, m_UploadContext.commandPool, nullptr);
		});
}

void VulkanEngine::InitForwardRenderpass()
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = m_SwapchainImageFormat;
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
	depthAttachment.format = m_depthFormat;
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


	VK_CHECK(vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_Passes[PassType::Forward]));

	m_MainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(m_Device, GetRenderPass(PassType::Forward), nullptr);
		});
}

void VulkanEngine::InitCopyRenderpass()
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = m_SwapchainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.pNext = nullptr;

	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	/*VkSubpassDependency colorDependency{};
	colorDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	colorDependency.dstSubpass = 0;
	colorDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	colorDependency.srcAccessMask = 0;
	colorDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	colorDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;*/

	/*renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &colorDependency;*/


	VK_CHECK(vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_Passes[PassType::Copy]));

	m_MainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(m_Device, GetRenderPass(PassType::Copy), nullptr);
		});
}

void VulkanEngine::InitShadowRenderpass()
{
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.flags = 0;
	depthAttachment.format = m_depthFormat;
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
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.pNext = nullptr;

	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &depthAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	/*VkSubpassDependency depthDependency{};
	depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depthDependency.dstSubpass = 0;
	depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.srcAccessMask = 0;
	depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &depthDependency;*/


	VK_CHECK(vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_Passes[PassType::Shadow]));

	m_MainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(m_Device, GetRenderPass(PassType::Shadow), nullptr);
		});
}

void VulkanEngine::InitFramebuffers()
{
	VkFramebufferCreateInfo forwardInfo = vkinit::framebuffer_create_info(GetRenderPass(PassType::Forward), m_WindowExtent);
	forwardInfo.attachmentCount = 2;
	VkImageView attachments[2];
	attachments[0] = m_RawRenderImage.defaultView;
	attachments[1] = m_DepthImage.defaultView;
	forwardInfo.pAttachments = attachments;

	VK_CHECK(vkCreateFramebuffer(m_Device, &forwardInfo, nullptr, &m_ForwardFramebuffer));
	m_MainDeletionQueue.push_function([=]() {
		vkDestroyFramebuffer(m_Device, m_ForwardFramebuffer, nullptr);
		});

	VkFramebufferCreateInfo shadowInfo = vkinit::framebuffer_create_info(GetRenderPass(PassType::Shadow), m_ShadowExtent);
	shadowInfo.pAttachments = &m_ShadowImage.defaultView;
	shadowInfo.attachmentCount = 1;
	VK_CHECK(vkCreateFramebuffer(m_Device, &shadowInfo, nullptr, &m_ShadowFramebuffer));
	m_MainDeletionQueue.push_function([=]() {
		vkDestroyFramebuffer(m_Device, m_ShadowFramebuffer, nullptr);
		});

	const uint32_t swapchainImageCount = (uint32_t)m_SwapchainImageViews.size();
	m_FrameBuffers = std::vector<VkFramebuffer>(swapchainImageCount);
	for (uint32_t i = 0; i < swapchainImageCount; ++i)
	{
		VkFramebufferCreateInfo frameInfo = vkinit::framebuffer_create_info(GetRenderPass(PassType::Copy), m_WindowExtent);
		frameInfo.pAttachments = &m_SwapchainImageViews[i];
		frameInfo.attachmentCount = 1;

		VK_CHECK(vkCreateFramebuffer(m_Device, &frameInfo, nullptr, &m_FrameBuffers[i]));
		m_MainDeletionQueue.push_function([=]() {
				vkDestroyFramebuffer(m_Device, m_FrameBuffers[i], nullptr);
			});
	}
}

void VulkanEngine::InitSyncStructures()
{
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info();
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		VK_CHECK(vkCreateFence(m_Device, &fenceCreateInfo, nullptr, &m_Frames[i].renderFence));
		m_MainDeletionQueue.push_function([=]() {
			vkDestroyFence(m_Device, m_Frames[i].renderFence, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr, &m_Frames[i].presentSemaphore));
		m_MainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(m_Device, m_Frames[i].presentSemaphore, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr, &m_Frames[i].renderSemaphore));
		m_MainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(m_Device, m_Frames[i].renderSemaphore, nullptr);
			});
	}

	VkFenceCreateInfo uploadFenceCreateInfo = vkinit::fence_create_info();
	VK_CHECK(vkCreateFence(m_Device, &uploadFenceCreateInfo, nullptr, &m_UploadContext.uploadFence));
	vkResetFences(m_Device, 1, &m_UploadContext.uploadFence);
	m_MainDeletionQueue.push_function([=] {
		vkDestroyFence(m_Device, m_UploadContext.uploadFence, nullptr);
		});

}

void VulkanEngine::InitDescriptors()
{
	m_DescriptorAllocator = new vkutil::DescriptorAllocator();
	m_DescriptorAllocator->init(m_Device);

	m_MainDeletionQueue.push_function([=] {
		m_DescriptorAllocator->Cleanup();
		delete m_DescriptorAllocator;
		m_DescriptorAllocator = nullptr;
		});

	m_DescritptorLayoutCache = new vkutil::DescriptorLayoutCache();
	m_DescritptorLayoutCache->init(m_Device);
	m_MainDeletionQueue.push_function([=] {
		m_DescritptorLayoutCache->Cleanup();
		delete m_DescritptorLayoutCache;
		m_DescritptorLayoutCache = nullptr;
		});
	{
		VkDescriptorSetLayoutBinding textureBinding = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		VkDescriptorSetLayoutCreateInfo layoutCreateInfo = vkinit::descriptorset_layout_create_info(&textureBinding, 1);
		m_SingleTextureSetLayout = m_DescritptorLayoutCache->CreateDescriptorLayout(&layoutCreateInfo);
	}

	const size_t sceneParamBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(GPUSceneData));

	
	m_SceneParameterBuffer = CreateBuffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		m_Frames[i].dynamicDescriptorAllocator = new vkutil::DescriptorAllocator();
		m_Frames[i].dynamicDescriptorAllocator->init(m_Device);
		

		AllocatedBufferUntyped dynamicDataBuffer = CreateBuffer(1000000, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		m_Frames[i].debugOutputBuffer = CreateBuffer(20000000, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

		m_Frames[i].dynamicData.Init(m_Allocator, dynamicDataBuffer, (uint32_t)m_GpuPropertices.limits.minUniformBufferOffsetAlignment);
		
		m_MainDeletionQueue.push_function([=] {
			m_Frames[i].dynamicDescriptorAllocator->Cleanup();
			delete m_Frames[i].dynamicDescriptorAllocator;
			m_Frames[i].dynamicDescriptorAllocator = nullptr;
			DestroyBuffer(dynamicDataBuffer);
			DestroyBuffer(m_Frames[i].debugOutputBuffer);
			});
	}
}

void VulkanEngine::InitPipelines()
{
	m_MaterialSystem = new vkutil::MaterialSystem();
	m_MaterialSystem->Init(this);
	m_MaterialSystem->BuildDefaultTemplates();

	ShaderEffect* blitEffect = new ShaderEffect();
	blitEffect->AddStage(m_ShaderCache.GetShader(ShaderPath("fullscreen.vert.spv")), VK_SHADER_STAGE_VERTEX_BIT);
	blitEffect->AddStage(m_ShaderCache.GetShader(ShaderPath("Blit.frag.spv")), VK_SHADER_STAGE_FRAGMENT_BIT);
	blitEffect->ReflectLayout(m_Device, nullptr, 0);

	PipelineBuilder pipelineBuilder;
	pipelineBuilder.inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
	pipelineBuilder.rasterizer.cullMode = VK_CULL_MODE_NONE;
	pipelineBuilder.multisampling = vkinit::multisampling_state_create_info();
	pipelineBuilder.colorBlendAttachment = vkinit::color_blend_attachment_state();
	pipelineBuilder.depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_ALWAYS);
	pipelineBuilder.SetShaders(blitEffect);
	pipelineBuilder.ClearVertexInput();

	m_BlitPipeline = pipelineBuilder.BuildPipeline(m_Device, GetRenderPass(PassType::Copy));
	m_BlitLayout = blitEffect->builtLayout;

	m_MainDeletionQueue.push_function([=]() {
		vkDestroyPipelineLayout(m_Device, blitEffect->builtLayout, nullptr);
		vkDestroyPipeline(m_Device, m_BlitPipeline, nullptr);
		delete blitEffect;
		});

	LoadComputeShader(ShaderPath("indirect_cull.comp.spv").c_str(), m_CullPipeline, m_CullLayout);
	LoadComputeShader(ShaderPath("depth_reduce.comp.spv").c_str(), m_DepthReducePipeline, m_DepthReduceLayout);
	LoadComputeShader(ShaderPath("sparse_upload.comp.spv").c_str(), m_SparseUploadPipeline, m_SparseUploadLayout);
}

bool VulkanEngine::LoadComputeShader(const char* shaderPath, VkPipeline& pipeline, VkPipelineLayout& layout)
{
	ShaderModule computeModule;
	if (!vkutil::LoadShaderModule(m_Device, shaderPath, &computeModule))
	{
		LOG_ERROR("Error when building compute shader module {}", shaderPath);
		return false;
	}

	ShaderEffect* computeEffect = new ShaderEffect();
	computeEffect->AddStage(&computeModule, VK_SHADER_STAGE_COMPUTE_BIT);
	computeEffect->ReflectLayout(m_Device, nullptr, 0);
	ComputePipelineBuilder computeBuilder;
	computeBuilder.pipelineLayout = computeEffect->builtLayout;
	computeBuilder.shaderStage = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_COMPUTE_BIT, computeModule.module);

	layout = computeEffect->builtLayout;
	pipeline = computeBuilder.BuildPipeline(m_Device);

	vkDestroyShaderModule(m_Device, computeModule.module, nullptr);

	m_MainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(m_Device, pipeline, nullptr);
		vkDestroyPipelineLayout(m_Device, layout, nullptr);
		delete computeEffect;
		});
	return true;
}

void VulkanEngine::InitScene()
{
	m_RenderScene.Init();
	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_NEAREST);
	VkSampler blockySampler;
	vkCreateSampler(m_Device, &samplerInfo, nullptr, &blockySampler);

	samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 2;
	samplerInfo.maxLod = 30.f;
	samplerInfo.minLod = 3.f;
	VkSampler smoothSampler;
	vkCreateSampler(m_Device, &samplerInfo, nullptr, &smoothSampler);

	{
		vkutil::MaterialData texturedInfo;
		texturedInfo.baseTemplate = "texturedPBR_opaque";
		texturedInfo.parameters = nullptr;

		vkutil::SampledTexture whiteTex;
		whiteTex.sampler = smoothSampler;
		whiteTex.view = m_LoadedTextures["white"].imageView;

		texturedInfo.textures.push_back(whiteTex);

		m_MaterialSystem->BuildMaterial("textured", texturedInfo);
		m_MaterialSystem->BuildMaterial("default", texturedInfo);
	}

	int dimHelmets = 1;
	for (int x = -dimHelmets; x <= dimHelmets; x++) {
		for (int y = -dimHelmets; y <= dimHelmets; y++) {

			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x * 5, 10, y * 5));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(10));

			LoadPrefab(AssetPath("FlightHelmet/FlightHelmet.pfb").c_str(), (translation * scale));
		}
	}

	glm::mat4 sponzaMatrix = glm::scale(glm::mat4{ 1.0 }, glm::vec3(1));;

	glm::mat4 unrealFixRotation = glm::rotate(glm::radians(-90.f), glm::vec3{ 1,0,0 });

	LoadPrefab(AssetPath("Sponza2.pfb").c_str(), sponzaMatrix);
	LoadPrefab(AssetPath("scifi/TopDownScifi.pfb").c_str(), glm::translate(glm::vec3{ 0,20,0 }));
	int dimcities = 2;
	for (int x = -dimcities; x <= dimcities; x++) {
		for (int y = -dimcities; y <= dimcities; y++) {

			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x * 300, y, y * 300));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(10));

			glm::mat4 cityMatrix = translation;
			LoadPrefab(AssetPath("CITY/polycity.pfb").c_str(), cityMatrix);
		}
	}

	m_RenderScene.BuildBatches();
	m_RenderScene.MergeMeshes(this);
}

void VulkanEngine::InitImgui()
{
	VkDescriptorPoolSize poolSize[] =
	{
		{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
		{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
		{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.pNext = nullptr;
	poolInfo.maxSets = 1000;
	poolInfo.poolSizeCount = (uint32_t)std::size(poolSize);
	poolInfo.pPoolSizes = poolSize;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &imguiPool));

	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(m_Window);

	ImGui_ImplVulkan_InitInfo initInfo{};
	initInfo.Instance = m_Instance;
	initInfo.PhysicalDevice = m_ChosenGPU;
	initInfo.Device = m_Device;
	initInfo.Queue = m_GraphicsQueue;
	initInfo.DescriptorPool = imguiPool;
	initInfo.MinImageCount = 3;
	initInfo.ImageCount = 3;
	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo, GetRenderPass(PassType::Forward));

	ImmediateSubmit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
		});
	
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	m_MainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(m_Device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});
}

void VulkanEngine::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	ZoneScopedNC("Immediate Submit", tracy::Color::White);

	VkCommandBuffer cmd;

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_UploadContext.commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(m_Device, &cmdAllocInfo, &cmd));

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);

	VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submit, m_UploadContext.uploadFence));

	vkWaitForFences(m_Device, 1, &m_UploadContext.uploadFence, true, 9999999999);
	vkResetFences(m_Device, 1, &m_UploadContext.uploadFence);

	vkResetCommandPool(m_Device, m_UploadContext.commandPool, 0);
}

ShaderModule* VulkanEngine::GetShaderModule(const std::string& path)
{
	return m_ShaderCache.GetShader(path);
}

bool VulkanEngine::LoadPrefab(const char* path, glm::mat4 root)
{
	ZoneScopedNC("Load prefab", tracy::Color::Red);

	assets::PrefabInfo* prefab;
	auto it = m_PrefabCache.find(path);
	if (it == m_PrefabCache.end())
	{
		assets::AssetFile file;
		bool loaded = assets::LoadBinaryFile(path, file);

		if (!loaded)
		{
			LOG_FATAL("Errot when loading prefab file at path {}", path);
			return false;
		}
		else
		{
			LOG_SUCCESS("Prefab {} loaded to cache", path);
		}
		prefab = new assets::PrefabInfo;
		*prefab = assets::ReadPrefabInfo(&file);
		m_PrefabCache[path] = prefab;
	}
	else
	{
		prefab = it->second;
	}

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	VkSampler smoothSampler;
	vkCreateSampler(m_Device, &samplerInfo, nullptr, &smoothSampler);

	std::unordered_map<uint64_t, glm::mat4> nodeWorldMats;
	std::vector<std::pair<uint64_t, glm::mat4>> pendingNodes;
	for (auto& [k, v] : prefab->node_matrices)
	{
		glm::mat4 nodematrix;
		auto localMat = prefab->matrices[v];
		memcpy(&nodematrix, &localMat, sizeof(glm::vec4));

		auto matrixIt = prefab->node_parents.find(k);
		if (matrixIt == prefab->node_parents.end())
		{
			nodeWorldMats[k] = root * nodematrix;
		}
		else
		{
			pendingNodes.push_back({ k,nodematrix });
		}
	}

	while (pendingNodes.size() > 0)
	{
		for (int i = 0; i < pendingNodes.size(); ++i)
		{
			uint64_t node = pendingNodes[i].first;
			uint64_t parent = prefab->node_parents[node];
			auto matrixIt = nodeWorldMats.find(parent);
			if (matrixIt != nodeWorldMats.end())
			{
				nodeWorldMats[node] = matrixIt->second * pendingNodes[i].second;
				pendingNodes[i] = pendingNodes.back();
				pendingNodes.pop_back();
				--i;
			}
		}
	}

	std::vector<MeshObject> prefabRenderables;
	prefabRenderables.reserve(prefab->node_meshes.size());

	for (auto& [k, v] : prefab->node_meshes)
	{
		if (v.mesh_path.find("Sky") != std::string::npos)
		{
			continue;
		}

		const std::string& meshName = v.mesh_path;
		if (!GetMesh(meshName))
		{
			Mesh mesh{};
			mesh.LoadFromMeshAsset(AssetPath(meshName).c_str());
			UploadMesh(mesh);
			m_Meshes[meshName] = mesh;
		}

		const std::string& materialName = v.material_path;

		bool isTransparent = false;
		vkutil::Material* objectMaterial = m_MaterialSystem->GetMaterial(materialName);
		if (!objectMaterial)
		{
			assets::AssetFile materialFile;
			bool loaded = assets::LoadBinaryFile(AssetPath(materialName).c_str(), materialFile);
			if (loaded)
			{
				assets::MaterialInfo material = assets::read_material_info(&materialFile);

				auto textureName = material.textures["baseColor"];
				if (textureName.size() <= 3)
				{
					textureName = "Sponza/White.tx";
				}

				loaded = LoadImageToCache(textureName, AssetPath(textureName));

				if (loaded)
				{
					vkutil::SampledTexture tex;
					tex.view = m_LoadedTextures[textureName].imageView;
					tex.sampler = smoothSampler;

					vkutil::MaterialData info;
					info.parameters = nullptr;

					if (material.transparency == assets::TransparencyMode::Transparent)
					{
						info.baseTemplate = "texturedPBR_transparent";
						isTransparent = true;
					}
					else
					{
						info.baseTemplate = "texturedPBR_opaque";
					}

					info.textures.push_back(tex);

					objectMaterial = m_MaterialSystem->BuildMaterial(materialName, info);

					if (!objectMaterial)
					{
						LOG_ERROR("Error when building materia {}", v.material_path);
					}
				}
				else
				{
					LOG_ERROR("Error when loading image at {}", v.material_path);
				}
			}
			else
			{
				LOG_ERROR("Error when loading material at path {}", v.material_path);
			}
		}

		MeshObject loadmesh;
		
		loadmesh.bDrawForwardPass = true;
		loadmesh.bDrawShadowPass = !isTransparent;

		glm::mat4 nodematrix{ 1.f };

		auto matrixIt = nodeWorldMats.find(k);
		if (matrixIt != nodeWorldMats.end())
		{
			memcpy(&nodematrix, &(matrixIt->second), sizeof(glm::mat4));
		}

		loadmesh.mesh = GetMesh(meshName);
		loadmesh.transformMatrix = nodematrix;
		loadmesh.material = objectMaterial;

		RefreshRenderBounds(&loadmesh);
		loadmesh.customSortKey = 0;
		prefabRenderables.push_back(loadmesh);
	}

	m_RenderScene.RegisterObjectBatch(prefabRenderables.data(), (uint32_t)prefabRenderables.size());
	return true;
}

void VulkanEngine::RefreshRenderBounds(MeshObject* object)
{
	if (!object->mesh->bounds.valid) return;
}


std::string VulkanEngine::ShaderPath(std::string_view path)
{
	return "../../shaders/" + std::string(path);
}

std::string VulkanEngine::AssetPath(std::string_view path)
{
	return "../../assets_export/" + std::string(path);
}

void VulkanEngine::LoadMeshes()
{
	m_Meshes.reserve(1000);

	Mesh triangleMesh;
	triangleMesh.vertices.resize(3);

	triangleMesh.vertices[0].position = { 1.f, 1.f, 0.0f };
	triangleMesh.vertices[1].position = { -1.f, 1.f, 0.0f };
	triangleMesh.vertices[2].position = { 0.f, -1.f, 0.0f };

	triangleMesh.vertices[0].color = { 0.f, 1.f, 0.0f };
	triangleMesh.vertices[1].color = { 0.f, 1.f, 0.0f };
	triangleMesh.vertices[2].color = { 0.f, 1.f, 0.0f };

	UploadMesh(triangleMesh);
	m_Meshes["triangle"] = triangleMesh;
}

void VulkanEngine::LoadImages()
{
	LoadImageToCache("white", AssetPath("Sponza/white.tx"));
}

bool VulkanEngine::LoadImageToCache(const char* name, const char* path)
{
	ZoneScopedNC("Load Texture", tracy::Color::Yellow);

	if (m_LoadedTextures.find(name) != m_LoadedTextures.end())
	{
		return true;
	}

	Texture tex;
	bool result = vkutil::LoadImageFromAsset(*this, path, tex.image);
	if (!result)
	{
		LOG_ERROR("Errir when loading texture {} at path {}", name, path);
		return false;
	}
	else
	{
		LOG_SUCCESS("Loaded Texture {} at path {}", name, path);
	}
	tex.imageView = tex.image.defaultView;

	m_LoadedTextures[name] = tex;
	return true;
}

void VulkanEngine::UploadMesh(Mesh& mesh)
{
	ZoneScopedNC("Upload Mesh", tracy::Color::Orange);

	const size_t vertexBufferSize = mesh.vertices.size() * sizeof(Vertex);
	VkBufferCreateInfo vertexBufferInfo{};

	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	vertexBufferInfo.size = vertexBufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	VK_CHECK(vmaCreateBuffer(m_Allocator, &vertexBufferInfo, &vmaAllocInfo,
		&mesh.vertexBuffer.buffer,
		&mesh.vertexBuffer.allocation,
		nullptr));


	void* data;
	vmaMapMemory(m_Allocator, mesh.vertexBuffer.allocation, &data);
	memcpy(data, mesh.vertices.data(), vertexBufferSize);
	vmaUnmapMemory(m_Allocator, mesh.vertexBuffer.allocation);

	if (mesh.indices.size() > 0)
	{
		const size_t indexBufferSize = mesh.indices.size() * sizeof(uint32_t);
		VkBufferCreateInfo indexBufferInfo{};
		indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		indexBufferInfo.pNext = nullptr;
		indexBufferInfo.size = indexBufferSize;
		indexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		VK_CHECK(vmaCreateBuffer(m_Allocator, &indexBufferInfo, &vmaAllocInfo,
			&mesh.indexBuffer.buffer,
			&mesh.indexBuffer.allocation,
			nullptr));


		vmaMapMemory(m_Allocator, mesh.indexBuffer.allocation, &data);
		memcpy(data, mesh.indices.data(), indexBufferSize);
		vmaUnmapMemory(m_Allocator, mesh.indexBuffer.allocation);
	}
}

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize)
{
	size_t minAlignment = m_GpuPropertices.limits.minUniformBufferOffsetAlignment;
	size_t alignSize = originalSize;
	if (minAlignment > 0)
	{
		alignSize = (alignSize + minAlignment - 1) & ~(minAlignment - 1);
	}
	return alignSize;
}

Mesh* VulkanEngine::GetMesh(const std::string& name)
{
	return nullptr;
}

bool VulkanEngine::LoadImageToCache(const std::string& name, const std::string& path)
{
	return false;
}

void VulkanEngine::ReallocateBuffer(AllocatedBufferUntyped& buffer, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags requiredFlags)
{
	AllocatedBufferUntyped newBUffer = CreateBuffer(allocSize, usage, memoryUsage, requiredFlags);
	GetCurrentFrame().frameDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(m_Allocator, buffer.buffer, buffer.allocation);
		});
	buffer = newBUffer;
}

void* VulkanEngine::MapBuffer(AllocatedBufferUntyped& buffer)
{
	void* data;
	vmaMapMemory(m_Allocator, buffer.allocation, &data);
	return data;
}

void VulkanEngine::UnmapBuffer(AllocatedBufferUntyped& buffer)
{
	vmaUnmapMemory(m_Allocator, buffer.allocation);
}

AllocatedImage VulkanEngine::CreateImage(VkImageCreateInfo* createInfo, VmaAllocationCreateInfo* allocInfo, VkFormat format, VkImageAspectFlags aspectFlags, int mip)
{
	AllocatedImage image{};
	vmaCreateImage(m_Allocator, createInfo, allocInfo, &image.image, &image.allocation, nullptr);
	image.mipLevels = mip;
	VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(format, image.image, aspectFlags);
	viewInfo.subresourceRange.levelCount = mip;
	vkCreateImageView(m_Device, &viewInfo, nullptr, &image.defaultView);
	return image;
}

void VulkanEngine::DestroyImage(AllocatedImage& image)
{
	vmaDestroyImage(m_Allocator, image.image, image.allocation);
}

void VulkanEngine::ClearVulkan()
{
	vmaDestroyAllocator(m_Allocator);
	vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
	vkDestroyDevice(m_Device, nullptr);
	vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger);
	vkDestroyInstance(m_Instance, nullptr);
}


glm::mat4 DirectionalLight::GetProjection()
{
	glm::mat4 projection = glm::orthoLH_ZO(-shadowExtent.x, shadowExtent.x, -shadowExtent.y, -shadowExtent.y, -shadowExtent.z, shadowExtent.z);
	return projection;
}

glm::mat4 DirectionalLight::GetView()
{
	glm::vec3 camPos = lightPosition;
	glm::vec3 camFwd = lightDirection;
	glm::mat4 view = glm::lookAt(camPos, camPos + camFwd, glm::vec3(1, 0, 0));
	return view;
}
