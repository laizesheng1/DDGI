#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(push_constant) uniform ScenePushConstants {
    mat4 viewProjection;
} pushConstants;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormalWorld;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outColor;

void main()
{
    // PreTransformVertices is enabled in the loader, so positions already live
    // in scene/world space. The GBuffer stores them directly to keep the later
    // lighting pass simple and deterministic during bring-up.
    gl_Position = pushConstants.viewProjection * vec4(inPosition, 1.0);
    outWorldPosition = inPosition;
    outNormalWorld = normalize(inNormal);
    outUV = inUV;
    outColor = inColor;
}
