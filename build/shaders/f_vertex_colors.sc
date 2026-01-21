$input v_color
$input v_worldPos
$input v_normal
$input v_texcoord0

#include <bgfx_shader.sh>

// ----- Lighting uniforms -----
uniform vec4 u_lights[64];
uniform vec4 u_numLights;

// ----- Helper functions -----
float luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main()
{
	// --- Lighting Calculation ---
	vec3 N = normalize(v_normal);
	
	vec3 lighting = vec3(0.0);
	int numLights = int(u_numLights.x);
	for (int i = 0; i < numLights; i++) {
		int offset = i * 4;
		float lightType = u_lights[offset].x; // 0: directional, 1: point, 2: spot
		float intensity = u_lights[offset].y;
		vec3 lightPos = u_lights[offset+1].xyz;
		vec3 lightDir = normalize(u_lights[offset+2].xyz);
		float coneAngle = u_lights[offset+2].w;
		vec3 lightColor = u_lights[offset+3].rgb;
		float range = u_lights[offset+3].w;
		
		vec3 L;
		if (lightType == 0.0) { // directional
			L = -lightDir;
		} else {
			L = normalize(lightPos - v_worldPos);
		}
		float diff = max(dot(N, L), 0.0);

		// For point and spot lights, apply attenuation
		if (lightType != 0.0) {
			float distance = length(lightPos - v_worldPos);
			float attenuation = clamp(1.0 - distance / range, 0.0, 1.0);
			diff *= attenuation;
		}

		// If this is a spot light, apply a cutoff and smooth falloff
		if (lightType == 2.0) {
			float theta = dot(normalize(-L), lightDir);
			float cutoff = cos(coneAngle);
			if (theta < cutoff) {
				diff = 0.0;
			} else {
				diff *= smoothstep(cutoff, 1.0, theta);
			}
		}

		lighting += lightColor * intensity * diff;
	}

	// Apply lighting to vertex color
	vec3 litColor = v_color.rgb * lighting;
	
	// Output lit vertex color
	gl_FragColor = vec4(litColor, v_color.a);
}