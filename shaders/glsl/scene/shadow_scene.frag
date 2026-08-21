#version 450

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;

layout(push_constant) uniform MaterialPushConstants {
    layout(offset = 64) vec4 baseColorFactor;
    layout(offset = 80) vec4 emissiveFactorAndAlphaCutoff;
    layout(offset = 96) vec4 metallicRoughnessOcclusionFlags;
    layout(offset = 112) vec4 normalScaleAndPadding;
} pushConstants;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

const uint MATERIAL_FLAG_ALPHA_MASK = 1u;
const uint MATERIAL_FLAG_ALPHA_BLEND = 2u;
const uint MATERIAL_FLAG_BASE_COLOR_TEXTURE = 8u;

void main()
{
    uint flags = uint(pushConstants.metallicRoughnessOcclusionFlags.w + 0.5);
    if ((flags & (MATERIAL_FLAG_ALPHA_MASK | MATERIAL_FLAG_ALPHA_BLEND)) == 0u) {
        return;
    }

    vec4 baseColor = pushConstants.baseColorFactor * inColor;
    if ((flags & MATERIAL_FLAG_BASE_COLOR_TEXTURE) != 0u) {
        baseColor *= texture(baseColorTexture, inUV);
    }
    if (baseColor.a < pushConstants.emissiveFactorAndAlphaCutoff.a) {
        // Shadow maps must follow the same cutout rule as the GBuffer and RT
        // any-hit shaders; otherwise alpha-masked curtains/foliage either cast
        // solid blocks of shadow or disappear from visibility entirely.
        discard;
    }
}
