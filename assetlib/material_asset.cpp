#include <material_asset.h>
#include <lz4.h>
#include <json.hpp>

using nlohmann::json;

static const char* s_kBaseEffect = "base_effect";
static const char* s_kTextures = "texture";
static const char* s_kCustomProperties = "custom_properties";
static const char* s_kTransparency = "transparency";

static const char* s_TransparenyModeName[] = {
	"Opaque",
	"Transparent",
	"Masked",
};

assets::TransparencyMode parse_transparency(const char* s)
{
	for (int i = 0; i < (int)(assets::TransparencyMode::Count); ++i)
	{
		if (std::strcmp(s, s_TransparenyModeName[i]) == 0)
		{
			return assets::TransparencyMode(i);
		}
	}
	return assets::TransparencyMode::Opaque;
}

assets::MaterialInfo assets::read_material_info(AssetFile* file)
{
	assets::MaterialInfo info;

	json metadata = json::parse(file->json);
	info.baseEffect = metadata[s_kBaseEffect];

	for (auto& [key, value] : metadata[s_kTextures].items())
	{
		info.textures[key] = value;
	}
	for (auto& [key, value] : metadata[s_kCustomProperties].items())
	{
		info.customProperties[key] = value;
	}

	std::string transparencyName = metadata[s_kTransparency];
	info.transparency = parse_transparency(transparencyName.c_str());

	return info;
}

assets::AssetFile assets::pack_material(MaterialInfo* info)
{
	json metadata;
	metadata[s_kBaseEffect] = info->baseEffect;
	metadata[s_kTextures] = info->textures;
	metadata[s_kCustomProperties] = info->customProperties;

	metadata[s_kTransparency] = s_TransparenyModeName[(int)info->transparency];

	AssetFile file;
	file.type[0] = 'M';
	file.type[1] = 'A';
	file.type[2] = 'T';
	file.type[3] = 'X';

	std::string jsonstr = metadata.dump();
	file.json = jsonstr;

	return file;
}
