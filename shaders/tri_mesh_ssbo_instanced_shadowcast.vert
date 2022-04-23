#version 450 
layout(location = 0) in vec3 vPosition;
layout(location = 1) in vec2 vOctNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in vec2 vTexCoord;

layout(location = 0 ) out vec3 outColor;

layout(set = 0, binding = 0) uniform CameraBuffer{
    mat4 view ;
    mat4 proj;
    mat4 viewproj;
}cameraData;

struct ObjectData{
    mat4 model;
    vec4 sphereBounds;
    vec4 extents;
};

layout(std140, set = 1, binding = 0) readonly buffer ObjectBuffer{
    ObjectData objects[];
} objectBuffer;

layout(set = 1, binding = 1) readonly buffer InstanceBuffer{
    uint ids[];
} InstanceBuffer;

void main()
{
    uint index = InstanceBuffer.Ids[gl_InstanceIndex];

    mat4 modelMatrix = objectBuffer.object[index].model;
    mat4 transformMatrix = (cameraData.viewproj * modelMatrix);
    gl_Position = transformMatrix * vec4(vPosition, 1.0f);
}
