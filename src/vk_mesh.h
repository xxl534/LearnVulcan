#pragma once

#include "vk_types.h"
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <mesh_asset.h>

struct VertexInputDescription {
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex {
	glm::vec3 position;
	glm::vec<2, uint8_t> octNormal;
	glm::vec<3, uint8_t> color;
	glm::vec2 uv;

	static VertexInputDescription get_vertex_description();

	void PackNormal(glm::vec3 n);
	void PackColor(glm::vec3 c);
};

struct RenderBounds {
	glm::vec3 origin;
	float radius;
	glm::vec3 extents;
	bool valid;

	void FromMeshBound(assets::MeshBounds& meshBounds);
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	AllocatedBuffer<Vertex> vertexBuffer;
	AllocatedBuffer<uint32_t> indexBuffer;

	RenderBounds bounds;

	bool LoadFromMeshAsset(const char* filename);
};