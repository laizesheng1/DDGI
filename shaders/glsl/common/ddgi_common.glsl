#ifndef DDGI_COMMON_GLSL
#define DDGI_COMMON_GLSL

const float DDGI_PI = 3.14159265358979323846;
const float DDGI_GOLDEN_ANGLE = 2.39996322972865332;
const float DDGI_EPSILON = 1.0e-5;

float ddgiSaturate(float value)
{
    return clamp(value, 0.0, 1.0);
}

vec3 ddgiSafeNormalize(vec3 value)
{
    float lengthSquared = max(dot(value, value), DDGI_EPSILON);
    return value * inversesqrt(lengthSquared);
}

uint ddgiProbeIndex(uvec3 probeCoord, uvec3 probeCounts)
{
    return probeCoord.x +
        probeCoord.y * probeCounts.x +
        probeCoord.z * probeCounts.x * probeCounts.y;
}

uvec3 ddgiProbeCoord(uint probeIndex, uvec3 probeCounts)
{
    uint probesPerSlice = probeCounts.x * probeCounts.y;
    uvec3 probeCoord;
    probeCoord.z = probeIndex / probesPerSlice;
    uint sliceIndex = probeIndex - probeCoord.z * probesPerSlice;
    probeCoord.y = sliceIndex / probeCounts.x;
    probeCoord.x = sliceIndex - probeCoord.y * probeCounts.x;
    return probeCoord;
}

vec2 ddgiOctWrap(vec2 value)
{
    bvec2 isPositive = greaterThanEqual(value, vec2(0.0));
    return (1.0 - abs(value.yx)) * mix(vec2(-1.0), vec2(1.0), isPositive);
}

vec2 ddgiOctEncodeSigned(vec3 direction)
{
    direction = ddgiSafeNormalize(direction);
    direction /= abs(direction.x) + abs(direction.y) + abs(direction.z);
    vec2 encoded = direction.xy;
    if (direction.z < 0.0) {
        encoded = ddgiOctWrap(encoded);
    }
    return encoded;
}

vec2 ddgiOctEncode(vec3 direction)
{
    return ddgiOctEncodeSigned(direction) * 0.5 + 0.5;
}

vec3 ddgiOctDecodeSigned(vec2 encoded)
{
    vec3 direction = vec3(encoded.xy, 1.0 - abs(encoded.x) - abs(encoded.y));
    if (direction.z < 0.0) {
        direction.xy = ddgiOctWrap(direction.xy);
    }
    return ddgiSafeNormalize(direction);
}

vec3 ddgiOctDecode(vec2 encoded)
{
    return ddgiOctDecodeSigned(encoded * 2.0 - 1.0);
}

uvec2 ddgiAtlasTileCoord(uint probeIndex, uint tileColumns)
{
    return uvec2(probeIndex % tileColumns, probeIndex / tileColumns);
}

uvec2 ddgiAtlasTileBase(uint probeIndex, uint tileColumns, uint tileSizeWithBorder)
{
    return ddgiAtlasTileCoord(probeIndex, tileColumns) * tileSizeWithBorder;
}

uvec2 ddgiAtlasInteriorTexel(uint probeIndex, uvec2 octTexel, uint tileColumns, uint tileSizeWithBorder)
{
    // Atlas tiles keep a one-texel border for bilinear filtering, so interior
    // texels start at +1 inside each probe tile.
    return ddgiAtlasTileBase(probeIndex, tileColumns, tileSizeWithBorder) + octTexel + uvec2(1u);
}

vec2 ddgiOctTexelCenter(uvec2 octTexel, uint octSize)
{
    return (vec2(octTexel) + vec2(0.5)) / float(octSize);
}

vec3 ddgiOctTexelDirection(uvec2 octTexel, uint octSize)
{
    return ddgiOctDecode(ddgiOctTexelCenter(octTexel, octSize));
}

vec3 ddgiFibonacciDirection(uint rayIndex, uint rayCount, float randomRotationRadians)
{
    float rayCountFloat = max(float(rayCount), 1.0);
    float z = 1.0 - (2.0 * (float(rayIndex) + 0.5) / rayCountFloat);
    float radius = sqrt(max(0.0, 1.0 - z * z));
    float phi = float(rayIndex) * DDGI_GOLDEN_ANGLE + randomRotationRadians;
    return vec3(cos(phi) * radius, sin(phi) * radius, z);
}

vec3 ddgiProbeWorldPosition(uvec3 probeCoord, vec3 volumeOriginWorld, vec3 probeSpacingWorld)
{
    return volumeOriginWorld + vec3(probeCoord) * probeSpacingWorld;
}

vec3 ddgiApplySurfaceBias(vec3 surfacePositionWorld,
                          vec3 surfaceNormalWorld,
                          vec3 viewDirectionWorld,
                          float normalBiasWorld,
                          float viewBiasWorld)
{
    // Offset the lookup point away from the receiver to reduce self-occlusion.
    // viewDirectionWorld is expected to point from the camera toward the surface.
    return surfacePositionWorld +
        ddgiSafeNormalize(surfaceNormalWorld) * normalBiasWorld -
        ddgiSafeNormalize(viewDirectionWorld) * viewBiasWorld;
}

#endif
