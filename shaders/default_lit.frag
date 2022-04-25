#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec4 outFragColor;

layout(set = 0, binding = 1) uniform SceneData{
	vec4 fogColor;
	vec4 fogDistance;
	vec4 ambientColor;
	vec4 sunlightDirection;
	vec4 sunlightColor;
} sceneData;

void main()
{
    vec3 color = inColor.xyz;
    float lightAngle = clamp(dot(inNormal, sceneData.sunlightDirection.xyz), 0, 1);
    vec3 lightColor = sceneData.sunlightColor.xyz * lightAngle;

	outFragColor = vec4(lightColor * inColor + sceneData.ambientColor.xyz, 1.0f);
}