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

layout(push_constant) uniform LightingPushConstants {
    vec4 cameraPosition;
    vec4 options; // x: DDGI enable, y: DDGI intensity, z: exposure, w: debug mode
} pushConstants;

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

#include "../common/ddgi_query.glsl"

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
    uint lightCount = min(sceneLighting.lightCounts.x, SCENE_MAX_LIGHTS);
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex) {
        LightSample lightSample = evaluateSceneLight(sceneLights[lightIndex], surfacePositionWorld);
        // Shadowing is intentionally centralized here when a shadow map or RT
        // visibility resource is available. Until that resource is bound, the
        // visibility term is one so the material/light semantics remain correct.
        float visibility = 1.0;
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
    finalColor = acesTonemap(finalColor * max(pushConstants.options.z, 0.0));
    outColor = vec4(finalColor, 1.0);
}
