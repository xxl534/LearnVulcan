#pragma once
#include <vk_types.h>
#include <material_system.h>
#include <glm/glm.hpp>
#include <vk_mesh.h>

struct MeshObject;
struct GPUObjectData;

template<typename T>
struct Handle {
	uint32_t handle;
};

struct Mesh;

struct DrawMesh {
	uint32_t firstVertex;
	uint32_t firstIndex;
	uint32_t indexCount;
	uint32_t vertexCount;
	bool isMerged;

	Mesh* original;
};

struct GPUInstance {
	uint32_t objectId;
	uint32_t batchId;
};

struct GPUIndirectObject {
	VkDrawIndexedIndirectCommand command;
	uint32_t objectId;
	uint32_t batchId;
};

struct RenderObject {
	Handle<DrawMesh> drawMeshId;
	Handle<vkutil::Material> materialId;

	uint32_t updateIndex;
	uint32_t customSortKey{ 0 };

	vkutil::PerPassData<uint32_t> passIndices;

	glm::mat4 transformMatrix;
	RenderBounds bounds;
};

class RenderScene {
public:
	struct Multibatch {
		uint32_t first;
		uint32_t count;
	};
	struct PassMaterial {
		VkDescriptorSet materialSet;
		vkutil::ShaderPass* shaderPass;
		bool operator==(const PassMaterial& other) const {
			return materialSet == other.materialSet && shaderPass == other.shaderPass;
		}
	};
	struct IndirectBatch {
		Handle<DrawMesh> meshId;
		PassMaterial material;
		uint32_t first;
		uint32_t count;
	};
	struct PassObject {
		PassMaterial material;
		Handle<DrawMesh> meshId;
		Handle<RenderObject> originalObjectId;
		int32_t builtbatch;
		uint32_t customKey;
	};
	struct RenderBatch {
		Handle<PassObject> object;
		uint64_t sortKey;

		bool operator==(const RenderBatch& other) const
		{
			return object.handle == other.object.handle && sortKey == other.sortKey;
		}
	};
	struct MeshPass {
		std::vector<RenderScene::Multibatch> multibatches;

		std::vector<RenderScene::IndirectBatch> indirectBatches;

		std::vector<Handle<RenderObject>> unbatchedRenderObjectIds;

		std::vector<RenderScene::RenderBatch> flatRenderBatches;

		std::vector<PassObject> passObjects;

		std::vector<Handle<PassObject>> reusablePassObjectIds;

		std::vector<Handle<PassObject>> passObjectsToDelete;

		AllocatedBuffer<uint32_t> compactedInstanceBuffer;
		AllocatedBuffer<GPUInstance> passObjectsBuffer;

		AllocatedBuffer<GPUIndirectObject> drawIndirectBuffer;
		AllocatedBuffer<GPUIndirectObject> clearIndirectBuffer;

		PassObject* Get(Handle<PassObject> handle);

		MeshpassType type;

		bool needsIndirectRefresh = true;
		bool needsInstanceRefresh = true;
	};

	void Init();

	Handle<RenderObject> RegisterObject(MeshObject* object);

	void RegisterObjectBatch(MeshObject* first, uint32_t count);

	void UpdateTransform(Handle<RenderObject> objectId, const glm::mat4& localToWorld);
	void UpdateObject(Handle<RenderObject> objectId);

	void FillObjectData(GPUObjectData* data);
	void FillIndirectArray(GPUIndirectObject* data, MeshPass& pass);
	void FillInstancesArray(GPUInstance* data, MeshPass& pass);

	void WriteObject(GPUObjectData* target, Handle<RenderObject> objectId);

	void ClearDirtyObjects();
	
	void BuildBatches();

	void MergeMeshes(class VulkanEngine* engine);

	void RefreshPass(MeshPass* pass);

	void BuildIndirectBatches(MeshPass* pass, std::vector<IndirectBatch>& outBatches, std::vector<RenderScene::RenderBatch>& inObjects);
	RenderObject* GetObject(Handle<RenderObject> objectId);
	DrawMesh* GetMesh(Handle<DrawMesh> objectId);
	vkutil::Material* GetMaterial(Handle<vkutil::Material> id);

	std::vector<RenderObject> renderables;
	std::vector<DrawMesh> meshes;
	std::vector<vkutil::Material*> materials;

	std::vector<Handle<RenderObject>> dirtyObjects;

	MeshPass& GetMeshPass(MeshpassType type);

	vkutil::PerPassData<MeshPass> m_Passes;

	std::unordered_map<vkutil::Material*, Handle<vkutil::Material>> materialConvert;
	std::unordered_map<Mesh*, Handle<DrawMesh>> meshConvert;

	Handle<vkutil::Material> GetMaterialHandle(vkutil::Material* m);
	Handle<DrawMesh> GetMeshHandle(Mesh* m);

	AllocatedBuffer<Vertex> mergedVertexBuffer;
	AllocatedBuffer<uint32_t> mergedIndexBuffer;

	AllocatedBuffer<GPUObjectData> objectDataBuffer;
};