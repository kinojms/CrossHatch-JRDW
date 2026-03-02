$input a_position, a_normal, a_texcoord0
$output v_pos, v_normal, v_view, v_texcoord0, v_shadowcoord

#include <bgfx_shader.sh>

uniform mat4 u_lightMtx;

void main()
{
    // 1. World Position
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    v_pos = worldPos.xyz;
    
    // 2. Normal
    v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    
    // 3. View Vector (Required by some BGFX helpers)
    v_view = mul(u_modelView, vec4(a_position, 1.0)).xyz;

    // 4. Texture Coord
    v_texcoord0 = a_texcoord0;

    // 5. Shadow Coord
    const float shadowMapOffset = 0.002;
    vec3 posOffset = worldPos.xyz + (v_normal * shadowMapOffset);
    v_shadowcoord = mul(u_lightMtx, vec4(posOffset, 1.0));

    gl_Position = mul(u_viewProj, worldPos);
}