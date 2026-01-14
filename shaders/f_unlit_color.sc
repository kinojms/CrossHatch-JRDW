// Use bgfx's varying.def.sc mechanism for varyings.
$input v_color0

#include <bgfx_shader.sh>

void main()
{
    // Unlit: just output the interpolated vertex color
    gl_FragColor = v_color0;
}
