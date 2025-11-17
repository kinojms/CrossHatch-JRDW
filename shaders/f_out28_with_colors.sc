#ifdef GL_ES
precision mediump float;
varying vec3 v_normal;
varying vec3 v_worldPos;
varying vec4 v_color;
varying vec2 v_texcoord0;
#else
in vec3 v_normal;
in vec3 v_worldPos;
in vec4 v_color;
in vec2 v_texcoord0;
#endif

#include <bgfx_shader.sh>

// ----- Lighting uniforms -----
uniform vec4 u_lights[64];
uniform vec4 u_numLights;

// ----- Uniforms for crosshatching effect -----
uniform vec4 u_tint;
uniform vec4 u_inkColor;
uniform vec4 u_cameraPos;
uniform vec4 u_e;
uniform sampler2D u_noiseTex;

// ----- Uniform for cross-hatch parameters -----
uniform vec4 u_params;

// ----- Extra parameters as a vec4 -----
uniform vec4 u_extraParams;

// ----- Diffuse texture -----
uniform sampler2D u_diffuseTex;

// ----- Uniform for the object's override color -----
uniform vec4 u_objectColor;

// ----- Extra parameters for 2nd Layer as a vec4 -----
uniform vec4 u_paramsLayer;

// Simple lighting function
vec3 calculateLighting(vec3 normal, vec3 worldPos, vec3 baseColor) {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0)); // Simple directional light
    float diff = max(dot(normal, lightDir), 0.0);
    return baseColor * (0.3 + 0.7 * diff); // Ambient + diffuse
}

void main() {
    vec3 normal = normalize(v_normal);
    
    // Use vertex color as base color
    vec3 baseColor = v_color.rgb;
    
    // Apply simple lighting
    vec3 litColor = calculateLighting(normal, v_worldPos, baseColor);
    
    // Apply object color override
    vec3 finalColor = litColor * u_objectColor.rgb;
    
    gl_FragColor = vec4(finalColor, v_color.a);
}