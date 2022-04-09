#include <mesh_asset.h>
#include <json.hpp>
#include <lz4.h>

using nlohmann::json;

static const char* s_kVertexBufferSize = "vertex_buffer_size";
static const char* s_kIndexBufferSize = "index_buffer_size";
static const char* s_kIndexSize = "index_size";
static const char* s_kOriginalFile = "original_file";
static const char* s_kCompression = "compression";
static const char* s_kBounds = "bounds";
static const char* s_kVertexForamt = "vertex_format";

static const char* s_FormatNames[] = {
	"None",
	"PNCV_F32",
	"P32N8C8V16",
};

assets::VertexFormat parse_format(const char* f)
{
	for (int i = 1; i < (int)assets::VertexFormat::Count; ++i)
	{
		if (strcmp(f, s_FormatNames[i]) == 0)
		{
			return (assets::VertexFormat)i;
		}
	}
	return assets::VertexFormat::Unknown;
}


assets::MeshInfo assets::read_mesh_info(AssetFile* file)
{
	MeshInfo info;

	json metadata = json::parse(file->json);

	info.vertexBufferSize = metadata[s_kVertexBufferSize];
	info.indexBufferSize = metadata[s_kIndexBufferSize];
	info.indexSize = (uint8_t)metadata[s_kIndexSize];
	info.originalFile = metadata[s_kOriginalFile];

	std::string compressionString = metadata[s_kCompression];
	info.compressionMode = parse_compression(compressionString.c_str());

	std::vector<float> boundsData;
	boundsData.reserve(7);
	boundsData = metadata[s_kBounds].get<std::vector<float>>();

	info.bounds.FromFloatArray(boundsData);

	std::string vertexFormat = metadata[s_kVertexForamt];
	info.vertexFormat = parse_format(vertexFormat.c_str());
	return info;
}

void assets::unpack_mesh(MeshInfo* info, const char* sourceBuffer, size_t sourceSize, char* vertexBuffer, char* indexBuffer)
{
	std::vector<char> decompressedBuffer;
	decompressedBuffer.reserve(info->vertexBufferSize + info->indexBufferSize);

	LZ4_decompress_safe(sourceBuffer, decompressedBuffer.data(), static_cast<int>(sourceSize), static_cast<int>(decompressedBuffer.size()));

	memcpy(vertexBuffer, decompressedBuffer.data(), info->vertexBufferSize);

	memcpy(indexBuffer, decompressedBuffer.data() + info->vertexBufferSize, info->indexBufferSize);
}

assets::AssetFile assets::pack_mesh(MeshInfo* info, char* vertexData, char* indexData)
{
	AssetFile file;
	file.type[0] = 'M';
	file.type[1] = 'E';
	file.type[2] = 'S';
	file.type[3] = 'H';
	file.version = 1;

	json metadata;
	metadata[s_kVertexForamt] = s_FormatNames[(int)info->vertexFormat];
	metadata[s_kVertexBufferSize] = info->vertexBufferSize;
	metadata[s_kIndexBufferSize] = info->indexBufferSize;
	metadata[s_kOriginalFile] = info->originalFile;

	std::vector<float> boundsData;
	info->bounds.ToFloatArray(boundsData);
	metadata[s_kBounds] = boundsData;

	size_t fullsize = info->vertexBufferSize + info->indexBufferSize;
	std::vector<char> mergedBuffer;
	mergedBuffer.resize(fullsize);
	memcpy(mergedBuffer.data(), vertexData, info->vertexBufferSize);
	memcpy(mergedBuffer.data() + info->vertexBufferSize, indexData, info->indexBufferSize);

	size_t compressStaging = LZ4_compressBound(static_cast<int>(fullsize));
	file.binaryBlob.reserve(compressStaging);
	int compressedSize = LZ4_compress_default(mergedBuffer.data(), file.binaryBlob.data(), static_cast<int>(mergedBuffer.size()), static_cast<int>(compressStaging));

	file.binaryBlob.reserve(compressedSize);
	metadata[s_kCompression] = "LZ4";

	file.json = metadata.dump();

	return file;
}

assets::MeshBounds assets::calculate_bounds(Vertex_f32_PNCV* vertices, size_t count)
{
	MeshBounds bounds;
	float min[3] = { std::numeric_limits<float>::max(),std::numeric_limits<float>::max() ,std::numeric_limits<float>::max() };
	float max[3] = { std::numeric_limits<float>::min(),std::numeric_limits<float>::min() ,std::numeric_limits<float>::min() };

	for (int i = 0; i < count; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			min[j] = std::min(min[j], vertices[i].position[j]);
			max[j] = std::max(max[j], vertices[i].position[j]);
		}
	}

	for (int j = 0; j < 3; ++j)
	{
		bounds.extents[j] = (max[j] - min[j]) * 0.5f;
		bounds.origin[j] = min[j] + bounds.extents[j];
	}
	
	//calculate bounding sphere radius
	float r2 = 0;

	for (int i = 0; i < count; ++i)
	{
		float distanceSqr = 0;
		for (int j = 0; j < 3; ++j)
		{
			float offset = vertices[i].position[j] = bounds.origin[j];
			distanceSqr += offset * offset;
		}
		r2 = std::max(r2, distanceSqr);
	}
	bounds.radius = std::sqrt(r2);

	return bounds;
}

void assets::MeshBounds::FromFloatArray(const std::vector<float>& floatArray)
{
	origin[0] = floatArray[0];
	origin[1] = floatArray[1];
	origin[2] = floatArray[2];

	radius = floatArray[3];

	extents[0] = floatArray[4];
	extents[1] = floatArray[5];
	extents[2] = floatArray[6];
}

void assets::MeshBounds::ToFloatArray(std::vector<float>& floatArray)
{
	floatArray.resize(7); 
	floatArray[0] = origin[0];
	floatArray[1] = origin[1];
	floatArray[2] = origin[2];

	floatArray[3] = radius;

	floatArray[4] = extents[0];
	floatArray[5] = extents[1];
	floatArray[6] = extents[2];
}
