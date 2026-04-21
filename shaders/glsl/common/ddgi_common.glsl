#ifndef DDGI_COMMON_GLSL
#define DDGI_COMMON_GLSL

vec2 ddgiOctEncode(vec3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 encoded = n.xy;
    if (n.z < 0.0) {
        encoded = (1.0 - abs(encoded.yx)) * sign(encoded.xy);
    }
    return encoded * 0.5 + 0.5;
}

#endif
