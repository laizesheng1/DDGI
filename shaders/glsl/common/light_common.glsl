#ifndef LIGHT_COMMON_GLSL
#define LIGHT_COMMON_GLSL

const uint SCENE_LIGHT_DIRECTIONAL = 0u;
const uint SCENE_LIGHT_POINT = 1u;
const uint SCENE_LIGHT_SPOT = 2u;
const uint SCENE_MAX_LIGHTS = 32u;

struct SceneLight {
    vec4 positionAndType;
    vec4 directionAndRange;
    vec4 colorAndIntensity;
    vec4 spotAngles;
};

struct SceneLightingInfo {
    uvec4 lightCounts;
    vec4 ambientColorAndExposure;
};

struct LightSample {
    vec3 directionToLight;
    vec3 radiance;
    float distanceToLight;
    uint type;
};

LightSample evaluateSceneLight(SceneLight light, vec3 surfacePositionWorld)
{
    LightSample result;
    uint lightType = uint(light.positionAndType.w + 0.5);
    result.type = lightType;
    result.distanceToLight = 1.0e27;

    if (lightType == SCENE_LIGHT_DIRECTIONAL) {
        result.directionToLight = normalize(light.directionAndRange.xyz);
        result.radiance = max(light.colorAndIntensity.rgb, vec3(0.0)) * max(light.colorAndIntensity.w, 0.0);
        return result;
    }

    vec3 lightVector = light.positionAndType.xyz - surfacePositionWorld;
    float distanceSquared = max(dot(lightVector, lightVector), 1.0e-4);
    float distanceToLight = sqrt(distanceSquared);
    vec3 directionToLight = lightVector / distanceToLight;
    float range = light.directionAndRange.w;
    float rangeAttenuation = range > 0.0
        ? pow(clamp(1.0 - pow(distanceToLight / range, 4.0), 0.0, 1.0), 2.0)
        : 1.0;

    float spotAttenuation = 1.0;
    if (lightType == SCENE_LIGHT_SPOT) {
        vec3 spotForward = normalize(light.directionAndRange.xyz);
        float cd = dot(spotForward, -directionToLight);
        float inner = light.spotAngles.x;
        float outer = light.spotAngles.y;
        spotAttenuation = clamp((cd - outer) / max(inner - outer, 1.0e-4), 0.0, 1.0);
        spotAttenuation *= spotAttenuation;
    }

    result.directionToLight = directionToLight;
    result.distanceToLight = distanceToLight;
    result.radiance = max(light.colorAndIntensity.rgb, vec3(0.0)) *
        max(light.colorAndIntensity.w, 0.0) *
        rangeAttenuation *
        spotAttenuation /
        distanceSquared;
    return result;
}

#endif
