#version 460
#extension GL_EXT_ray_tracing : require

struct DDGITracePayload {
    vec3 radiance;
    float distance;
    vec3 direction;
    float distanceSquared;
    vec3 normal;
    float flags;
};

layout(location = 0) rayPayloadInEXT DDGITracePayload payload;
hitAttributeEXT vec2 triangleBarycentrics;

struct SceneRtVertexAttribute {
    vec4 normalAndMaterial;
    vec4 uvAndFlags;
};

struct SceneRtMesh {
    uvec4 firstIndexFirstVertexMaterialFlags;
};

struct SceneRtMaterial {
    vec4 baseColorAndAlphaCutoff;
    vec4 emissiveAndFlags;
};

layout(set = 1, binding = 1, std430) readonly buffer SceneVertexAttributesBuffer {
    SceneRtVertexAttribute vertexAttributes[];
};

layout(set = 1, binding = 2, std430) readonly buffer SceneIndexBuffer {
    uint sceneIndices[];
};

layout(set = 1, binding = 3, std430) readonly buffer SceneMeshBuffer {
    SceneRtMesh sceneMeshes[];
};

layout(set = 1, binding = 4, std430) readonly buffer SceneMaterialBuffer {
    SceneRtMaterial sceneMaterials[];
};

void main()
{
    vec3 rayDirectionWorld = normalize(gl_WorldRayDirectionEXT);
    float hitDistanceWorld = max(gl_HitTEXT, 0.0);
    uint meshIndex = uint(gl_InstanceCustomIndexEXT);
    SceneRtMesh mesh = sceneMeshes[meshIndex];
    uint firstIndex = mesh.firstIndexFirstVertexMaterialFlags.x;
    uint firstVertex = mesh.firstIndexFirstVertexMaterialFlags.y;
    uint materialIndex = mesh.firstIndexFirstVertexMaterialFlags.z;
    uint primitiveIndexBase = firstIndex + uint(gl_PrimitiveID) * 3u;

    uvec3 localIndices = uvec3(
        sceneIndices[primitiveIndexBase + 0u],
        sceneIndices[primitiveIndexBase + 1u],
        sceneIndices[primitiveIndexBase + 2u]);
    uvec3 vertexIndices = localIndices + uvec3(firstVertex);

    vec3 barycentricWeights = vec3(
        1.0 - triangleBarycentrics.x - triangleBarycentrics.y,
        triangleBarycentrics.x,
        triangleBarycentrics.y);
    vec3 interpolatedNormal =
        vertexAttributes[vertexIndices.x].normalAndMaterial.xyz * barycentricWeights.x +
        vertexAttributes[vertexIndices.y].normalAndMaterial.xyz * barycentricWeights.y +
        vertexAttributes[vertexIndices.z].normalAndMaterial.xyz * barycentricWeights.z;
    vec3 geometricNormalWorld = normalize(interpolatedNormal);
    bool frontFacing = dot(geometricNormalWorld, -rayDirectionWorld) > 0.0;
    vec3 shadingNormalWorld = frontFacing ? geometricNormalWorld : -geometricNormalWorld;

    SceneRtMaterial material = sceneMaterials[materialIndex];
    vec3 baseColor = max(material.baseColorAndAlphaCutoff.rgb, vec3(0.0));
    vec3 emissive = max(material.emissiveAndFlags.rgb, vec3(0.0));

    // This is still a one-bounce probe trace, so we approximate hit radiance
    // from material factors and the angle between the probe ray and surface.
    // The key improvement over the old debug color is that probe atlas values
    // now react to authored baseColor/emissive factors without requiring
    // bindless texture descriptors in the first material pass.
    float facing = clamp(dot(shadingNormalWorld, -rayDirectionWorld), 0.08, 1.0);

    payload.radiance = baseColor * facing + emissive;
    payload.distance = hitDistanceWorld;
    payload.direction = rayDirectionWorld;
    payload.distanceSquared = hitDistanceWorld * hitDistanceWorld;
    payload.normal = shadingNormalWorld;
    payload.flags = frontFacing ? 1.0 : 3.0;
}
