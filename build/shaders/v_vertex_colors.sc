#ifdef GL_ES
precision mediump float;
attribute vec3 a_position;
attribute vec3 a_normal;
attribute vec4 a_color;
attribute vec2 a_texcoord0;
varying vec3 v_worldPos;
varying vec3 v_normal;
varying vec4 v_color;
varying vec2 v_texcoord0;
#else
in vec3 a_position;
in vec3 a_normal;
in vec4 a_color;
in vec2 a_texcoord0;
out vec3 v_worldPos;
out vec3 v_normal;
out vec4 v_color;
out vec2 v_texcoord0;
#endif

#include <bgfx_shader.sh>

void main()
{
    vec3 worldPos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
    gl_Position = mul(u_viewProj, vec4(worldPos, 1.0));
    
    v_worldPos = worldPos;
    v_normal = mul((mat3)u_model[0], a_normal).xyz;
    v_color = a_color;
    v_texcoord0 = a_texcoord0;
}

