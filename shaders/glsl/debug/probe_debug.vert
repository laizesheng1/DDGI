#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inProbePositionAndRadius;
layout(location = 3) in vec4 inProbeColor;

layout(push_constant) uniform ProbeDebugPushConstants {
    mat4 viewProjection;
} pushConstants;

layout(location = 0) out vec3 outColor;

void main()
{
    // Probe spheres are drawn in world space so the debug view matches the
    // exact positions used by DDGI tracing and relocation.
    vec3 worldPosition = inProbePositionAndRadius.xyz + inPosition * inProbePositionAndRadius.w;
    vec3 normalWorld = normalize(inNormal);

    // The probe color already encodes a debug approximation of per-probe
    // radiance. A tiny directional-light term makes the sphere shape readable
    // without changing the underlying hue too much.
    vec3 lightDirectionWorld = normalize(vec3(0.35, 0.85, 0.25));
    float ndl = max(dot(normalWorld, lightDirectionWorld), 0.0);
    vec3 shadedColor = inProbeColor.rgb * (0.25 + 0.75 * ndl);
    gl_Position = pushConstants.viewProjection * vec4(worldPosition, 1.0);
    outColor = shadedColor;
}
