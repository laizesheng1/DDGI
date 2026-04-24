#version 450

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormalWorld;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 baseColor = texture(baseColorTexture, inUV) * inColor;

    // Simple directional light for visibility before the deferred lighting and
    // DDGI query are wired into the renderer.
    vec3 normalWorld = normalize(inNormalWorld);
    vec3 lightDirectionWorld = normalize(vec3(0.35, 0.85, 0.25));
    float directLight = max(dot(normalWorld, lightDirectionWorld), 0.0);
    vec3 ambient = vec3(0.18, 0.20, 0.22);
    vec3 litColor = baseColor.rgb * (ambient + vec3(0.85) * directLight);
    outColor = vec4(litColor, baseColor.a);

}
