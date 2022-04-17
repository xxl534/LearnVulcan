#include <prefab_asset.h>
#include <json.hpp>

using nlohmann::json;
static const char* s_kNodeMatrices = "node_matrices";
static const char* s_kNodeNames = "node_names";
static const char* s_kNodeParents = "node_parents";
static const char* s_kNodeMeshes = "node_meshes";
static const char* s_kMeshPath = "mesh_path";
static const char* s_kMaterialPath = "material_path";

assets::PrefabInfo assets::ReadPrefabInfo(AssetFile* file)
{
	PrefabInfo info;
	json metadata = json::parse(file->json);

	for (auto& [key, value] : metadata[s_kNodeMatrices].items())
	{
		info.node_matrices[value[0]] = value[1];
	}
	for (auto& [key, value] : metadata[s_kNodeNames].items())
	{
		info.node_names[value[0]] = value[1];
	}
	for (auto& [key, value] : metadata[s_kNodeParents].items())
	{
		info.node_parents[value[0]] = value[1];
	}

	std::unordered_map<uint64_t, json> meshnodes = metadata[s_kNodeMeshes];
	for (auto pair : meshnodes)
	{
		assets::PrefabInfo::NodeMesh node;
		node.mesh_path = pair.second[s_kMeshPath];
		node.material_path = pair.second[s_kMaterialPath];
		info.node_meshes[pair.first] = node;
	}

	size_t nMaterices = file->binaryBlob.size() / (sizeof(float) * 16);
	info.matrices.resize(nMaterices);
	memcpy(info.matrices.data(), file->binaryBlob.data(), file->binaryBlob.size());

	return info;
}

assets::AssetFile assets::pack_prefab(const PrefabInfo& info)
{
	json metadata;
	metadata[s_kNodeMatrices] = info.node_matrices;
	metadata[s_kNodeNames] = info.node_names;
	metadata[s_kNodeParents] = info.node_parents;

	std::unordered_map<uint64_t, json> meshnodes;
	for (auto pair : info.node_meshes)
	{
		json node;
		node[s_kMeshPath] = pair.second.mesh_path;
		node[s_kMaterialPath] = pair.second.material_path;
		meshnodes[pair.first] = node;
	}

	metadata[s_kNodeMeshes] = meshnodes;

	AssetFile file;
	file.type[0] = 'P';
	file.type[1] = 'R';
	file.type[2] = 'F';
	file.type[3] = 'B';

	file.binaryBlob.resize(info.matrices.size() * sizeof(float) * 16);
	memcpy(file.binaryBlob.data(), info.matrices.data(), info.matrices.size() * sizeof(float) * 16);

	std::string jsonstr = metadata.dump();
	file.json = jsonstr;
	return file;
}
