#version 460
#extension GL_EXT_ray_tracing : require

struct DDGITracePayload {
    vec3 radiance;
    float distance;
    vec3 direction;
    float distanceSquared;
};

layout(location = 0) rayPayloadInEXT DDGITracePayload payload;
hitAttributeEXT vec2 triangleBarycentrics;

void main()
{
    vec3 rayDirectionWorld = normalize(gl_WorldRayDirectionEXT);
    float hitDistanceWorld = max(gl_HitTEXT, 0.0);

    // The first hit shader does not yet read scene material/normal buffers.
    // It writes a stable geometry-dependent color so the atlas can prove that
    // hit and miss paths differ before full material shading is wired in.
    float facing = clamp(abs(rayDirectionWorld.y), 0.08, 1.0);
    vec3 debugAlbedo = mix(vec3(0.55, 0.50, 0.44), vec3(0.85, 0.72, 0.48), triangleBarycentrics.x);

    payload.radiance = debugAlbedo * facing;
    payload.distance = hitDistanceWorld;
    payload.direction = rayDirectionWorld;
    payload.distanceSquared = hitDistanceWorld * hitDistanceWorld;
}
