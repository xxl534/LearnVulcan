#pragma once

#include "vk_types.h"
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct VertexInputDescription {
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 uv;

	static VertexInputDescription get_vertex_description();
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;

	AllocatedBuffer<Vertex> vertexBuffer;
	AllocatedBuffer<uint16_t> indexBuffer;

	bool load_from_obj(const char* filename);

	bool LoadFromMeshAsset(const char* filename);
};

struct RenderBounds {
	glm::vec3 origin;
	float radius;
	glm::vec3 extents;
	bool valid;
};