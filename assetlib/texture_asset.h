#pragma once
#include <asset_loader.h>

namespace assets {
	enum class TextureFormat : uint32_t
	{
		Unknown = 0,
		RGBA8,
	};

	struct PageInfo {
		uint32_t width;
		uint32_t height;
		uint32_t compressedSize;
		uint32_t originalSize;
	};

	struct TextureInfo
	{
		uint64_t textureSize;
		TextureFormat textureFormat;
		CompressionMode compressionMode;

		std::string originalFile;
		std::vector<PageInfo> pages;
	};

	TextureInfo ReadTextureInfo(AssetFile* file);

	void unpack_texture(TextureInfo* info, const char* sourceBuffer, size_t sourceSize, char* destination);

	void unpack_texture_page(TextureInfo* info, int pageIndex, char* sourceBuffer, char* destination);

	AssetFile pack_texture(TextureInfo* info, void* pixelData);
}