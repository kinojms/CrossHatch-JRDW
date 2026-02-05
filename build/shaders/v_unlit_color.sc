// Use bgfx's varying.def.sc mechanism for attributes/varyings.
$input a_position, a_color0
$output v_color0

#include <bgfx_shader.sh>

void main()
{
    vec4 worldPos = u_model[0] * vec4(a_position, 1.0);
    gl_Position   = u_viewProj * worldPos;

    v_color0 = a_color0;
}