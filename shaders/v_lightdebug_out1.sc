#ifdef GL_ES
precision mediump float;
attribute vec3 a_position;
attribute vec3 a_normal;
varying vec3 v_normal;
varying vec3 v_pos;
#else
in vec3 a_position;
in vec3 a_normal;
out vec3 v_normal;
out vec3 v_pos;
#endif

#include <bgfx_shader.sh>

void main()
{
    vec3 worldPos = (u_model[0] * vec4(a_position, 1.0)).xyz;
    gl_Position = u_viewProj * vec4(worldPos, 1.0);
    
    v_pos = worldPos;
    v_normal = (mat3(u_model[0]) * a_normal).xyz;
}



