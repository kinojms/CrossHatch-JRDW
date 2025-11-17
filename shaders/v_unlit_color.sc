#ifdef GL_ES
precision mediump float;
attribute vec3 a_position;
attribute vec4 a_color0;
varying vec4 v_color0;
#else
in vec3 a_position;
in vec4 a_color0;
out vec4 v_color0;
#endif

#include <bgfx_shader.sh>

void main()
{
    // Same transform path as your main vertex shader
    vec4 worldPos = u_model[0] * vec4(a_position, 1.0);
    gl_Position   = u_viewProj * worldPos;

    // Pass raw vertex color through
    v_color0 = a_color0;
}