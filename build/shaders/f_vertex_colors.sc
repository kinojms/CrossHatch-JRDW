$input v_normal, v_worldPos, v_color, v_texcoord0

#include <bgfx_shader.sh>

// ----- Lighting uniforms -----
uniform vec4 u_lights[64];
uniform vec4 u_numLights;

void main()
{
    // --- Lighting Calculation ---
    vec3 N = normalize(v_normal);
    vec3 baseColor = v_color.rgb; // Use vertex color as base material
    
    vec3 lighting = vec3(0.0);
    int numLights = int(u_numLights.x);
    for (int i = 0; i < numLights; i++) {
        int offset = i * 4;
        float lightType = u_lights[offset].x; // 0: directional, 1: point, 2: spot.
        float intensity = u_lights[offset].y;
        vec3 lightPos = u_lights[offset+1].xyz;
        vec3 lightDir = normalize(u_lights[offset+2].xyz);
        float coneAngle = u_lights[offset+2].w; // For spotlights if needed
        vec3 lightColor = u_lights[offset+3].rgb;
        float range = u_lights[offset+3].w; // For attenuation if needed
        
        vec3 L;
        if (lightType == 0.0) { // directional
            L = -lightDir;
        } else {
            L = normalize(lightPos - v_worldPos);
        }
        float diff = max(dot(N, L), 0.0);

        // For point and spot lights, apply attenuation.
        if (lightType != 0.0) {
            float distance = length(lightPos - v_worldPos);
            // Simple linear attenuation (clamped)
            float attenuation = clamp(1.0 - distance / range, 0.0, 1.0);
            diff *= attenuation;
        }

        // If this is a spot light, apply a cutoff and smooth falloff.
        if (lightType == 2.0) {
            float spotEffect = dot(-L, lightDir);
            float spotCutoff = cos(coneAngle);
            if (spotEffect > spotCutoff) {
                spotEffect = smoothstep(spotCutoff, spotCutoff + 0.1, spotEffect);
                diff *= spotEffect;
            } else {
                diff = 0.0;
            }
        }

        lighting += lightColor * diff * intensity;
    }
    
    // Apply ambient lighting (minimum visibility)
    vec3 ambient = vec3(0.1, 0.1, 0.1);
    vec3 finalColor = baseColor * (ambient + lighting);
    
    gl_FragColor = vec4(finalColor, v_color.a);
}