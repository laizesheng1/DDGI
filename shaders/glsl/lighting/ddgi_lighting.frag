#version 450

layout(location = 0) out vec4 outColor;

void main()
{
    // TODO: Query DDGI irradiance for the shaded surface.
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
}
