#version 450

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 3) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 4) uniform sampler2D occlusionTexture;

layout(push_constant) uniform MaterialPushConstants {
    layout(offset = 64) vec4 baseColorFactor;
    layout(offset = 80) vec4 emissiveFactorAndAlphaCutoff;
    layout(offset = 96) vec4 metallicRoughnessOcclusionFlags;
    layout(offset = 112) vec4 normalScaleAndPadding;
} pushConstants;

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormalWorld;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangentWorld;

layout(location = 0) out vec4 outWorldPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outMaterial;
layout(location = 4) out vec4 outEmissive;

const uint MATERIAL_FLAG_ALPHA_MASK = 1u;
const uint MATERIAL_FLAG_ALPHA_BLEND = 2u;
const uint MATERIAL_FLAG_BASE_COLOR_TEXTURE = 8u;
const uint MATERIAL_FLAG_NORMAL_TEXTURE = 16u;
const uint MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE = 32u;
const uint MATERIAL_FLAG_EMISSIVE_TEXTURE = 64u;
const uint MATERIAL_FLAG_OCCLUSION_TEXTURE = 128u;

vec3 resolveNormalWorld(uint flags)
{
    vec3 geometryNormal = normalize(inNormalWorld);
    if ((flags & MATERIAL_FLAG_NORMAL_TEXTURE) == 0u) {
        return geometryNormal;
    }
    vec3 tangentWorld = inTangentWorld.xyz;
    if (dot(tangentWorld, tangentWorld) < 1.0e-6) {
        // Some glTF assets omit tangents. A derivative TBN keeps normal mapped
        // surfaces usable without changing the model loader's vertex contract.
        vec3 dpdx = dFdx(inWorldPosition);
        vec3 dpdy = dFdy(inWorldPosition);
        vec2 duvdx = dFdx(inUV);
        vec2 duvdy = dFdy(inUV);
        vec3 tangent = normalize(dpdx * duvdy.y - dpdy * duvdx.y);
        tangentWorld = dot(tangent, tangent) > 1.0e-6 ? tangent : normalize(cross(abs(geometryNormal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), geometryNormal));
    } else {
        tangentWorld = normalize(tangentWorld);
    }

    vec3 bitangentWorld = normalize(cross(geometryNormal, tangentWorld)) * (inTangentWorld.w == 0.0 ? 1.0 : inTangentWorld.w);
    mat3 tbn = mat3(tangentWorld, bitangentWorld, geometryNormal);
    vec3 normalSample = texture(normalTexture, inUV).xyz * 2.0 - 1.0;
    normalSample.xy *= pushConstants.normalScaleAndPadding.x;
    return normalize(tbn * normalSample);
}

void main()
{
    uint flags = uint(pushConstants.metallicRoughnessOcclusionFlags.w + 0.5);
    vec4 baseColor = pushConstants.baseColorFactor * inColor;
    if ((flags & MATERIAL_FLAG_BASE_COLOR_TEXTURE) != 0u) {
        baseColor *= texture(baseColorTexture, inUV);
    }
    if (((flags & MATERIAL_FLAG_ALPHA_MASK) != 0u || (flags & MATERIAL_FLAG_ALPHA_BLEND) != 0u) &&
        baseColor.a < pushConstants.emissiveFactorAndAlphaCutoff.a) {
        // BLEND is treated as alpha-tested in the deferred path for now. That
        // keeps GBuffer depth, RT any-hit alpha, and DDGI visibility coherent.
        discard;
    }

    vec4 mrSample = ((flags & MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE) != 0u)
        ? texture(metallicRoughnessTexture, inUV)
        : vec4(1.0);
    float roughness = clamp(mrSample.g * pushConstants.metallicRoughnessOcclusionFlags.y, 0.04, 1.0);
    float metallic = clamp(mrSample.b * pushConstants.metallicRoughnessOcclusionFlags.x, 0.0, 1.0);
    float occlusionSample = ((flags & MATERIAL_FLAG_OCCLUSION_TEXTURE) != 0u) ? texture(occlusionTexture, inUV).r : 1.0;
    float occlusion = mix(1.0, occlusionSample, clamp(pushConstants.metallicRoughnessOcclusionFlags.z, 0.0, 1.0));
    vec3 emissiveSample = ((flags & MATERIAL_FLAG_EMISSIVE_TEXTURE) != 0u) ? texture(emissiveTexture, inUV).rgb : vec3(1.0);
    vec3 emissive = emissiveSample * pushConstants.emissiveFactorAndAlphaCutoff.rgb;

    outWorldPosition = vec4(inWorldPosition, 1.0);
    outNormal = vec4(resolveNormalWorld(flags), 1.0);
    outAlbedo = vec4(baseColor.rgb, baseColor.a);
    outMaterial = vec4(roughness, metallic, occlusion, baseColor.a);
    outEmissive = vec4(emissive, 1.0);
}
