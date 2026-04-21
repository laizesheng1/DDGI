#version 450

layout(location = 0) out vec3 outColor;

void main()
{
    const vec2 positions[3] = vec2[](
        vec2(-0.02, -0.02),
        vec2( 0.02, -0.02),
        vec2( 0.00,  0.02)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    outColor = vec3(1.0, 0.8, 0.2);
}
