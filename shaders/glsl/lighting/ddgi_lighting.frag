#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/ddgi_common.glsl"
#include "../common/pbr_common.glsl"
#include "../common/light_common.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D gbufferWorldPosition;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gbufferMaterial;
layout(set = 0, binding = 4) uniform sampler2D gbufferEmissive;
layout(set = 0, binding = 5) uniform sampler2D gbufferDepth;

struct DDGIFrameConstants {
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 volumeOriginAndRays;
    vec4 probeSpacingAndHysteresis;
    uvec4 probeCounts;
    vec4 biasAndDebug;
    uvec4 atlasLayout;
    vec4 traceParams;
    vec4 stabilityParams;
    uvec4 updateParams;
    vec4 scrollAnchorAndMovement;
    vec4 sdfOriginAndMaxDistance;
    vec4 sdfVoxelSizeAndClearance;
    uvec4 sdfResolutionAndFlags;
    vec4 multiBounceParams;
};

layout(set = 1, binding = 0) uniform DDGIConstantsBuffer {
    DDGIFrameConstants constants;
};

layout(set = 1, binding = 4, rgba16f) readonly uniform image2D irradianceAtlas;
layout(set = 1, binding = 5, r32f) readonly uniform image2D depthAtlas;
layout(set = 1, binding = 6, r32f) readonly uniform image2D depthSquaredAtlas;

layout(set = 1, binding = 3, std430) readonly buffer ProbeStatesBuffer {
    uint probeStates[];
};

layout(set = 2, binding = 0) uniform SceneLightingInfoBuffer {
    SceneLightingInfo sceneLighting;
};

layout(set = 2, binding = 1, std430) readonly buffer SceneLightBuffer {
    SceneLight sceneLights[];
};

layout(set = 2, binding = 2) uniform sampler2D shadowMap;

layout(push_constant) uniform LightingPushConstants {
    vec4 cameraPosition;
    vec4 options; // x: DDGI enable, y: DDGI intensity, z: exposure, w: debug mode
    mat4 shadowLightViewProjection;
    vec4 shadowParams; // x enabled, y texel world size, z normal bias, w depth bias
} pushConstants;

#include "../common/ddgi_query.glsl"

// The shadow map is rendered through Vulkan framebuffer coordinates, then read
// back as a sampled texture. In this project those two paths expose opposite
// Y conventions to the lighting shader, so the production lookup flips V after
// projecting into light clip space. The "raw Y" debug modes intentionally skip
// this flip to diagnose future projection regressions.
const bool DDGI_SHADOW_TEXTURE_FLIP_Y = true;

struct ShadowProjection {
    vec3 ndc;
    vec2 uv;
    bool inRange;
};

ShadowProjection projectShadowPosition(vec3 surfacePositionWorld, bool flipTextureY)
{
    vec4 shadowClip = pushConstants.shadowLightViewProjection * vec4(surfacePositionWorld, 1.0);
    vec3 shadowNdc = shadowClip.xyz / max(shadowClip.w, 1.0e-6);
    vec2 shadowUv = shadowNdc.xy * 0.5 + 0.5;
    if (flipTextureY) {
        shadowUv.y = 1.0 - shadowUv.y;
    }

    ShadowProjection result;
    result.ndc = shadowNdc;
    result.uv = shadowUv;
    result.inRange =
        shadowUv.x > 0.0 && shadowUv.x < 1.0 &&
        shadowUv.y > 0.0 && shadowUv.y < 1.0 &&
        shadowNdc.z > 0.0 && shadowNdc.z < 1.0;
    return result;
}

float sampleDirectionalShadow(vec3 surfacePositionWorld,
                              vec3 surfaceNormalWorld,
                              vec3 directionToLight,
                              bool flipTextureY,
                              bool reverseDepthCompare)
{
    if (pushConstants.shadowParams.x <= 0.5) {
        return 1.0;
    }

    vec3 biasedPosition = surfacePositionWorld +
        surfaceNormalWorld * pushConstants.shadowParams.z +
        directionToLight * pushConstants.shadowParams.y;
    ShadowProjection shadowProjection = projectShadowPosition(biasedPosition, flipTextureY);
    if (!shadowProjection.inRange) {
        return 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float receiverDepth = shadowProjection.ndc.z;
    receiverDepth += reverseDepthCompare ? pushConstants.shadowParams.w : -pushConstants.shadowParams.w;
    float visibility = 0.0;
    float validSamples = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float blockerDepth = texture(shadowMap, shadowProjection.uv + vec2(x, y) * texelSize).r;
            if (blockerDepth <= 1.0e-5 || blockerDepth >= 0.99999) {
                visibility += 1.0;
                continue;
            }
            validSamples += 1.0;
            visibility += reverseDepthCompare
                ? (receiverDepth >= blockerDepth ? 1.0 : 0.0)
                : (receiverDepth <= blockerDepth ? 1.0 : 0.0);
        }
    }
    if (validSamples <= 0.0) {
        return 1.0;
    }
    return visibility / 9.0;
}

float sampleShadowMapDepthAtSurface(vec3 surfacePositionWorld, bool flipTextureY)
{
    ShadowProjection shadowProjection = projectShadowPosition(surfacePositionWorld, flipTextureY);
    if (!shadowProjection.inRange) {
        return 1.0;
    }
    return texture(shadowMap, shadowProjection.uv).r;
}

float shadowReceiverDepthAtSurface(vec3 surfacePositionWorld)
{
    ShadowProjection shadowProjection = projectShadowPosition(surfacePositionWorld, false);
    if (!shadowProjection.inRange) {
        return 1.0;
    }
    return shadowProjection.ndc.z;
}

void main()
{
    float depth = texture(gbufferDepth, inUV).r;
    // The main swapchain depth buffer is empty at fullscreen lighting time.
    // Re-emitting GBuffer depth keeps debug geometry depth-tested against the
    // visible scene without a second geometry prepass.
    gl_FragDepth = depth;
    if (depth >= 0.999999) {
        outColor = vec4(0.02, 0.025, 0.03, 1.0);
        return;
    }

    vec3 surfacePositionWorld = texture(gbufferWorldPosition, inUV).xyz;
    vec3 surfaceNormalWorld = normalize(texture(gbufferNormal, inUV).xyz);
    vec4 albedoSample = texture(gbufferAlbedo, inUV);
    vec4 materialSample = texture(gbufferMaterial, inUV);
    vec3 emissive = texture(gbufferEmissive, inUV).rgb;

    vec3 baseColor = max(albedoSample.rgb, vec3(0.0));
    float roughness = clamp(materialSample.r, 0.04, 1.0);
    float metallic = clamp(materialSample.g, 0.0, 1.0);
    float occlusion = clamp(materialSample.b, 0.0, 1.0);
    vec3 viewDirectionWorld = normalize(pushConstants.cameraPosition.xyz - surfacePositionWorld);

    vec3 directLighting = vec3(0.0);
    float primaryShadowVisibility = 1.0;
    vec3 primaryDirectionToLight = vec3(0.0, 1.0, 0.0);
    uint debugMode = uint(pushConstants.options.w + 0.5);
    bool debugRawShadowY = debugMode == 7u || debugMode == 8u || debugMode == 9u;
    bool shadowLookupFlipY = debugRawShadowY ? false : DDGI_SHADOW_TEXTURE_FLIP_Y;
    float primaryShadowDepth = sampleShadowMapDepthAtSurface(surfacePositionWorld, shadowLookupFlipY);
    uint lightCount = min(sceneLighting.lightCounts.x, SCENE_MAX_LIGHTS);
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex) {
        LightSample lightSample = evaluateSceneLight(sceneLights[lightIndex], surfacePositionWorld);
        float visibility = (lightIndex == 0u && lightSample.type == SCENE_LIGHT_DIRECTIONAL)
            ? sampleDirectionalShadow(
                surfacePositionWorld,
                surfaceNormalWorld,
                lightSample.directionToLight,
                DDGI_SHADOW_TEXTURE_FLIP_Y,
                false)
            : 1.0;
        if (lightIndex == 0u) {
            primaryShadowVisibility = visibility;
            primaryDirectionToLight = lightSample.directionToLight;
        }
        directLighting += pbrEvaluateDirect(
            baseColor,
            metallic,
            roughness,
            surfaceNormalWorld,
            viewDirectionWorld,
            lightSample.directionToLight,
            lightSample.radiance * visibility);
    }

    vec3 indirectIrradiance = pushConstants.options.x > 0.5
        ? ddgiQueryIndirectIrradiance(surfacePositionWorld, surfaceNormalWorld, viewDirectionWorld, true)
        : vec3(0.0);
    vec3 diffuseAlbedo = baseColor * (1.0 - metallic);
    vec3 indirectDiffuse = indirectIrradiance * diffuseAlbedo * (pushConstants.options.y / DDGI_PI) * occlusion;
    vec3 ambient = diffuseAlbedo * sceneLighting.ambientColorAndExposure.rgb * occlusion;

    vec3 finalColor = directLighting + indirectDiffuse + ambient + emissive;
    if (debugMode == 1u) {
        finalColor = directLighting;
    } else if (debugMode == 2u) {
        finalColor = vec3(primaryShadowVisibility);
    } else if (debugMode == 3u) {
        finalColor = vec3(primaryShadowDepth);
    } else if (debugMode == 4u) {
        finalColor = vec3(shadowReceiverDepthAtSurface(surfacePositionWorld));
    } else if (debugMode == 5u) {
        float receiverDepth = shadowReceiverDepthAtSurface(surfacePositionWorld);
        float delta = receiverDepth - primaryShadowDepth;
        // 0.5 means receiver ~= stored shadow depth. Values below 0.5 are in
        // front of the stored depth, values above 0.5 are behind it and will be
        // classified as shadowed by the normal <= compare.
        finalColor = vec3(clamp(delta * 20.0 + 0.5, 0.0, 1.0));
    } else if (debugMode == 6u) {
        ShadowProjection shadowProjection = projectShadowPosition(surfacePositionWorld, DDGI_SHADOW_TEXTURE_FLIP_Y);
        finalColor = shadowProjection.inRange
            ? vec3(shadowProjection.uv, 1.0)
            : vec3(1.0, 0.0, 0.0);
    } else if (debugMode == 7u) {
        finalColor = vec3(sampleDirectionalShadow(
            surfacePositionWorld,
            surfaceNormalWorld,
            primaryDirectionToLight,
            false,
            false));
    } else if (debugMode == 8u) {
        finalColor = vec3(sampleShadowMapDepthAtSurface(surfacePositionWorld, false));
    } else if (debugMode == 9u) {
        float receiverDepth = shadowReceiverDepthAtSurface(surfacePositionWorld);
        float rawShadowDepth = sampleShadowMapDepthAtSurface(surfacePositionWorld, false);
        float delta = receiverDepth - rawShadowDepth;
        finalColor = vec3(clamp(delta * 20.0 + 0.5, 0.0, 1.0));
    } else if (debugMode == 10u) {
        finalColor = vec3(sampleDirectionalShadow(
            surfacePositionWorld,
            surfaceNormalWorld,
            primaryDirectionToLight,
            DDGI_SHADOW_TEXTURE_FLIP_Y,
            true));
    }
    finalColor = acesTonemap(finalColor * max(pushConstants.options.z, 0.0));
    outColor = vec4(finalColor, 1.0);
}
