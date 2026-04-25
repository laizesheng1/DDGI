#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D debugTexture;

layout(push_constant) uniform TextureDebugPushConstants {
    vec4 visualizeModeAndScale;
} pushConstants;

void main()
{
    vec4 sampled = texture(debugTexture, inUV);
    if (pushConstants.visualizeModeAndScale.x < 0.5) {
        outColor = vec4(sampled.rgb, 1.0);
        return;
    }

    float scalar = clamp(sampled.r * pushConstants.visualizeModeAndScale.y, 0.0, 1.0);
    outColor = vec4(vec3(scalar), 1.0);
}
