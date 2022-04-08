#include "texture_asset.h"
#include <json.hpp>
#include <lz4.h>

typedef nlohmann::json json;
assets::TextureFormat parse_format(const char* f)
{
    if (strcmp(f, "RGBA8") == 0)
    {
        return assets::TextureFormat::RGBA8;
    }
    else
    {
        return assets::TextureFormat::Unknown;
    }
}
assets::TextureInfo assets::read_texture_info(AssetFile* file)
{
    TextureInfo info;

    json texture_metadata = json::parse(file->json);

    std::string formatString = texture_metadata["format"];
    info.textureFormat = parse_format(formatString.c_str());

    std::string compressionString = texture_metadata["compression"];
    info.compressionMode = parse_compression(compressionString.c_str());

    info.textureSize = texture_metadata["buffer_size"];
    info.originalFile = texture_metadata["original_file"];

    for (auto& [key, value] : texture_metadata["pages"].items())
    {
        PageInfo page;

        page.compressedSize = value["compressed_size"];
        page.originalSize = value["original_size"];
        page.width = value["width"];
        page.height = value["height"];

        info.pages.push_back(page);
    }

    return info;
}

void assets::unpack_texture(TextureInfo* info, const char* sourceBuffer, size_t sourceSize, char* destination)
{
    if (info->compressionMode == CompressionMode::LZ4)
    {
        for (auto& page : info->pages)
        {
            LZ4_decompress_safe(sourceBuffer, destination, page.compressedSize, page.originalSize);
            sourceBuffer += page.compressedSize;
            destination += page.originalSize;
        }
    }
    else
    {
        memcpy(destination, sourceBuffer, sourceSize);
    }
}

void assets::unpack_texture_page(TextureInfo* info, int pageIndex, char* sourceBuffer, char* destination)
{
    char* source = sourceBuffer;
    for (int i = 0; i < pageIndex; ++i)
    {
        source += info->pages[i].compressedSize;
    }

    if (info->compressionMode == CompressionMode::LZ4)
    {
        if (info->pages[pageIndex].compressedSize != info->pages[pageIndex].originalSize)
        {
            LZ4_decompress_safe(source, destination, info->pages[pageIndex].compressedSize, info->pages[pageIndex].originalSize);
        }
        else
        {
            memcpy(destination, source, info->pages[pageIndex].originalSize);
        }
    }
    else
    {
        memcpy(destination, source, info->pages[pageIndex].originalSize);
    }
}

assets::AssetFile assets::pack_texture(TextureInfo* info, void* pixelData)
{
    AssetFile file;
    
    file.type[0] = 'T';
    file.type[1] = 'E';
    file.type[2] = 'X';
    file.type[3] = 'I';

    file.version = 1;

    char* pixels = (char*)pixelData;
    std::vector<char> page_buffer;

    for (auto& p : info->pages)
    {
        page_buffer.resize(p.originalSize);

        int compressStaging = LZ4_compressBound(p.originalSize);

        page_buffer.resize(compressStaging);

        int compressedSize = LZ4_compress_default(pixels, page_buffer.data(), p.originalSize, compressStaging);

        float compression_rate = float(compressedSize) / float(info->textureSize);
    }

    return assets::AssetFile();
}
