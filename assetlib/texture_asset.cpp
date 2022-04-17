#include <texture_asset.h>
#include <json.hpp>
#include <lz4.h>

using nlohmann::json;

static const char* s_kFormat = "format";
static const char* s_kCompression = "compression";
static const char* s_kBufferSize = "buffer_size";
static const char* s_kOriginalFile = "original_file";
static const char* s_kCompressedSize = "compressed_size";
static const char* s_kOriginalSize = "original_size";
static const char* s_kWidth = "width";
static const char* s_kHeight = "height";
static const char* s_kPage = "pages";
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

    std::string formatString = texture_metadata[s_kFormat];
    info.textureFormat = parse_format(formatString.c_str());

    std::string compressionString = texture_metadata[s_kCompression];
    info.compressionMode = parse_compression(compressionString.c_str());

    info.textureSize = texture_metadata[s_kBufferSize];
    info.originalFile = texture_metadata[s_kOriginalFile];

    for (auto& [key, value] : texture_metadata[s_kPage].items())
    {
        PageInfo page;

        page.compressedSize = value[s_kCompressedSize];
        page.originalSize = value[s_kOriginalSize];
        page.width = value[s_kWidth];
        page.height = value[s_kHeight];

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

        //if the compression is more than 80% of the originalObjectId size, its not worth to use it
        if (compression_rate > 0.8)
        {
            compressedSize = p.originalSize;
            page_buffer.resize(compressedSize);
            memcpy(page_buffer.data(), pixels, compressedSize);
        }
        else
        {
            page_buffer.resize(compressedSize);
        }
        p.compressedSize = compressedSize;
        file.binaryBlob.insert(file.binaryBlob.end(), page_buffer.begin(), page_buffer.end());

        pixels += p.originalSize;
    }

    json texture_metadata;
    texture_metadata[s_kFormat] = "RGBA8";

    texture_metadata[s_kBufferSize] = info->textureSize;
    texture_metadata[s_kOriginalFile] = info->originalFile;
    texture_metadata[s_kCompression] = "LZ4";

    std::vector<json> page_json;
    for (auto& p : info->pages)
    {
        json page;
        page[s_kCompressedSize] = p.compressedSize;
        page[s_kOriginalSize] = p.originalSize;
        page[s_kWidth] = p.width;
        page[s_kHeight] = p.height;
        page_json.push_back(page);
    }
    texture_metadata[s_kPage] = page_json;

    std::string jsonstr = texture_metadata.dump();
    file.json = jsonstr;

    return file;
}
