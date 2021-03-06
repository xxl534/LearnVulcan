// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include "vk_mesh.h"

namespace vkinit {

	//vulkan init code goes here
	VkCommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0);

	VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool pool, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule shaderModule);

	VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info(VertexInputDescription* pInputDesc = nullptr);

	VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info(VkPrimitiveTopology topology);

	VkPipelineRasterizationStateCreateInfo rasterization_state_create_info(VkPolygonMode polygonMode);

	VkPipelineMultisampleStateCreateInfo multisampling_state_create_info();

	VkPipelineColorBlendAttachmentState color_blend_attachment_state();

	VkPipelineLayoutCreateInfo pipeline_layout_create_info(const std::vector<VkPushConstantRange>& constantRanges, const std::vector<VkDescriptorSetLayout>& layouts);
	VkPipelineLayoutCreateInfo pipeline_layout_create_info(const VkPushConstantRange* constantRanges, uint32_t constantCount, const VkDescriptorSetLayout* layouts, uint32_t layoutCount);

	VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);

	VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);

	VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info(bool bDepthTest, bool bDepthWrite, VkCompareOp compareOp);

	VkDescriptorSetLayoutBinding descriptorset_layout_binding(VkDescriptorType type, VkShaderStageFlags stageFlag, uint32_t binding);

	VkDescriptorSetLayoutCreateInfo descriptorset_layout_create_info(const VkDescriptorSetLayoutBinding* pBindings, uint32_t bindingCount, VkDescriptorSetLayoutCreateFlags flags = 0);

	VkWriteDescriptorSet write_descriptor_buffer(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorBufferInfo* bufferInfo, uint32_t binding);

	VkFenceCreateInfo fence_create_info();

	VkSemaphoreCreateInfo semaphore_create_info();

	VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags = 0);

	VkSubmitInfo submit_info(VkCommandBuffer* cmd);

	VkPresentInfoKHR present_info();

	VkSamplerCreateInfo sampler_create_info(VkFilter filter, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

	VkWriteDescriptorSet write_descriptor_image(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorImageInfo* imageInfo, uint32_t binding);

	VkFramebufferCreateInfo framebuffer_create_info(VkRenderPass renderPass, VkExtent2D extent);

	VkBufferMemoryBarrier buffer_barrier(VkBuffer buffer, uint32_t queue);

	VkRenderPassBeginInfo renderpass_begin_info(VkRenderPass renderPass, VkExtent2D windowExtent, uint32_t clearCount, VkClearValue* clearValues, VkFramebuffer framebuffer);

	VkImageMemoryBarrier image_barrier(VkImage iamge, VkAccessFlags srAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLAyout, VkImageLayout newLayout, VkImageAspectFlags aspectMask);
}

