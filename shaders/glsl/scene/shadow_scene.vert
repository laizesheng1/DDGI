#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightViewProjection;
} pushConstants;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

void main()
{
    gl_Position = pushConstants.lightViewProjection * vec4(inPosition, 1.0);
    outUV = inUV;
    outColor = inColor;
}
