#pragma once

#include <asset_loader.h>

namespace assets
{
	enum class TransparencyMode :uint8_t {
		Opaque,
		Transparent,
		Masked,
		Count,
	};

	struct MaterialInfo {
		std::string baseEffect;
		std::unordered_map<std::string, std::string> textures;
		std::unordered_map<std::string, std::string> customProperties;
		TransparencyMode transparency;
	};

	MaterialInfo read_material_info(AssetFile* file);

	AssetFile pack_material(MaterialInfo* info);
}