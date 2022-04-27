#include <asset_loader.h>

#include <fstream>
#include <iostream>

bool assets::SaveBinaryFile(const char* path, const AssetFile& file)
{
	std::ofstream outfile;
	outfile.open(path, std::ios::binary | std::ios::out);
	if (!outfile.is_open())
	{
		std::cout << "Error when trying to write file :" << path << std::endl;
		return false;
	}
	outfile.write(file.type, 4);

	uint32_t version = file.version;
	outfile.write((const char*)&version, sizeof(uint32_t));

	uint32_t length = static_cast<uint32_t>(file.json.size());
	outfile.write((const char*)&length, sizeof(uint32_t));

	uint32_t bloblength = static_cast<uint32_t>(file.binaryBlob.size());
	outfile.write((const char*)&bloblength, sizeof(uint32_t));

	outfile.write(file.json.data(), length);

	outfile.write(file.binaryBlob.data(), file.binaryBlob.size());

	outfile.close();

	return true;
}

bool assets::LoadBinaryFile(const char* path, AssetFile& outputFile)
{
	std::ifstream infile;
	infile.open(path, std::ios::binary);

	if(!infile.is_open())
		return false;

	infile.seekg(0);

	infile.read(outputFile.type, 4);

	infile.read((char*)&outputFile.version, sizeof(uint32_t));

	uint32_t jsonlen = 0;
	infile.read((char*)&jsonlen, sizeof(uint32_t));

	uint32_t blobLength = 0;
	infile.read((char*)&blobLength, sizeof(uint32_t));

	outputFile.json.resize(jsonlen);
	infile.read(outputFile.json.data(), jsonlen);

	outputFile.binaryBlob.resize(blobLength);
	infile.read(outputFile.binaryBlob.data(), blobLength);

	return true;
}

assets::CompressionMode assets::parse_compression(const char* f)
{
	if (strcmp(f, "LZ4") == 0)
	{
		return assets::CompressionMode::LZ4;
	}
	else
	{
		return assets::CompressionMode::None;
	}
}
