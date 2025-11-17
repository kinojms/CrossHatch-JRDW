#ifdef GL_ES
precision mediump float;
varying vec4 v_color;
#else
in vec4 v_color;
#endif

#include <bgfx_shader.sh>

void main() {
    gl_FragColor = v_color;
}