#ifndef DDGI_QUERY_GLSL
#define DDGI_QUERY_GLSL

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
    float irradianceGamma = max(constants.traceParams.y, 1.0);
    // Probe atlases store gamma-encoded irradiance to preserve low-energy
    // history. All consumers decode to linear radiance before applying albedo.
    return pow(max(imageLoad(irradianceAtlas, atlasTexel).rgb, vec3(0.0)), vec3(irradianceGamma));
}

float ddgiChebyshevVisibility(float receiverDistance, float meanDistance, float meanDistanceSquared)
{
    float variance = max(meanDistanceSquared - meanDistance * meanDistance, 0.02);
    float distanceDelta = max(receiverDistance - meanDistance, 0.0);
    float chebyshev = variance / (variance + distanceDelta * distanceDelta);
    return receiverDistance <= meanDistance ? 1.0 : clamp(chebyshev, 0.0, 1.0);
}

vec3 ddgiQueryIndirectIrradiance(vec3 surfacePositionWorld,
                                 vec3 surfaceNormalWorld,
                                 vec3 viewDirectionWorld,
                                 bool skipInactiveProbes)
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
        if (skipInactiveProbes && ((constants.updateParams.z & 2u) != 0u) && probeStates[probeIndex] != 0u) {
            continue;
        }

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

vec3 ddgiQueryIndirectDiffuse(vec3 surfacePositionWorld,
                              vec3 surfaceNormalWorld,
                              vec3 viewDirectionWorld,
                              vec3 diffuseAlbedo,
                              float intensity,
                              bool skipInactiveProbes)
{
    vec3 irradiance = ddgiQueryIndirectIrradiance(
        surfacePositionWorld,
        surfaceNormalWorld,
        viewDirectionWorld,
        skipInactiveProbes);
    return irradiance * diffuseAlbedo * (intensity / DDGI_PI);
}

#endif
