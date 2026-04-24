#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/ddgi_common.glsl"

layout(location = 0) out vec4 outColor;

struct DDGIFrameConstants {
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 volumeOriginAndRays;
    vec4 probeSpacingAndHysteresis;
    uvec4 probeCounts;
    vec4 biasAndDebug;
    uvec4 atlasLayout;
};

layout(set = 0, binding = 0) uniform DDGIConstantsBuffer {
    DDGIFrameConstants constants;
};

layout(set = 0, binding = 4, rgba16f) readonly uniform image2D irradianceAtlas;
layout(set = 0, binding = 5, r32f) readonly uniform image2D depthAtlas;
layout(set = 0, binding = 6, r32f) readonly uniform image2D depthSquaredAtlas;

float ddgiLoadDepthMoment(uint probeIndex, vec3 probeToSurfaceDirection, bool squaredMoment)
{
    uint octSize = constants.atlasLayout.w;
    uint tileSize = octSize + 2u;
    vec2 octUv = ddgiOctEncode(probeToSurfaceDirection);
    uvec2 octTexel = uvec2(clamp(floor(octUv * float(octSize)), vec2(0.0), vec2(float(octSize - 1u))));
    ivec2 atlasTexel = ivec2(ddgiAtlasInteriorTexel(probeIndex, octTexel, constants.atlasLayout.x, tileSize));
    return squaredMoment
        ? imageLoad(depthSquaredAtlas, atlasTexel).r
        : imageLoad(depthAtlas, atlasTexel).r;
}

vec3 ddgiLoadIrradiance(uint probeIndex, vec3 surfaceNormalWorld)
{
    uint octSize = constants.atlasLayout.z;
    uint tileSize = octSize + 2u;
    vec2 octUv = ddgiOctEncode(surfaceNormalWorld);
    uvec2 octTexel = uvec2(clamp(floor(octUv * float(octSize)), vec2(0.0), vec2(float(octSize - 1u))));
    ivec2 atlasTexel = ivec2(ddgiAtlasInteriorTexel(probeIndex, octTexel, constants.atlasLayout.x, tileSize));
    return imageLoad(irradianceAtlas, atlasTexel).rgb;
}

float ddgiChebyshevVisibility(float receiverDistance, float meanDistance, float meanDistanceSquared)
{
    float variance = max(meanDistanceSquared - meanDistance * meanDistance, 0.02);
    float distanceDelta = max(receiverDistance - meanDistance, 0.0);
    float chebyshev = variance / (variance + distanceDelta * distanceDelta);
    return receiverDistance <= meanDistance ? 1.0 : clamp(chebyshev, 0.0, 1.0);
}

vec3 ddgiQueryIndirectDiffuse(vec3 surfacePositionWorld, vec3 surfaceNormalWorld, vec3 viewDirectionWorld)
{
    vec3 biasedPositionWorld = ddgiApplySurfaceBias(
        surfacePositionWorld,
        surfaceNormalWorld,
        viewDirectionWorld,
        constants.biasAndDebug.x,
        constants.biasAndDebug.y);

    vec3 volumePosition = (biasedPositionWorld - constants.volumeOriginAndRays.xyz) /
        constants.probeSpacingAndHysteresis.xyz;
    ivec3 baseCell = ivec3(floor(volumePosition));
    ivec3 maxBaseCell = ivec3(max(constants.probeCounts.xyz, uvec3(1u)) - uvec3(1u));
    baseCell = clamp(baseCell, ivec3(0), max(maxBaseCell - ivec3(1), ivec3(0)));
    vec3 trilinearAlpha = clamp(volumePosition - vec3(baseCell), vec3(0.0), vec3(1.0));

    vec3 accumulatedIrradiance = vec3(0.0);
    float accumulatedWeight = 0.0;

    for (uint cornerIndex = 0u; cornerIndex < 8u; ++cornerIndex) {
        uvec3 cornerOffset = uvec3(cornerIndex & 1u, (cornerIndex >> 1u) & 1u, (cornerIndex >> 2u) & 1u);
        uvec3 probeCoord = uvec3(baseCell) + cornerOffset;
        probeCoord = min(probeCoord, constants.probeCounts.xyz - uvec3(1u));
        uint probeIndex = ddgiProbeIndex(probeCoord, constants.probeCounts.xyz);

        vec3 probePositionWorld = ddgiProbeWorldPosition(
            probeCoord,
            constants.volumeOriginAndRays.xyz,
            constants.probeSpacingAndHysteresis.xyz);
        vec3 probeToSurface = biasedPositionWorld - probePositionWorld;
        float receiverDistance = length(probeToSurface);
        vec3 probeToSurfaceDirection = ddgiSafeNormalize(probeToSurface);

        vec3 cornerWeight = mix(1.0 - trilinearAlpha, trilinearAlpha, vec3(cornerOffset));
        float trilinearWeight = cornerWeight.x * cornerWeight.y * cornerWeight.z;
        float normalWeight = max(0.05, dot(surfaceNormalWorld, -probeToSurfaceDirection));
        float meanDistance = ddgiLoadDepthMoment(probeIndex, probeToSurfaceDirection, false);
        float meanDistanceSquared = ddgiLoadDepthMoment(probeIndex, probeToSurfaceDirection, true);
        float visibilityWeight = ddgiChebyshevVisibility(receiverDistance, meanDistance, meanDistanceSquared);

        float finalWeight = trilinearWeight * normalWeight * visibilityWeight;
        accumulatedIrradiance += ddgiLoadIrradiance(probeIndex, surfaceNormalWorld) * finalWeight;
        accumulatedWeight += finalWeight;
    }

    return accumulatedWeight > DDGI_EPSILON
        ? accumulatedIrradiance / accumulatedWeight
        : vec3(0.0);
}

void main()
{
    // The renderer does not yet provide GBuffer world position/normal inputs to
    // this pass. Keep the shader compilable and place the complete DDGI query
    // above so wiring the actual lighting pass is a descriptor/input task next.
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
}
