
#include <vk_engine.h>
#include <vk_initializers.h>

#include <TracyVulkan.hpp>
#include <cvar.h>

AutoCVar_Int CVAR_FreezeCull("culling.freeze", "Locks culling", 0, CVarFlags::EditCheckBox);

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
