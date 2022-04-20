
#include <vk_engine.h>
#include <vk_initializers.h>

#include <TracyVulkan.hpp>

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