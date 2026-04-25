#version 450

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormalWorld;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec4 outWorldPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outMaterial;

void main()
{
    vec4 baseColor = texture(baseColorTexture, inUV) * inColor;

    outWorldPosition = vec4(inWorldPosition, 1.0);
    outNormal = vec4(normalize(inNormalWorld), 1.0);
    outAlbedo = vec4(baseColor.rgb, 1.0);

    // VK MiniRender's current glTF material path only guarantees base color.
    // Keep metallic/roughness in the GBuffer with conservative defaults so the
    // deferred layout is stable while richer material inputs are added later.
    const float roughness = 1.0;
    const float metallic = 0.0;
    outMaterial = vec4(roughness, metallic, 1.0, baseColor.a);
}
