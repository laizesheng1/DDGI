#version 460
#extension GL_EXT_ray_tracing : require

struct DDGITracePayload {
    vec3 radiance;
    float distance;
    vec3 direction;
    float distanceSquared;
};

layout(location = 0) rayPayloadInEXT DDGITracePayload payload;

void main()
{
    vec3 rayDirectionWorld = normalize(gl_WorldRayDirectionEXT);
    float skyBlend = clamp(rayDirectionWorld.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizonRadiance = vec3(0.18, 0.20, 0.23);
    vec3 zenithRadiance = vec3(0.42, 0.55, 0.80);

    payload.radiance = mix(horizonRadiance, zenithRadiance, skyBlend);
    payload.distance = 1.0e27;
    payload.direction = rayDirectionWorld;
    payload.distanceSquared = payload.distance * payload.distance;
}
