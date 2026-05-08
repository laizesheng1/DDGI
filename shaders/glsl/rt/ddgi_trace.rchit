#version 460
#extension GL_EXT_ray_tracing : require

const float DDGI_PI = 3.14159265358979323846;
const uint DDGI_RT_MATERIAL_FLAG_MASK = 1u;
const uint DDGI_RT_MATERIAL_FLAG_BLEND = 2u;
const uint DDGI_RT_MATERIAL_FLAG_BASE_COLOR_TEXTURE = 4u;
const uint DDGI_RT_MATERIAL_FLAG_NORMAL_TEXTURE = 8u;
const uint DDGI_RT_MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE = 16u;
const uint DDGI_RT_MATERIAL_FLAG_EMISSIVE_TEXTURE = 32u;
const uint DDGI_MAX_RT_MATERIAL_TEXTURES = 256u;

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
    vec4 tangentAndSign;
};

struct SceneRtMesh {
    uvec4 firstIndexFirstVertexMaterialFlags;
};

struct SceneRtMaterial {
    vec4 baseColorAndAlphaCutoff;
    vec4 emissiveAndFlags;
    vec4 metallicRoughnessAndFlags;
    vec4 baseColorTextureTransform;
    vec4 normalTextureTransform;
    vec4 metallicRoughnessTextureTransform;
    vec4 emissiveTextureTransform;
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

layout(set = 1, binding = 5) uniform sampler2D baseColorTextures[DDGI_MAX_RT_MATERIAL_TEXTURES];
layout(set = 1, binding = 6) uniform sampler2D normalTextures[DDGI_MAX_RT_MATERIAL_TEXTURES];
layout(set = 1, binding = 7) uniform sampler2D metallicRoughnessTextures[DDGI_MAX_RT_MATERIAL_TEXTURES];
layout(set = 1, binding = 8) uniform sampler2D emissiveTextures[DDGI_MAX_RT_MATERIAL_TEXTURES];

vec2 applyUvTransform(vec2 uv, vec4 transform)
{
    return uv * transform.xy + transform.zw;
}

void loadHitInputs(out uint materialIndex,
                   out vec3 barycentricWeights,
                   out SceneRtVertexAttribute a0,
                   out SceneRtVertexAttribute a1,
                   out SceneRtVertexAttribute a2)
{
    uint meshIndex = uint(gl_InstanceCustomIndexEXT);
    SceneRtMesh mesh = sceneMeshes[meshIndex];
    uint firstIndex = mesh.firstIndexFirstVertexMaterialFlags.x;
    uint firstVertex = mesh.firstIndexFirstVertexMaterialFlags.y;
    materialIndex = min(mesh.firstIndexFirstVertexMaterialFlags.z, DDGI_MAX_RT_MATERIAL_TEXTURES - 1u);
    uint primitiveIndexBase = firstIndex + uint(gl_PrimitiveID) * 3u;

    uvec3 localIndices = uvec3(
        sceneIndices[primitiveIndexBase + 0u],
        sceneIndices[primitiveIndexBase + 1u],
        sceneIndices[primitiveIndexBase + 2u]);
    uvec3 vertexIndices = localIndices + uvec3(firstVertex);

    barycentricWeights = vec3(
        1.0 - triangleBarycentrics.x - triangleBarycentrics.y,
        triangleBarycentrics.x,
        triangleBarycentrics.y);
    a0 = vertexAttributes[vertexIndices.x];
    a1 = vertexAttributes[vertexIndices.y];
    a2 = vertexAttributes[vertexIndices.z];
}

void main()
{
    vec3 rayDirectionWorld = normalize(gl_WorldRayDirectionEXT);
    float hitDistanceWorld = max(gl_HitTEXT, 0.0);

    uint materialIndex;
    vec3 barycentricWeights;
    SceneRtVertexAttribute a0;
    SceneRtVertexAttribute a1;
    SceneRtVertexAttribute a2;
    loadHitInputs(materialIndex, barycentricWeights, a0, a1, a2);

    vec3 geometricNormalWorld = normalize(
        a0.normalAndMaterial.xyz * barycentricWeights.x +
        a1.normalAndMaterial.xyz * barycentricWeights.y +
        a2.normalAndMaterial.xyz * barycentricWeights.z);
    vec4 interpolatedTangent = normalize(
        a0.tangentAndSign * barycentricWeights.x +
        a1.tangentAndSign * barycentricWeights.y +
        a2.tangentAndSign * barycentricWeights.z);
    vec2 uv =
        a0.uvAndFlags.xy * barycentricWeights.x +
        a1.uvAndFlags.xy * barycentricWeights.y +
        a2.uvAndFlags.xy * barycentricWeights.z;

    // Classification and relocation need the actual triangle orientation that
    // the RT pipeline hit, not an interpolated/smoothed vertex normal. Smooth
    // normals on columns or arches can point away from the geometric face and
    // incorrectly turn a valid frontface hit into backface/no-geometry evidence.
    bool frontFacing = gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT;
    vec3 surfaceNormalWorld = frontFacing ? geometricNormalWorld : -geometricNormalWorld;
    vec3 tangentWorld = normalize(interpolatedTangent.xyz - surfaceNormalWorld * dot(interpolatedTangent.xyz, surfaceNormalWorld));
    vec3 bitangentWorld = normalize(cross(surfaceNormalWorld, tangentWorld)) * (interpolatedTangent.w >= 0.0 ? 1.0 : -1.0);

    SceneRtMaterial material = sceneMaterials[materialIndex];
    uint materialFlags = uint(material.emissiveAndFlags.w + 0.5);

    vec4 baseColor = vec4(
        material.baseColorAndAlphaCutoff.rgb,
        material.metallicRoughnessAndFlags.z > 0.0 ? material.metallicRoughnessAndFlags.z : 1.0);
    if ((materialFlags & DDGI_RT_MATERIAL_FLAG_BASE_COLOR_TEXTURE) != 0u) {
        baseColor *= texture(baseColorTextures[materialIndex], applyUvTransform(uv, material.baseColorTextureTransform));
    }

    vec3 shadingNormalWorld = surfaceNormalWorld;
    if ((materialFlags & DDGI_RT_MATERIAL_FLAG_NORMAL_TEXTURE) != 0u) {
        vec3 normalSample = texture(normalTextures[materialIndex], applyUvTransform(uv, material.normalTextureTransform)).xyz * 2.0 - 1.0;
        mat3 tangentToWorld = mat3(tangentWorld, bitangentWorld, surfaceNormalWorld);
        shadingNormalWorld = normalize(tangentToWorld * normalSample);
        if (!frontFacing) {
            shadingNormalWorld = -shadingNormalWorld;
        }
    }

    float metallic = clamp(material.metallicRoughnessAndFlags.x, 0.0, 1.0);
    float roughness = clamp(material.metallicRoughnessAndFlags.y, 0.04, 1.0);
    if ((materialFlags & DDGI_RT_MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE) != 0u) {
        vec4 metallicRoughness = texture(metallicRoughnessTextures[materialIndex], applyUvTransform(uv, material.metallicRoughnessTextureTransform));
        roughness = clamp(roughness * metallicRoughness.g, 0.04, 1.0);
        metallic = clamp(metallic * metallicRoughness.b, 0.0, 1.0);
    }

    vec3 emissive = max(material.emissiveAndFlags.rgb, vec3(0.0));
    if ((materialFlags & DDGI_RT_MATERIAL_FLAG_EMISSIVE_TEXTURE) != 0u) {
        emissive *= texture(emissiveTextures[materialIndex], applyUvTransform(uv, material.emissiveTextureTransform)).rgb;
    }

    vec3 sunDirectionWorld = normalize(vec3(-0.35, 0.80, -0.45));
    float ndl = max(dot(shadingNormalWorld, sunDirectionWorld), 0.0);
    vec3 diffuseAlbedo = max(baseColor.rgb, vec3(0.0)) * (1.0 - metallic);
    vec3 directDiffuse = diffuseAlbedo * ndl * mix(1.25, 0.80, roughness) / DDGI_PI;

    payload.radiance = directDiffuse + emissive;
    payload.distance = hitDistanceWorld;
    payload.direction = rayDirectionWorld;
    payload.distanceSquared = hitDistanceWorld * hitDistanceWorld;
    payload.normal = shadingNormalWorld;
    payload.flags = frontFacing ? 1.0 : 3.0;
}
