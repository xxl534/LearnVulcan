#include <vk_scene.h>
#include <vk_engine.h>
#include <Tracy.hpp>

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
    for (int i = 0; i < renderables.size(); ++i)
    {
        Handle<RenderObject> h;
        h.handle = i;
        WriteObject(data + i, h);
    }
}

void RenderScene::FillIndirectArray(GPUIndirectObject* data, MeshPass& pass)
{
    ZoneScopedNC("Fill Indirect", tracy::Color::Red);
    for (int i = 0; i < pass.batches.size(); ++i)
    {
        auto batch = pass.batches[i];

        data[i].command.firstInstance = batch.first;
        data[i].command.instanceCount = 0;
        data[i].command.firstIndex = GetMesh(batch.meshId)->firstIndex;
        data[i].command.vertexOffset = GetMesh(batch.meshId)->firstVertex;
        data[i].command.indexCount = GetMesh(batch.meshId)->indexCount;
        data[i].objectId = 0;
        data[i].batchId = i;
    }
}

void RenderScene::FillInstancesArray(GPUInstance* data, MeshPass& pass)
{
    ZoneScopedNC("Fill Instances", tracy::Color::Red);
    int dataIndex = 0;
    for (int i = 0; i < pass.batches.size(); ++i)
    {
        auto batch = pass.batches[i];

        for (int b = 0; b < batch.count; ++b)
        {
            data[dataIndex].objectId = pass.Get(pass.flatBatches[b + batch.first].object)->original.handle;
            data[dataIndex].batchId = i;
            ++dataIndex;
        }
    }
}

void RenderScene::WriteObject(GPUObjectData* target, Handle<RenderObject> objectId)
{
    RenderObject* renderObject = GetObject(objectId);
    GPUObjectData data;

    data.modelMatrix = renderObject->transformMatrix;
    data.originRadius = glm::vec4(renderObject->bounds.origin, renderObject->bounds.radius);
    data.extents = glm::vec4(renderObject->bounds.extents, renderObject->bounds.valid ? 1.f : 0.f);

    memcpy(target, &data, sizeof(GPUObjectData));
}

void RenderScene::ClearDirtyObjects()
{
    for (auto obj : dirtyObjects)
    {
        GetObject(obj)->updateIndex = (uint32_t)-1;
    }
    dirtyObjects.clear();
}

#include <future>
void RenderScene::BuildBatches()
{
    vkutil::PerPassData<std::future<void>> futures;
    for (int i = 0; i < (int)MeshpassType::Count; ++i)
    {
        futures[(MeshpassType)i] = std::async(std::launch::async, [&] { RefreshPass(&m_Passes[(MeshpassType)i]); });
    }
    
    for (int i = 0; i < (int)MeshpassType::Count; ++i)
    {
        futures[(MeshpassType)i].get();
    }
}

void RenderScene::MergeMeshes(VulkanEngine* engine)
{
    ZoneScopedNC("Mesh Merge", tracy::Color::Magenta);
    size_t totalVertices = 0;
    size_t totalIndices = 0;

    for (auto& mesh : meshes)
    {
        mesh.firstIndex = static_cast<uint32_t>(totalIndices);
        mesh.firstVertex = static_cast<uint32_t>(totalVertices);

        totalVertices += mesh.vertexCount;
        totalIndices += mesh.indexCount;

        mesh.isMerged = true;
    }

    mergedVertexBuffer = engine->create_buffer(totalVertices * sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    mergedIndexBuffer = engine->create_buffer(totalIndices * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    engine->immediate_submit([&](VkCommandBuffer cmd)
        {
            for (auto& mesh : meshes)
            {
                VkBufferCopy vertexCopy;
                vertexCopy.dstOffset = mesh.firstVertex * sizeof(Vertex);
                vertexCopy.size = mesh.vertexCount * sizeof(Vertex);
                vertexCopy.srcOffset = 0;

                vkCmdCopyBuffer(cmd, mesh.original->vertexBuffer.buffer, mergedVertexBuffer.buffer, 1, &vertexCopy);

                VkBufferCopy indexCopy;
                indexCopy.dstOffset = mesh.firstIndex * sizeof(uint32_t);
                indexCopy.size = mesh.indexCount * sizeof(uint32_t);
                indexCopy.srcOffset = 0;

                vkCmdCopyBuffer(cmd, mesh.original->indexBuffer.buffer, mergedIndexBuffer.buffer, 1, &indexCopy);
            }
        }
    );
}

void RenderScene::RefreshPass(MeshPass* pass)
{
    pass->needsIndirectRefresh = true;
    pass->needsInstanceRefresh = true;

    std::vector<uint32_t> newObjects;
    if (pass->objectsToDelete.size() > 0)
    {
        ZoneScopedNC("Delete objects", tracy::Color::Blue3);

        //create the render batches so that then we can do the deletion on the flat-array directly
        std::vector<RenderScene::RenderBatch> deletionBatch;
        deletionBatch.reserve(newObjects.size());

        for (auto i : pass->objectsToDelete)
        {
            pass->reusableObjects.push_back(i);
            RenderScene::RenderBatch newCmd;

            auto obj = pass->objects[i.handle];
            newCmd.object = i;

            uint64_t pipelineHash = std::hash<uint64_t>()(uint64_t(obj.material.shaderPass->pipeline));
            uint64_t setHash = std::hash<uint64_t>()((uint64_t)obj.material.materialSet);

            uint32_t matHash = static_cast<uint32_t>(pipelineHash ^ setHash);
            uint32_t meshMatHash = uint64_t(matHash) ^ uint64_t(obj.meshId.handle);

            newCmd.sortKey = uint64_t(meshMatHash) | (uint64_t(obj.customKey) << 32);

            pass->objects[i.handle].customKey = 0;
            pass->objects[i.handle].material.shaderPass = nullptr;
            pass->objects[i.handle].meshId.handle = -1;
            pass->objects[i.handle].original.handle = -1;

            deletionBatch.push_back(newCmd);
        }
        pass->objectsToDelete.clear();
        {
            ZoneScopedNC("Deletion Sort", tracy::Color::Blue1);
            std::sort(deletionBatch.begin(), deletionBatch.end(), [](const RenderScene::RenderBatch& a, const RenderScene::RenderBatch& b)
            {
                if (a.sortKey == b.sortKey)
                {
                    return a.object.handle < b.object.handle;
                }
                return a.sortKey < b.sortKey;
            });
        }

        {
            ZoneScopedNC("removal", tracy::Color::Blue1);

            std::vector<RenderScene::RenderBatch> newBatches;
            newBatches.reserve(pass->flatBatches.size());
            {
                ZoneScopedNC("Set Difference", tracy::Color::Red);

                std::set_difference()
            }

        }
    }
}

void RenderScene::BuildIndirectBatches(MeshPass* pass, std::vector<IndirectBatch>& outBatches, std::vector<RenderScene::RenderBatch>& inObjects)
{
    if (inObjects.size() == 0)
        return;

    ZoneScopedNC("Build Indirect Batches", tracy::Color::Blue);

    RenderScene::IndirectBatch newBatch;
    newBatch.first = 0;
    newBatch.count = 0;

    newBatch.material = pass->Get(inObjects[0].object)->material;
    newBatch.meshId = pass->Get(inObjects[0].object)->meshId;

    outBatches.push_back(newBatch);
    RenderScene::IndirectBatch* back = &outBatches.back();

    RenderScene::PassMaterial lastMat = pass->Get(inObjects[0].object)->material;
    for (int i = 0; i < inObjects.size(); ++i)
    {
        PassObject* obj = pass->Get(inObjects[i].object);

        bool bSameMesh = obj->meshId.handle == back->meshId.handle;
        bool bSameMaterial = obj->material == back->material;

        if (bSameMaterial && bSameMesh)
        {
            ++back->count;
        }
        else
        {
            newBatch.first = i;
            newBatch.count = i;
            newBatch.material = obj->material;
            newBatch.meshId = obj->meshId;

            outBatches.push_back(newBatch);
            back = &outBatches.back();
        }
    }
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
    return &objects[handle.handle];
}
