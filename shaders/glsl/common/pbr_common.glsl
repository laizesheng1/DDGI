#ifndef PBR_COMMON_GLSL
#define PBR_COMMON_GLSL

#include "ddgi_common.glsl"

float pbrDistributionGGX(vec3 normalWorld, vec3 halfVectorWorld, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normalWorld, halfVectorWorld), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denom = nDotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(DDGI_PI * denom * denom, 1.0e-5);
}

float pbrGeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 1.0e-5);
}

float pbrGeometrySmith(vec3 normalWorld, vec3 viewDirectionWorld, vec3 lightDirectionWorld, float roughness)
{
    float nDotV = max(dot(normalWorld, viewDirectionWorld), 0.0);
    float nDotL = max(dot(normalWorld, lightDirectionWorld), 0.0);
    return pbrGeometrySchlickGGX(nDotV, roughness) * pbrGeometrySchlickGGX(nDotL, roughness);
}

vec3 pbrFresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 pbrEvaluateDirect(vec3 baseColor,
                       float metallic,
                       float roughness,
                       vec3 normalWorld,
                       vec3 viewDirectionWorld,
                       vec3 lightDirectionWorld,
                       vec3 lightRadiance)
{
    vec3 diffuseAlbedo = baseColor * (1.0 - metallic);
    vec3 halfVectorWorld = normalize(viewDirectionWorld + lightDirectionWorld);
    float nDotL = max(dot(normalWorld, lightDirectionWorld), 0.0);
    float nDotV = max(dot(normalWorld, viewDirectionWorld), 0.0);
    if (nDotL <= 0.0 || nDotV <= 0.0) {
        return vec3(0.0);
    }

    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    float d = pbrDistributionGGX(normalWorld, halfVectorWorld, roughness);
    float g = pbrGeometrySmith(normalWorld, viewDirectionWorld, lightDirectionWorld, roughness);
    vec3 f = pbrFresnelSchlick(max(dot(halfVectorWorld, viewDirectionWorld), 0.0), f0);
    vec3 specular = (d * g * f) / max(4.0 * nDotV * nDotL, 1.0e-5);
    vec3 diffuse = diffuseAlbedo / DDGI_PI;
    return (diffuse + specular) * lightRadiance * nDotL;
}

vec3 acesTonemap(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

#endif
