#ifdef GL_ES
precision mediump float;
varying vec3 v_normal;
varying vec3 v_pos;
varying vec4 v_color;
varying vec2 v_texcoord0;
#else
in vec3 v_normal;
in vec3 v_pos;
in vec4 v_color;
in vec2 v_texcoord0;
#endif

#include <bgfx_shader.sh>

// ----- Lighting uniforms -----
// Each light is packed into 4 vec4’s. For example, with MAX_LIGHTS=16, you’ll have 64 vec4’s.
uniform vec4 u_lights[64];
uniform vec4 u_numLights; // x component holds the number of lights.

// ----- Uniforms for crosshatching effect -----
uniform vec4 u_tint;
uniform vec4 u_inkColor;    // Ink color for crosshatching
uniform vec4 u_cameraPos;   // Camera position (if needed for hatch calculations)
uniform vec4 u_e;           // Epsilon value in u_e.x (for smoothstep)
uniform sampler2D u_noiseTex; // Noise texture for crosshatching

// ----- Uniform for cross-hatch parameters -----
// u_params.y = stroke multiplier (default 5.0)
// u_params.z = first hatch angle factor (default TAU/8)
// u_params.w = second hatch angle factor (default TAU/16)
uniform vec4 u_params;

// ----- Extra parameters as a vec4 -----
// u_extraParams.x = pattern scale
// u_extraParams.y = line thickness
// u_extraParams.z = transparencyValue
// u_extraParams.w = crosshatch mode or switch type of shader
uniform vec4 u_extraParams;

// ----- Diffuse texture -----
uniform sampler2D u_diffuseTex;

// ----- Uniform for the object’s override color -----
uniform vec4 u_objectColor;

// ----- Extra parameters for 2nd Layer as a vec4 -----
// u_paramsLayer.x = pattern scale
// u_paramsLayer.y = stroke
// u_paramsLayer.z = angle
// u_paramsLayer.w = line thickness
uniform vec4 u_paramsLayer;

// NEW uniforms for tiling/offset and albedo factor:
uniform vec4 u_uvTransform;   // (tilingU, tilingV, offsetU, offsetV)
uniform vec4 u_albedoFactor;  // (r, g, b, a) color tint

// ----- Helper functions for crosshatching -----
float luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 blendDarken(vec3 base, vec3 ink, float opacity) {
    return mix(base, min(base, ink), opacity);
}

float aastep(float threshold, float value) {
#ifdef GL_OES_standard_derivatives
    float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678;
    return smoothstep(threshold - afwidth, threshold + afwidth, value);
#else
    return step(threshold, value);
#endif  
}

float texh(in vec2 p, in float str) {
    float rz = 1.0;
    for (int i = 0; i < 10; i++) {
        float g = texture2D(u_noiseTex, vec2(0.025, 0.5) * p).r;
        g = smoothstep(0.0 - str * 0.1, 2.3 - str * 0.1, g);
        rz = min(1.0 - g, rz);
        p = p.yx;
        p += 0.7;
        p *= 1.52;
        if (float(i) > str)
            break;
    }
    return rz * 1.05;
}

float texcube(in vec3 p, in vec3 n, in float str, float a) {
    float s = sin(a);
    float c = cos(a);
    mat2 rot = mat2(c, -s, s, c);
    vec3 v;
    v.x = texh(rot * p.yz, str);
    v.y = texh(rot * p.zx, str);
    v.z = texh(rot * p.xy, str);
    return dot(v, n * n);
}

void main()
{
    // --- Lighting Calculation ---
    vec3 N = normalize(v_normal);
    
    // Try inverting normals if they point the wrong direction
    N = -N;
    
    // DEBUG: Visualize normals if needed (check if normals are correct)
    // Uncomment next line to see normals as colors (mostly blue = pointing away)
    // gl_FragColor = vec4(N * 0.5 + 0.5, 1.0); return;
    
    // DEBUG: Visualize v_pos if needed (should show gradient based on position)
    // Uncomment next line to verify v_pos is being passed correctly
    // gl_FragColor = vec4(abs(v_pos) * 0.1, 1.0); return;
    
    // Debug: Check if vertex color is being received
    // If v_color is white (1,1,1,1), it means no vertex color data
    // If v_color has actual colors, use them
    
    vec3 lighting = vec3(0.0);
    int numLights = int(u_numLights.x);
    
    // DEBUG: If no lights, output red warning
    if (numLights == 0)
    {
        // gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); // RED = NO LIGHTS
        // return;
    }
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
            L = normalize(lightPos - v_pos);
        }
        float diff = max(dot(N, L), 0.0);

        // For point and spot lights, apply attenuation.
        if (lightType != 0.0) {
            float distance = length(lightPos - v_pos);
            // Simple linear attenuation (clamped)
            float attenuation = clamp(1.0 - distance / range, 0.0, 1.0);
            diff *= attenuation;
        }

        // If this is a spot light, apply a cutoff and smooth falloff.
        if (lightType == 2.0) {
            // Compute the angle between the light's direction and the direction from light to fragment.
            // Here, lightDir should be the direction the spotlight is facing.
            float theta = dot(normalize(-L), lightDir);
            float cutoff = cos(coneAngle);
            if (theta < cutoff) {
                diff = 0.0;
            } else {
                // Optionally smooth the edge of the spotlight.
                //float epsilon = 0.1; // adjust as needed for softness
                //diff *= smoothstep(cutoff, cutoff + epsilon, theta);
                diff *= smoothstep(cutoff, 1.0, theta);
            }
        }

        lighting += lightColor * intensity * diff;
    }
    
    // Add ambient lighting to ensure vertex colors are visible even without lights
    vec3 ambient = vec3(0.2, 0.2, 0.2); // Minimum ambient light
    lighting = max(lighting, ambient);
    
    // DEBUG: Visualize the calculated lighting value
    // Uncomment to see raw lighting contribution (red = no light, green/white = bright)
    // gl_FragColor = vec4(lighting, 1.0); return;
    
    // --- Texture/Material ---
    // 1. Tiling & offset:
    //    scale = (tilingU, tilingV), offset = (offsetU, offsetV)
    vec2 uvScaled = v_texcoord0 * u_uvTransform.xy + u_uvTransform.zw;

    // 2. Sample the diffuse texture with that transformed UV:
    vec4 texSample = texture2D(u_diffuseTex, uvScaled);

    // 3. Prioritize vertex colors - use them directly if present, otherwise use texture
    //    Vertex colors from OBJ files should be displayed as-is
    vec3 vertexColorMod = v_color.rgb;
    
    // Check if vertex color is not white (has actual color data)
    // OBJ vertex colors are typically in 0-1 range, so check if significantly different from white
    // Use a threshold to detect non-white colors (accounting for floating point precision)
    // Since debug shows colors like RGBA(0.068, 0.051, 0.025, 1), these are clearly not white
    float colorThreshold = 0.99; // If any component is less than 0.99, consider it colored
    bool hasVertexColor = (vertexColorMod.r < colorThreshold || vertexColorMod.g < colorThreshold || vertexColorMod.b < colorThreshold);
    
    // DEBUG: Force use vertex colors if they're significantly different from white
    // This ensures vertex colors from OBJ files are always used
    vec3 baseColor;
    if (hasVertexColor) {
        // Vertex has color - use it directly, don't multiply by white texture
        baseColor = vertexColorMod;
    } else {
        // No vertex color - use texture
        baseColor = texSample.rgb;
    }

    // 4. Apply the color tint:
    //    Only apply albedoFactor if vertex colors aren't present (to preserve vertex colors)
    //    If vertex has color, use it directly; otherwise apply albedo tint
    vec3 tintedBase = hasVertexColor ? baseColor : (baseColor * u_albedoFactor.rgb);

    // 4 Combine tintedBase with your crosshatch logic:
    //    For vertex colors, ensure they're visible even with low lighting
    //    Multiply by lighting, but ensure minimum visibility for vertex colors
    vec3 litColor;
    if (hasVertexColor) {
        // For vertex colors, apply lighting but ensure they remain visible
        // Use max to ensure colors don't go completely black
        // Add ambient to lighting to ensure colors are always visible
        vec3 finalLighting = max(lighting, ambient);
        litColor = tintedBase * finalLighting;
        
        // DEBUG: Show vertex color being used
        // gl_FragColor = vec4(tintedBase, 1.0); return;
    } else {
        // For textures, use normal lighting with ambient
        litColor = tintedBase * max(lighting, ambient);
    }
    
    //vec3 litColor = baseColor * lighting;
    
    // --- Crosshatch Effect Selection ---
    int mode = int(u_extraParams.w);
    vec3 crossColor;

    //mode 0 is original
    if(mode == 0){
        // --- Crosshatch Effect ---
        float lumVal = luma(litColor);
        float lVal = 1.0 - lumVal;
        float darks = 1.0 - 2.0 * lumVal;

        // Use the stroke multiplier and angle factors from u_params:
        float strokeMult = u_params.y;
        float angle1 = u_params.z;
        float angle2 = u_params.w;

        // Scale the world position by the pattern scale (u_extraParams.x)
        vec3 p_scaled = v_pos * u_extraParams.x;

        // Multiply the stroke multiplier by the line thickness factor (u_extraParams.y)
        float line = texcube(p_scaled, N, lVal * strokeMult * u_extraParams.y, angle1);
        float lineDark = texcube(p_scaled, N, darks * strokeMult * u_extraParams.y, angle2);
        
        float epsilon = u_e.x;
        float r = 1.0 - smoothstep(lVal - epsilon, lVal + epsilon, line);
        float rDark = 1.0 - smoothstep(lVal - epsilon, lVal + epsilon, lineDark);
        
        //mix(litColor, u_inkColor.xyz, r);
        vec3 inkedColor = blendDarken(litColor, u_inkColor.xyz, 0.5 * r);
        //vec3 crossColor = mix(inkedColor, u_inkColor.xyz, rDark);
        crossColor = mix(inkedColor, u_inkColor.xyz, rDark);

    }
    else if (mode == 1){
        // --- Modified Crosshatch Effect ---
        float lumVal = luma(litColor);
        float lVal = 1.0 - lumVal;

        // Use the stroke multiplier and angle factors from u_params:
        float strokeMult = u_params.y;
        float angle1 = u_params.z;

        // Scale the world position by the pattern scale (u_extraParams.x)
        vec3 p_scaled = v_pos * u_extraParams.x;

        // Multiply the stroke multiplier by the line thickness factor (u_extraParams.y)
        float line = texcube(p_scaled, N, lVal * strokeMult * u_extraParams.y, angle1);
        
        float epsilon = u_e.x;
        float r = 1.0 - smoothstep(lVal - epsilon, lVal + epsilon, line);
        
        crossColor = mix(litColor, u_inkColor.xyz, r);

    }else if(mode == 2){
        // --- Another Modified Crosshatch Effect ---

        // --- Layer 1 ---
        // Partial distance compensation:
        // 1. measure distance from camera to fragment
        float dist = length(u_cameraPos.xyz - v_pos);

        // 2. pick a "referenceDist" so that if dist >= referenceDist, the pattern doesn't shrink further
        float referenceDist = 2.0;  // e.g. 5.0 or 10.0

        // 3. compute a factor that is 1.0 when dist <= referenceDist, and smaller if you come closer
        float factor = referenceDist / max(dist, referenceDist);

        // 4. combine with your usual crosshatch scale
        float finalScale = u_extraParams.x * factor;

        // anchor in world space
        vec3 p_scaled = v_pos * finalScale;

        float lumVal = luma(litColor);
        float lVal   = 1.0 - lumVal;

        float strokeMult = u_params.y;
        float angle1     = u_params.z;
        
        float line    = texcube(p_scaled, N, lVal * strokeMult * u_extraParams.y, angle1);
        float epsilon = u_e.x;
        float r = 1.0 - smoothstep(lVal - epsilon, lVal + epsilon, line);
        // Force a minimum hatch effect regardless of brightness:
        // r = max(r, 0.2);

        // --- Layer 2 ---
        // strokeMult2 = 0.3, angle1test = 2.983, thickness 10.0, p_scaled2 = v_pos * 0.3

        float layerPatternScale = u_paramsLayer.x;
        float layerStrokeMult = u_paramsLayer.y;
        float layerAngle = u_paramsLayer.z;

        float finalScale2 = layerPatternScale * factor;
        vec3 p_scaled2 = v_pos * finalScale2;

        float line2 = texcube(p_scaled2, N, layerStrokeMult * u_paramsLayer.w, layerAngle);
        float r2 = 1.0 - step(0.5, line2);
        
        vec3 crosshatch = mix(litColor, u_inkColor.xyz, r);

        crossColor =  mix(crosshatch, u_inkColor.xyz, r2);

    }else if(mode == 3){
        // --- Layer 1 ---
        // anchor in world space
        vec3 p_scaled = v_pos * u_extraParams.x;

        float lumVal = luma(litColor);
        float lVal   = 1.0 - lumVal;

        float strokeMult = u_params.y;
        float angle1     = u_params.z;
        
        float line    = texcube(p_scaled, N, lVal * strokeMult * u_extraParams.y, angle1);
        float epsilon = u_e.x;
        float r = 1.0 - smoothstep(lVal - epsilon, lVal + epsilon, line);

        // --- Layer 2 ---
        // strokeMult2 = 0.3, angle1test = 2.983, thickness 10.0, p_scaled2 = v_pos * 0.3
        float layerPatternScale = u_paramsLayer.x;
        float layerStrokeMult = u_paramsLayer.y;
        float layerAngle = u_paramsLayer.z;

        vec3 p_scaled2 = v_pos * layerPatternScale;

        float line2 = texcube(p_scaled2, N, layerStrokeMult * u_paramsLayer.w, layerAngle);
        float r2 = 1.0 - step(0.5, line2);
        
        vec3 crosshatch = mix(litColor, u_inkColor.xyz, r);

        crossColor =  mix(crosshatch, u_inkColor.xyz, r2);
    }else if(mode == 4){
        //default/simple lighting system
        // For vertex colors, ensure they're visible even with mode 4
        if (hasVertexColor) {
            // Use vertex colors directly with minimal processing
            crossColor = hasVertexColor ? vertexColorMod : litColor;
        } else {
            crossColor = litColor;
        }
    }
    
    // Blend the crosshatch with the lit color (adjust blend factor as desired)
    vec3 finalColor = mix(litColor, crossColor, u_extraParams.z);
    
    // --- Apply the object color override:
    // Only apply object color if it's not white (to preserve vertex colors)
    // If object color is white, it means no override, so use vertex colors as-is
    // For vertex colors, skip object color override to preserve the original colors
    if (!hasVertexColor && (u_objectColor.r < 0.99 || u_objectColor.g < 0.99 || u_objectColor.b < 0.99)) {
        finalColor *= u_objectColor.rgb;
    }
    
    // Final fallback: If we have vertex colors but they're not showing, use them directly
    // This ensures vertex colors are always visible
    if (hasVertexColor && dot(finalColor, vec3(1.0)) < 0.01) {
        // If final color is nearly black, use vertex color directly with ambient
        finalColor = vertexColorMod * ambient;
    }
    
    vec4 finalColor4 = vec4(finalColor, v_color.a);
    gl_FragColor = mix(finalColor4, u_tint, u_tint.a);
}
