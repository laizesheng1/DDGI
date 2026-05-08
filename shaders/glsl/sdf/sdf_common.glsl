struct SDFGpuConstants {
    vec4 originAndMaxDistance;
    vec4 voxelSizeAndSurfaceThreshold;
    uvec4 resolutionAndTriangleCount;
    uvec4 meshCountAndStep;
};

layout(push_constant) uniform SDFPushConstants {
    SDFGpuConstants constants;
};

layout(set = 0, binding = 1, rgba32f) uniform image3D surfaceSeedImage;
layout(set = 0, binding = 2, rgba32f) uniform image3D seedPingImage;
layout(set = 0, binding = 3, rgba32f) uniform image3D seedPongImage;
layout(set = 0, binding = 4, r32f) uniform image3D finalDistanceImage;

vec3 sdfWorldPositionFromCoord(ivec3 coord)
{
    return constants.originAndMaxDistance.xyz +
        (vec3(coord) + vec3(0.5)) * constants.voxelSizeAndSurfaceThreshold.xyz;
}

bool sdfInside(ivec3 coord)
{
    ivec3 resolution = ivec3(constants.resolutionAndTriangleCount.xyz);
    return all(greaterThanEqual(coord, ivec3(0))) && all(lessThan(coord, resolution));
}

vec4 sdfLoadSeed(uint selector, ivec3 coord)
{
    if (selector == 0u) {
        return imageLoad(surfaceSeedImage, coord);
    }
    if (selector == 1u) {
        return imageLoad(seedPingImage, coord);
    }
    return imageLoad(seedPongImage, coord);
}

void sdfStoreSeed(uint selector, ivec3 coord, vec4 seed)
{
    if (selector == 0u) {
        imageStore(surfaceSeedImage, coord, seed);
    } else if (selector == 1u) {
        imageStore(seedPingImage, coord, seed);
    } else {
        imageStore(seedPongImage, coord, seed);
    }
}
