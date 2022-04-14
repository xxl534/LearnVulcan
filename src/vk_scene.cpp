#include <vk_scene.h>
#include <vk_engine.h>

void RenderScene::Init()
{
    m_Passes[MeshpassType::Forward].type = MeshpassType::Forward;
    m_Passes[MeshpassType::DirectionalShadow].type = MeshpassType::DirectionalShadow;
    m_Passes[MeshpassType::Transparency].type = MeshpassType::Transparency;
}

Handle<RenderObject> RenderScene::RegisterObject(MeshObject* object)
{
    RenderObject newObject;
    newObject.bounds = object->bounds;
    newObject.transformMatrix = object->transformMatrix;
    newObject.materialId = GetMaterialHandle(object->material);
    newObject.meshID = GetMeshHandle(object->mesh);
    newObject.updateIndex = (uint32_t)-1;
    newObject.customSortKey = object->customSortKey;
    newObject.passIndices.clear(-1);
    Handle<RenderObject> handle;
    handle.handle = static_cast<uint32_t>(renderables.size());
    renderables.push_back(newObject);

    if (object->bDrawForwardPass)
    {
        if (object->material->original->passShaders[MeshpassType::Transparency])
        {
            m_Passes[MeshpassType::Transparency].unbatchedObject.push_back(handle);
        }
        if (object->material->original->passShaders[MeshpassType::Forward])
        {
            m_Passes[MeshpassType::Forward].unbatchedObject.push_back(handle);
        }
    }
    if (object->bDrawShadowPass)
    {
        if (object->material->original->passShaders[MeshpassType::DirectionalShadow])
        {
            m_Passes[MeshpassType::DirectionalShadow].unbatchedObject.push_back(handle);
        }
    }

    UpdateObject(handle);
    return handle;
}

void RenderScene::RegisterObjectBatch(MeshObject* first, uint32_t count)
{
    renderables.reserve(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        RegisterObject(&first[i]);
    }
}

void RenderScene::UpdateTransform(Handle<RenderObject> objectId, const glm::mat4& localToWorld)
{
    GetObject(objectId)->transformMatrix = localToWorld;
    UpdateObject(objectId);
}

void RenderScene::UpdateObject(Handle<RenderObject> objectId)
{
    RenderObject* pObject = GetObject(objectId);
    auto& passIndices = pObject->passIndices;
    for (int i = 0; i < (int)MeshpassType::Count; ++i)
    {
        MeshpassType passType = MeshpassType(i);
        if (passIndices[passType] != -1)
        {
            Handle<PassObject> passObjectiId;
            passObjectiId.handle = passIndices[passType];
            m_Passes[passType].objectsToDelete.push_back(passObjectiId);
            m_Passes[passType].unbatchedObject.push_back(objectId);

            passIndices[passType] = -1;
        }
    }
    if (pObject->updateIndex == (uint32_t)-1)
    {
        pObject->updateIndex = static_cast<uint32_t>(dirtyObjects.size());
        dirtyObjects.push_back(objectId);
    }
}

void RenderScene::FillObjectData(GPUObjectData* data)
{

}

void RenderScene::FillIndirectArray(GPUIndirectObject* data, MeshPass& pass)
{
}

void RenderScene::FillInstancesArray(GPUInstance* data, MeshPass& pass)
{
}

void RenderScene::WriteObject(GPUObjectData* target, Handle<RenderObject> objectId)
{
}

void RenderScene::ClearDirtyObjects()
{
}

void RenderScene::BuildBatches()
{
}

void RenderScene::MergeMeshes(VulkanEngine* engine)
{
}

void RenderScene::RefreshPass(MeshPass* pass)
{
}

void RenderScene::BuildIndirectBatches(MeshPass* pass, std::vector<IndirectBatch>& outBatches, std::vector<RenderScene::RenderBatch>& inObjects)
{
}

RenderObject* RenderScene::GetObject(Handle<RenderObject> objectId)
{
    return &renderables[objectId.handle];
}

DrawMesh* RenderScene::GetMesh(Handle<DrawMesh> objectId)
{
    return &meshes[objectId.handle];
}

vkutil::Material* RenderScene::GetMaterial(Handle<vkutil::Material> id)
{
    return materials[id.handle];
}

RenderScene::MeshPass* RenderScene::GetMeshPass(MeshpassType type)
{
    return &m_Passes[type];
}

Handle<vkutil::Material> RenderScene::GetMaterialHandle(vkutil::Material* m)
{
    Handle<vkutil::Material> handle;
    auto it = materialConvert.find(m);
    if (it == materialConvert.end())
    {
        uint32_t index = static_cast<uint32_t>(materials.size());
        materials.push_back(m);

        handle.handle = index;
        materialConvert[m] = handle;
    }
    else
    {
        handle = (*it).second;
    }
    return handle;
}

Handle<DrawMesh> RenderScene::GetMeshHandle(Mesh* m)
{
    Handle<DrawMesh> handle;
    auto it = meshConvert.find(m);
    if (it == meshConvert.end())
    {
        uint32_t index = static_cast<uint32_t>(materials.size());
        DrawMesh drawMesh;
        drawMesh.original = m;
        drawMesh.firstIndex = 0;
        drawMesh.firstVertex = 0;
        drawMesh.vertexCount = static_cast<uint32_t>(m->vertices.size());
        drawMesh.indexCount = static_cast<uint32_t>(m->indices.size());

        meshes.push_back(drawMesh);

        handle.handle = index;
        meshConvert[m] = handle;
    }
    else
    {
        handle = (*it).second;
    }
    return handle;
}

RenderScene::PassObject* RenderScene::MeshPass::Get(Handle<PassObject> handle)
{
    return nullptr;
}
