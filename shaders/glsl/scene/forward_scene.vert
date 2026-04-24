#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(push_constant) uniform ScenePushConstants {
    mat4 viewProjection;
} pushConstants;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormalWorld;
layout(location = 2) out vec4 outColor;

void main()
{
    // Vertices are loaded with PreTransformVertices, so inPosition is already
    // in scene/world space. Keeping the shader model-free avoids applying the
    // glTF node matrix twice during this bring-up pass.
    gl_Position = pushConstants.viewProjection * vec4(inPosition, 1.0);
    outUV = inUV;
    outNormalWorld = normalize(inNormal);
    outColor = inColor;
}
