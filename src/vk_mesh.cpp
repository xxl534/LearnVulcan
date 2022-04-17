#include "vk_mesh.h"
#include <tiny_obj_loader.h>
#include <iostream>
#include <glm/common.hpp>
#include <glm/detail/func_geometric.inl>
#include <asset_loader.h>
#include <mesh_asset.h>
#include <logger.h>


VertexInputDescription Vertex::get_vertex_description()
{
	VertexInputDescription description;

	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	description.bindings.push_back(mainBinding);

	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(Vertex, position);

	VkVertexInputAttributeDescription normalAttribute{};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	normalAttribute.offset = offsetof(Vertex, octNormal);

	VkVertexInputAttributeDescription colorAttribute{};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	colorAttribute.offset = offsetof(Vertex, color);

	VkVertexInputAttributeDescription uvAttribute{};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
	uvAttribute.offset = offsetof(Vertex, uv);

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);

	return description;
}

glm::vec2 OctNormalWrap(glm::vec2 v)
{
	glm::vec2 wrap;
	wrap.x = (1.0f - glm::abs(v.y)) * (v.x > 0.f ? 1.f : -1.f);
	wrap.y = (1.0f - glm::abs(v.x)) * (v.y > 0.f ? 1.f : -1.f);
	return wrap;
}

glm::vec2 OctNormalEncode(glm::vec3 n)
{
	n /= (glm::abs(n.x) + glm::abs(n.y) + glm::abs(n.z));

	glm::vec2 result;
	if (n.z >= 0)
	{
		result.x = n.x;
		result.y = n.y;
	}
	else
	{
		result = OctNormalWrap(n);
	}

	result = result * 0.5f + 0.5f;
	return result;
}

glm::vec3 OctNormalDecode(glm::vec2 encN)
{
	encN = encN * 2.f - 1.f;
	glm::vec3 n = glm::vec3(encN.x, encN.y, 1.f - glm::abs(encN.x) - glm::abs(encN.y));
	float t = glm::clamp(-n.z, 0.f, 1.f);

	n.x += n.x >= 0.f ? -t : t;
	n.y += n.y >= 0.f ? -t : t;

	n = glm::normalize(n);
	return n;
}
void Vertex::PackNormal(glm::vec3 n)
{
	glm::vec2 oct = OctNormalEncode(n);

	octNormal.x = uint8_t(oct.x * 255);
	octNormal.y = uint8_t(oct.y * 255);
}

void Vertex::PackColor(glm::vec3 c)
{
	color.r = uint8_t(c.x * 255);
	color.g = uint8_t(c.y * 255);
	color.b = uint8_t(c.z * 255);
}

bool Mesh::LoadFromMeshAsset(const char* filename)
{
	assets::AssetFile file;
	bool loaded = assets::LoadBinaryFile(filename, file);

	if (!loaded)
	{
		LOG_ERROR("Error when loading mesh {}", filename);
		return false;
	}

	assets::MeshInfo meshInfo = assets::ReadMeshInfo(&file);

	std::vector<char> vertexBuffer;
	std::vector<char> indexBuffer;

	vertexBuffer.resize(meshInfo.vertexBufferSize);
	indexBuffer.resize(meshInfo.indexBufferSize);

	assets::UnpackMesh(&meshInfo, file.binaryBlob.data(), file.binaryBlob.size(), vertexBuffer.data(), indexBuffer.data());

	bounds.FromMeshBound(meshInfo.bounds);

	vertices.clear();
	indices.clear();

	indices.resize(indexBuffer.size() / sizeof(uint32_t));
	memcpy(indices.data(), indexBuffer.data(), indexBuffer.size());
	/*for (int i = 0; i < indices.size(); ++i)
	{
		uint32_t* unpackedIndices = (uint32_t*)indexBuffer.data();
		indices[i] = unpackedIndices[i];
	}*/

	if (meshInfo.vertexFormat == assets::VertexFormat::PNCV_F32)
	{
		assets::Vertex_f32_PNCV* unpackedVertices = (assets::Vertex_f32_PNCV*)vertexBuffer.data();
		vertices.resize(vertexBuffer.size() / sizeof(assets::Vertex_f32_PNCV));

		for (int i = 0; i < vertices.size(); ++i)
		{
			vertices[i].position.x = unpackedVertices[i].position[0];
			vertices[i].position.y = unpackedVertices[i].position[1];
			vertices[i].position.z = unpackedVertices[i].position[2];

			vertices[i].PackNormal(glm::vec3(unpackedVertices[i].normal[0], 
				unpackedVertices[i].normal[1], 
				unpackedVertices[i].normal[2]));
			vertices[i].PackColor(glm::vec3(unpackedVertices[i].color[0],
				unpackedVertices[i].color[1],
				unpackedVertices[i].color[2]));

			vertices[i].uv.x = unpackedVertices[i].uv[0];
			vertices[i].uv.y = unpackedVertices[i].uv[1];
		}
	}
	else if (meshInfo.vertexFormat == assets::VertexFormat::P32N8C8V16)
	{
		assets::Vertex_P32N8C8V16* unpackedVertices = (assets::Vertex_P32N8C8V16*)vertexBuffer.data();
		vertices.resize(vertexBuffer.size() / sizeof(assets::Vertex_P32N8C8V16));

		for (int i = 0; i < vertices.size(); ++i)
		{
			vertices[i].position.x = unpackedVertices[i].position[0];
			vertices[i].position.y = unpackedVertices[i].position[1];
			vertices[i].position.z = unpackedVertices[i].position[2];

			vertices[i].PackNormal(glm::vec3(unpackedVertices[i].normal[0],
				unpackedVertices[i].normal[1],
				unpackedVertices[i].normal[2]));
			vertices[i].color.x = unpackedVertices[i].color[0];
			vertices[i].color.y = unpackedVertices[i].color[1];
			vertices[i].color.z = unpackedVertices[i].color[2];

			vertices[i].uv.x = unpackedVertices[i].uv[0];
			vertices[i].uv.y = unpackedVertices[i].uv[1];
		}
	}
	
	LOG_SUCCESS("Loaded mesh {} : Verts {}, tris = {}", filename, vertices.size(), indices.size() / 3);
	return true;
}

void RenderBounds::FromMeshBound(assets::MeshBounds& meshBounds)
{
	extents.x = meshBounds.extents[0];
	extents.y = meshBounds.extents[1];
	extents.z = meshBounds.extents[2];

	origin.x = meshBounds.origin[0];
	origin.y = meshBounds.origin[1];
	origin.z = meshBounds.origin[2];

	radius = meshBounds.radius;
	valid = true;
}
