#pragma once

#include <asset_loader.h>

namespace assets {
	struct Vertex_f32_PNCV
	{
		float position[3];
		float normal[3];
		float color[4];
		float uv[2];
	};

	struct Vertex_P32N8C8V16
	{
		float position[3];
		uint8_t normal[3];
		uint8_t color[4];
		float uv[2];
	};

	enum class VertexFormat : uint32_t {
		Unknown = 0,
		PNCV_F32,	//everything at 32 bits
		P32N8C8V16,	//position at 32 bits, octNormal at 8 bits, color at 8 bits, uvs at 16bits float
		Count,
	};

	struct MeshBounds {
		float origin[3];
		float radius;
		float extents[3];

		void FromFloatArray(const std::vector<float>& floatArray);
		void ToFloatArray(std::vector<float>& floatArray);
	};

	struct MeshInfo {
		uint64_t vertexBufferSize;
		uint64_t indexBufferSize;
		MeshBounds bounds;
		VertexFormat vertexFormat;
		char indexSize;
		CompressionMode compressionMode;
		std::string originalFile;
	};

	MeshInfo ReadMeshInfo(AssetFile* file);

	void UnpackMesh(MeshInfo* info, const char* sourceBuffer, size_t sourceSize, char* vertexBuffer, char* indexBuffer);

	AssetFile pack_mesh(MeshInfo* info, char* vertexData, char* indexData);

	MeshBounds CalculateBounds(Vertex_f32_PNCV* vertices, size_t count);
}