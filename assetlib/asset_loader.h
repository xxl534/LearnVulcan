#pragma once
#include <vector>
#include <string>
#include <unordered_map>

namespace assets
{
	struct AssetFile {
		char type[4];
		int version;
		std::string json;
		std::vector<char> binaryBlob;
	};

	enum class CompressionMode : uint32_t {
		None,
		LZ4,
	};

	bool save_binaryFile(const char* path, const AssetFile& file);

	bool LoadBinaryFile(const char* path, AssetFile& outputFile);

	assets::CompressionMode parse_compression(const char* f);
}