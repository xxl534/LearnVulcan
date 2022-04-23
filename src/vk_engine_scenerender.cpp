
#include <vk_engine.h>
#include <vk_initializers.h>
#include <vk_profiler.h>
#include <TracyVulkan.hpp>
#include <cvar.h>

AutoCVar_Int CVAR_FreezeCull("culling.freeze", "Locks culling", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_Shadowcast("gpu.shadowcast", "Use shadowcasting", 1, CVarFlags::EditCheckbox);

AutoCVar_Float CVAR_ShadowBias("gpu.shadowBias", "Distance cull", 5.25f);
AutoCVar_Float CVAR_SlopeBias("gpu.shadowBiasSlope", "Distance cull", 4.75f);

void VulkanEngine::ReadyMeshDraw(VkCommandBuffer cmd)
{
	FrameData& currentFrame = GetCurrentFrame();
	TracyVkZone(m_GraphicQueueContext, currentFrame.mainCommandBuffer, "DataRefresh");
	ZoneScopedNC("Draw upload", tracy::Color::Blue);

	if (m_RenderScene.dirtyObjects.size() > 0)
	{
		ZoneScopedNC("Refresh Object Buffer", tracy::Color::Red);

		size_t copySize = m_RenderScene.renderables.size() * sizeof(GPUObjectData);
		if (m_RenderScene.objectDataBuffer.size < copySize);
		{
			ReallocateBuffer(m_RenderScene.objectDataBuffer, copySize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		}

		//If 80% of the objects are dirty, just reupload the whole things;
		if (m_RenderScene.dirtyObjects.size() >= m_RenderScene.renderables.size() * 0.8)
		{
			AllocatedBuffer<GPUObjectData> newBuffer = CreateBuffer(copySize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

			GPUObjectData* objectSSBO = MapBuffer(newBuffer);
			m_RenderScene.FillObjectData(objectSSBO);
			UnmapBuffer(newBuffer);

			currentFrame.frameDeletionQueue.push_function([=]() {
				vmaDestroyBuffer(m_Allocator, newBuffer.buffer, newBuffer.allocation);
				});

			VkBufferCopy indirectCopy;
			indirectCopy.dstOffset = 0;
			indirectCopy.srcOffset = 0;
			indirectCopy.size = copySize;
			vkCmdCopyBuffer(cmd, newBuffer.buffer, m_RenderScene.objectDataBuffer.buffer, 1, &indirectCopy);
		}
		else
		{
			std::vector<VkBufferCopy> copies;
			copies.reserve(m_RenderScene.dirtyObjects.size());

			uint64_t buffersize = sizeof(GPUObjectData) * m_RenderScene.dirtyObjects.size();
			uint64_t vec4size = sizeof(glm::vec4);
			uint64_t intSize = sizeof(uint32_t);
			uint64_t wordSize = sizeof(GPUIndirectObject) / sizeof(uint32_t);
			uint64_t uploadSize = m_RenderScene.dirtyObjects.size() * wordSize * intSize;
			AllocatedBuffer<GPUObjectData> newBuffer = CreateBuffer(buffersize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
			AllocatedBuffer<uint32_t> targetBuffer = CreateBuffer(uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

			currentFrame.frameDeletionQueue.push_function([=]() {
				vmaDestroyBuffer(m_Allocator, newBuffer.buffer, newBuffer.allocation);
				vmaDestroyBuffer(m_Allocator, targetBuffer.buffer, targetBuffer.allocation);
				});

			uint32_t* targetData = MapBuffer(targetBuffer);
			GPUObjectData* objectSSBO = MapBuffer(newBuffer);
			uint32_t launchCount = m_RenderScene.dirtyObjects.size() * wordSize;
			{
				ZoneScopedNC("Write diry Objects", tracy::Color::Red);
				uint32_t sidx = 0;
				for (int i = 0; i < m_RenderScene.dirtyObjects.size(); ++i)
				{
					m_RenderScene.WriteObject(objectSSBO + i, m_RenderScene.dirtyObjects[i]);

					uint32_t dstOffset = static_cast<uint32_t>(wordSize * m_RenderScene.dirtyObjects[i].handle);

					for (int b = 0; b < wordSize; ++b)
					{
						uint32_t tidx = dstOffset + b;
						targetData[tidx] = tidx;
						++sidx;
					}
				}
				launchCount = sidx;
			}
			UnmapBuffer(newBuffer);
			UnmapBuffer(targetBuffer);

			VkDescriptorBufferInfo indexData = targetBuffer.GetInfo();
			VkDescriptorBufferInfo sourceData = newBuffer.GetInfo();
			VkDescriptorBufferInfo targetInfo = m_RenderScene.objectDataBuffer.GetInfo();

			VkDescriptorSet computeObjectDataSet;
			vkutil::DescriptorBuilder::Begin(m_DescritptorLayoutCache, currentFrame.dynamicDescriptorAllocator)
				.BindBuffer(0, &indexData, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.BindBuffer(1, &sourceData, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.BindBuffer(2, &targetInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.Build(computeObjectDataSet);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_SparseUploadPipeline);
			vkCmdPushConstants(cmd, m_SparseUploadLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &launchCount);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_SparseUploadLayout, 0, 1, &computeObjectDataSet, 0, nullptr);
			vkCmdDispatch(cmd, ((launchCount) / 256) + 1, 1, 1);
		}
		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(m_RenderScene.objectDataBuffer.buffer, m_GraphicsQueueFamily);
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		m_UploadBarriers.push_back(barrier);
		m_RenderScene.ClearDirtyObjects();
	}
}

void VulkanEngine::ReadyCullData(RenderScene::MeshPass& pass, VkCommandBuffer cmd)
{
	VkBufferCopy indirectCopy;
	indirectCopy.dstOffset = 0; 
	indirectCopy.size = pass.indirectBatches.size() * sizeof(GPUIndirectObject);
	indirectCopy.srcOffset = 0;
	vkCmdCopyBuffer(cmd, pass.clearIndirectBuffer.buffer, pass.drawIndirectBuffer.buffer, 1, &indirectCopy);
	{
		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(pass.drawIndirectBuffer.buffer, m_GraphicsQueueFamily);
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		m_CullReadyBarriers.push_back(barrier);
	}
}

void VulkanEngine::DrawObjectsForward(VkCommandBuffer cmd, RenderScene::MeshPass& pass)
{
	ZoneScopedNC("DrawObjects", tracy::Color::Blue);
	auto& currentFrame = GetCurrentFrame();
	glm::mat4 view = m_Camera.get_view_matrix();
	glm::mat4 projection = m_Camera.get_projection_matrix();

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	m_SceneParameters.sunlightShadowMatrix = m_MainLight.GetProjection() * m_MainLight.GetView();

	float framed = (m_FrameNumber / 120.f);
	m_SceneParameters.ambientColor = glm::vec4{ 0.5f };
	m_SceneParameters.sunlightColor = glm::vec4{ 1.f };
	m_SceneParameters.sunlightDirection = glm::vec4{ m_MainLight.lightDirection * 1.f, 1.f };
	m_SceneParameters.sunlightColor.w = CVAR_Shadowcast.Get() ? 0 : 1;

	//Push datas to Dynamic memory
	currentFrame.dynamicData.PushBegin();
	uint32_t sceneDataOffset = currentFrame.dynamicData.Push(m_SceneParameters);
	uint32_t cameraDataOffset = currentFrame.dynamicData.Push(camData);
	currentFrame.dynamicData.PushEnd();

	VkDescriptorBufferInfo objectBufferInfo = m_RenderScene.objectDataBuffer.GetInfo();
	VkDescriptorBufferInfo sceneInfo = currentFrame.dynamicData.source.GetInfo();
	sceneInfo.range = sizeof(GPUSceneData);
	
	VkDescriptorBufferInfo camInfo = currentFrame.dynamicData.source.GetInfo();
	camInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.compactedInstanceBuffer.GetInfo();

	VkDescriptorImageInfo shadowImage;
	shadowImage.sampler = m_ShadowSampler;
	shadowImage.imageView = m_ShadowImage.defaultView;
	shadowImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorSet globalSet;
	vkutil::DescriptorBuilder::Begin(m_DescritptorLayoutCache, currentFrame.dynamicDescriptorAllocator)
		.BindBuffer(0, &camInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
		.BindBuffer(1, &sceneInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.BindImage(2, &shadowImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.Build(globalSet);

	VkDescriptorSet objectDataSet;
	vkutil::DescriptorBuilder::Begin(m_DescritptorLayoutCache, currentFrame.dynamicDescriptorAllocator)
		.BindBuffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.BindBuffer(0, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.Build(objectDataSet);

	vkCmdSetDepthBias(cmd, 0, 0, 0);
	std::vector<uint32_t> dynamicOffsets;
	dynamicOffsets.push_back(cameraDataOffset);
	dynamicOffsets.push_back(sceneDataOffset);
	ExecuteDrawCommands(cmd, pass, objectDataSet, dynamicOffsets, globalSet);
	
}

void VulkanEngine::DrawObjectsShadow(VkCommandBuffer cmd, RenderScene::MeshPass& pass)
{
	ZoneScopedNC("DrawObjectShadows", tracy::Color::Blue);

	auto& currentFrame = GetCurrentFrame();
	glm::mat4 view = m_MainLight.GetView();
	glm::mat4 projection = m_MainLight.GetProjection();

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	currentFrame.dynamicData.PushBegin();
	uint32_t cameraDataOffset = currentFrame.dynamicData.Push(camData);
	currentFrame.dynamicData.PushEnd();

	VkDescriptorBufferInfo objectBufferInfo = m_RenderScene.objectDataBuffer.GetInfo();
	VkDescriptorBufferInfo camInfo = currentFrame.dynamicData.source.GetInfo();
	camInfo.range = sizeof(GPUCameraData);
	VkDescriptorBufferInfo instanceInfo = pass.compactedInstanceBuffer.GetInfo();


	VkDescriptorSet globalSet;
	vkutil::DescriptorBuilder::Begin(m_DescritptorLayoutCache, currentFrame.dynamicDescriptorAllocator)
		.BindBuffer(0, &camInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
		.Build(globalSet);

	VkDescriptorSet objectDataSet;
	vkutil::DescriptorBuilder::Begin(m_DescritptorLayoutCache, currentFrame.dynamicDescriptorAllocator)
		.BindBuffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.BindBuffer(0, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.Build(objectDataSet);

	vkCmdSetDepthBias(cmd, CVAR_ShadowBias.GetFloat(), 0, CVAR_SlopeBias.GetFloat());

	std::vector<uint32_t> dynamicOffsets;
	dynamicOffsets.push_back(cameraDataOffset);

	ExecuteDrawCommands(cmd, pass, objectDataSet, dynamicOffsets, globalSet);
}

void VulkanEngine::ExecuteDrawCommands(VkCommandBuffer cmd, RenderScene::MeshPass& passs, VkDescriptorSet objectDataSet, std::vector<uint32_t> dynamicOffsets, VkDescriptorSet globalSet)
{
	if (passs.indirectBatches.size() > 0)
	{
		ZoneScopedNC("Draw Commit", tracy::Color::Blue4);

		Mesh* lastMesh = nullptr;
		VkPipeline lastPipeline{ VK_NULL_HANDLE };
		VkPipelineLayout lastLayout{ VK_NULL_HANDLE };
		VkDescriptorSet lastMaterialSet{ VK_NULL_HANDLE };

		VkDeviceSize offset = 0;
		
		vkCmdBindVertexBuffers(cmd, 0, 1, &m_RenderScene.mergedVertexBuffer.buffer, &offset);
		vkCmdBindIndexBuffer(cmd, m_RenderScene.mergedVertexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		m_Stats.objects = passs.flatRenderBatches.size();
		for (int i = 0; i < passs.multibatches.size(); ++i)
		{
			auto& multibatch = passs.multibatches[i];
			auto& instanceDraw = passs.indirectBatches[multibatch.first];

			VkPipeline newPipeline = instanceDraw.material.shaderPass->pipeline;
			VkPipelineLayout newLayout = instanceDraw.material.shaderPass->layout;
			VkDescriptorSet newMaterialSet = instanceDraw.material.materialSet;
			Mesh* drawMesh = m_RenderScene.GetMesh(instanceDraw.meshId)->original;

			if (newPipeline != lastPipeline)
			{
				lastPipeline = newPipeline;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 1, 1, &objectDataSet, 0, nullptr);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 0, 1, &globalSet, dynamicOffsets.size(), dynamicOffsets.data());
			}
			if (newMaterialSet != lastMaterialSet)
			{
				lastMaterialSet = newMaterialSet;
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 2, 1, &newMaterialSet, 0, nullptr);
			}

			bool merged = m_RenderScene.GetMesh(instanceDraw.meshId)->isMerged;
			if (merged)
			{
				if (lastMesh != nullptr)
				{
					VkDeviceSize offset = 0;

					vkCmdBindVertexBuffers(cmd, 0, 1, &m_RenderScene.mergedVertexBuffer.buffer, &offset);
					vkCmdBindIndexBuffer(cmd, m_RenderScene.mergedVertexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
					lastMesh = nullptr;
				}
			}
			else if (lastMesh != drawMesh)
			{
				VkDeviceSize offset = 0;

				vkCmdBindVertexBuffers(cmd, 0, 1, &drawMesh->vertexBuffer.buffer, &offset);
				if (drawMesh->indexBuffer.buffer != VK_NULL_HANDLE)
				{
					vkCmdBindIndexBuffer(cmd, drawMesh->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
				}
				lastMesh = drawMesh;
			}

			bool hasIndices = drawMesh->indices.size() > 0;
			if (!hasIndices)
			{
				m_Stats.triangles += drawMesh->vertices.size() / 3 * instanceDraw.count;
				vkCmdDraw(cmd, drawMesh->vertices.size(), instanceDraw.count, 0, instanceDraw.first);

				++m_Stats.draws;
				m_Stats.drawcalls += instanceDraw.count;
			}
			else
			{
				m_Stats.triangles += drawMesh->indices.size() / 3 * instanceDraw.count;
				vkCmdDrawIndexedIndirect(cmd, passs.drawIndirectBuffer.buffer, multibatch.first * sizeof(GPUIndirectObject), multibatch.count, sizeof(GPUIndirectObject));

				++m_Stats.draws;
				m_Stats.drawcalls += instanceDraw.count;
			}
		}
	}
}

struct alignas(16) DepthReduceData
{
	glm::vec2 imageSize;
};

inline uint32_t GetGroupCount(uint32_t threadCount, uint32_t localSize)
{
	return (threadCount + localSize - 1) / localSize;
}

void VulkanEngine::ReduceDepth(VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer time(cmd, m_Profiler, "Depth Reduce");

	VkImageMemoryBarrier depthReadBarriers[] = {
		vkinit::image_barrier(m_DepthImage.image, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT),
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, depthReadBarriers);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_DepthReducePipeline);

	for (int32_t i = 0; i < m_DepthPyramidLevels; ++i)
	{
		VkDescriptorImageInfo dstTarget;
		dstTarget.sampler = m_DepthSampler;
		dstTarget.imageView = m_DepthPyramidMips[i];
		dstTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorImageInfo srcTarget;
		srcTarget.sampler = m_DepthSampler;
		if (i == 0)
		{
			srcTarget.imageView = m_DepthImage.defaultView;
			srcTarget.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		else
		{
			srcTarget.imageView = m_DepthPyramidMips[i - 1];
			srcTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}

		VkDescriptorSet depthSet;
		vkutil::DescriptorBuilder::Begin(m_DescritptorLayoutCache, GetCurrentFrame().dynamicDescriptorAllocator)
			.BindImage(0, &dstTarget, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
			.BindImage(0, &srcTarget, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
			.Build(depthSet);
		
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_DepthReduceLayout, 0, 1, &depthSet, 0, nullptr);

		uint32_t levelWidth = m_DepthPyramidWidth >> i;
		uint32_t levelHeight = m_DepthPyramidHeight >> i;
		if (levelHeight < 1)
			levelHeight = 1;
		if (levelWidth < 1)
			levelWidth = 1;

		DepthReduceData reduceData = { glm::vec2(levelWidth, levelHeight) };

		vkCmdPushConstants(cmd, m_DepthReduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(reduceData), &reduceData);
		vkCmdDispatch(cmd, GetGroupCount(levelWidth, 32), GetGroupCount(levelHeight, 32), 1);

		VkImageMemoryBarrier reduceBarrier = vkinit::image_barrier(m_DepthPyramidImage.image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &reduceBarrier);
	}
	VkImageMemoryBarrier depthWriteBarrier = vkinit::image_barrier(m_DepthImage.image, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depthWriteBarrier);
}

glm::vec4 NormalizePlane(glm::vec4 p)
{
	return p / glm::length(glm::vec3(p));
}

void VulkanEngine::ExecuteComputeCull(VkCommandBuffer cmd, RenderScene::MeshPass& pass, CullParams& params)
{
	if (CVAR_FreezeCull.Get())
		return;

	if (pass.indirectBatches.size() == 0)
		return;

	TracyVkZone(m_GraphicQueueContext, cmd, "Cull Dispatch");

	VkDescriptorBufferInfo objectBufferInfo = m_RenderScene.objectDataBuffer.GetInfo();
	VkDescriptorBufferInfo dynamicInfo = GetCurrentFrame().dynamicData.source.GetInfo();
	dynamicInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.passObjectsBuffer.GetInfo();
	VkDescriptorBufferInfo finalInfo = pass.compactedInstanceBuffer.GetInfo();
	VkDescriptorBufferInfo indirectInfo = pass.drawIndirectBuffer.GetInfo();

	VkDescriptorImageInfo depthPyramid;
	depthPyramid.sampler = m_DepthSampler;
	depthPyramid.imageView = m_DepthPyramidImage.defaultView;
	depthPyramid.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorSet computeObjectDataSet;
	vkutil::DescriptorBuilder::Begin(m_DescritptorLayoutCache, GetCurrentFrame().dynamicDescriptorAllocator)
		.BindBuffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.BindBuffer(1, &indirectInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.BindBuffer(2, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.BindBuffer(3, &finalInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.BindImage(4, &depthPyramid, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
		.BindBuffer(5, &dynamicInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.Build(computeObjectDataSet);

	glm::mat4 projection = params.projMat;
	glm::mat4 projectionT = transpose(projection);

	glm::vec4 frustumX = NormalizePlane(projectionT[3] + projectionT[0]);
	glm::vec4 frustumY = NormalizePlane(projectionT[3] + projectionT[1]);

	DrawCullData cullData{};
	cullData.viewMat = params.viewMat;
	cullData.p00 = projection[0][0];
	cullData.p11 = projection[1][1];
	cullData.znear = 0.1f;
	cullData.zfar = params.drawDist;
	cullData.frustum[0] = frustumX.x;
	cullData.frustum[1] = frustumX.z;
	cullData.frustum[2] = frustumY.y;
	cullData.frustum[3] = frustumY.z;
	cullData.lodBase = 10.f;
	cullData.lodStep = 1.5f;
	cullData.pyramidWidth = m_DepthPyramidWidth;
	cullData.pyramidHeight = m_DepthPyramidHeight;
	cullData.drawCount = pass.flatRenderBatches.size();
	cullData.cullingEnabled = params.frustrumCull;
	cullData.lodEnabled = false;
	cullData.occlusionEnabled = params.occlusionCull;
	cullData.distanceCheck = params.drawDist <= 10000;
	cullData.AABBCheck = params.aabb;
	cullData.aabbMinX = params.aabbMin.x;
	cullData.aabbMinY = params.aabbMin.y;
	cullData.aabbMinZ = params.aabbMin.z;
	cullData.aabbMaxX = params.aabbMax.x;
	cullData.aabbMaxY = params.aabbMax.y;
	cullData.aabbMaxZ = params.aabbMax.z;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline);
	vkCmdPushConstants(cmd, m_CullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DrawCullData), &cullData);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullLayout, 0, 1, &computeObjectDataSet, 0, nullptr);
	vkCmdDispatch(cmd, static_cast<uint32_t>(pass.flatRenderBatches.size() / 256) + 1, 1, 1);

	//barrier the 2 buffers we just wrote for culling, the indirect draw one, and the instances one, so that they can be read well when rendering the pass
	{
		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(pass.compactedInstanceBuffer.buffer, m_GraphicsQueueFamily);
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		m_PostCullBarriers.push_back(barrier);
	}
	{
		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(pass.drawIndirectBuffer.buffer, m_GraphicsQueueFamily);
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		m_PostCullBarriers.push_back(barrier);
	}

	if (*CVarSystem::Get()->GetIntCVar("culling.outputIndirectBufferToFile"))
	{
		uint32_t offset = GetCurrentFrame().debugDataOffsets.back();
		VkBufferCopy debugCopy;
		debugCopy.dstOffset = offset;
		debugCopy.size = pass.indirectBatches.size() * sizeof(GPUIndirectObject);
		debugCopy.srcOffset = 0;
		vkCmdCopyBuffer(cmd, pass.drawIndirectBuffer.buffer, GetCurrentFrame().debugOutputBuffer.buffer, 1, &debugCopy);
		GetCurrentFrame().debugDataOffsets.push_back(offset + debugCopy.size);
		GetCurrentFrame().debugDataNames.push_back("Cull Indirect Output");
	}
}
