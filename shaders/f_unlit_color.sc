#ifdef GL_ES
precision mediump float;
varying vec4 v_color0;
#else
in vec4 v_color0;
#endif

#include <bgfx_shader.sh>

void main()
{
    // Unlit: just output the interpolated vertex color
    gl_FragColor = v_color0;
}

