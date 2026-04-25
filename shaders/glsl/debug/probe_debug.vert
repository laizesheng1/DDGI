#version 450

layout(push_constant) uniform ProbeDebugPushConstants {
    mat4 viewProjection;
    vec4 volumeOriginAndSize;
    vec4 probeSpacing;
    uvec4 probeCounts;
} pushConstants;

layout(location = 0) out vec3 outColor;

void main()
{
    const vec2 positions[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );

    uint probeIndex = uint(gl_InstanceIndex);
    uint probeCountX = max(pushConstants.probeCounts.x, 1u);
    uint probeCountY = max(pushConstants.probeCounts.y, 1u);
    uint probeCoordX = probeIndex % probeCountX;
    uint probeCoordY = (probeIndex / probeCountX) % probeCountY;
    uint probeCoordZ = probeIndex / (probeCountX * probeCountY);

    vec3 probeWorldPosition = pushConstants.volumeOriginAndSize.xyz +
        vec3(probeCoordX, probeCoordY, probeCoordZ) * pushConstants.probeSpacing.xyz;
    vec4 clipPosition = pushConstants.viewProjection * vec4(probeWorldPosition, 1.0);
    vec2 clipOffset = positions[gl_VertexIndex] * pushConstants.volumeOriginAndSize.w * clipPosition.w;
    gl_Position = clipPosition + vec4(clipOffset, 0.0, 0.0);
    outColor = vec3(0.1, 0.95, 0.95);
}
