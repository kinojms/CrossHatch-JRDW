#ifdef GL_ES
precision mediump float;
varying vec3 v_normal;
varying vec3 v_pos;
#else
in vec3 v_normal;
in vec3 v_pos;
#endif

#include <bgfx_shader.sh>

void main()
{
    vec3 N = normalize(v_normal);
    // Simple debug visualization - use normal as color
    vec3 color = N * 0.5 + 0.5;
    gl_FragColor = vec4(color, 1.0);
}



