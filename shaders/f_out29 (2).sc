$input v_pos, v_normal, v_view, v_texcoord0, v_shadowcoord

#include <bgfx_shader.sh>
// --- SHADOW CONFIG ---
#define SHADOW_PACKED_DEPTH 0 // Use hardware depth comparison (Sampler2DShadow)
#include "fs_sms_shadow.sh"   // Include the BGFX helper you uploaded

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

// Add these new uniforms at the top of f_out29.sc
uniform vec4 u_shadowParams1; // x=Scale, y=Density, z=Angle, w=Thickness
uniform vec4 u_shadowParams2; // x=Epsilon (Smoothness)

// ----- Helper functions for crosshatching -----
float calcLuma(vec3 color) {
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
    //vec3 baseColor = texture2D(u_diffuseTex, v_texcoord0).rgb;
    
    vec3 lighting = vec3(0.0);
    int numLights = int(u_numLights.x);

    // --- SHADOW CALCULATION ---
    // 1. Get Softness from u_e.w (Default to 1.0 if 0)
    float softness = max(u_e.w, 1.0);

    // 2. Calculate Shadow with Variable Spread
    // Multiplying the texel size by 'softness' spreads the samples further apart.
    // This creates a wider gradient at the shadow edges.
    float shadowVal = PCF(s_shadowMap, v_shadowcoord, 0.002, vec2(1.0/2048.0, 1.0/2048.0) * softness);

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
        float attenuation = 1.0; // Defined HERE so it is visible in the entire loop
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
            attenuation = clamp(1.0 - distance / range, 0.0, 1.0);
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

        lighting += lightColor * intensity * diff * attenuation;
    }
    
    // --- Texture/Material ---
    // 1. Tiling & offset:
    //    scale = (tilingU, tilingV), offset = (offsetU, offsetV)
    vec2 uvScaled = v_texcoord0 * u_uvTransform.xy + u_uvTransform.zw;

    // 2. Sample the diffuse texture with that transformed UV:
    vec4 texSample = texture2D(u_diffuseTex, uvScaled);

    // 3. Apply the color tint:
    //    multiply the texture color by the albedoFactor.rgb
    //    (optionally also multiply alpha if you want)
    vec3 tintedBase = texSample.rgb * u_albedoFactor.rgb;

    // 4 Combine tintedBase with your crosshatch logic:
    //    e.g., litColor = tintedBase * lighting, then crosshatching...
    //    or tintedBase * (some lighting factor)...
    vec3 litColor = tintedBase * lighting;
    
    // --- Crosshatch Effect Selection ---
    int mode = int(u_extraParams.w);
    vec3 crossColor;

    // Unpack Config
    float shadowHatchIntensity = u_e.y; // Shadow strength from C++
    float paperOpacity = u_e.z;         // Paper opacity from C++ (New!)

    // Modify Luminance based on Shadow Map
    float realLum = calcLuma(litColor);
    float shadowFactor = mix(1.0 - shadowHatchIntensity, 1.0, shadowVal);
    float lumVal = realLum * shadowFactor;

    // Track how much "Ink" we generate (0.0 = Empty, 1.0 = Solid Ink)
    float inkFactor = 0.0;

    //mode 0 is original
    if(mode == 0){
        // --- Foundation Crosshatch Effect ---
        float lVal = 1.0 - lumVal;
        float darks = 1.0 - 2.0 * lumVal;
        float strokeMult = u_params.y;
        vec3 p_scaled = v_pos * u_extraParams.x;

        float line = texcube(p_scaled, N, lVal * strokeMult * u_extraParams.y, u_params.z);
        float lineDark = texcube(p_scaled, N, darks * strokeMult * u_extraParams.y, u_params.w);
        
        float r = 1.0 - smoothstep(lVal - u_e.x, lVal + u_e.x, line);
        float rDark = 1.0 - smoothstep(lVal - u_e.x, lVal + u_e.x, lineDark);
        
        // Combine layers for ink factor
        inkFactor = max(r, rDark); 

        vec3 inkedColor = blendDarken(litColor, u_inkColor.xyz, 0.5 * r);
        crossColor = mix(inkedColor, u_inkColor.xyz, rDark);

    }
    else if (mode == 1){
        // --- Modified Crosshatch Effect ---
        float lVal = 1.0 - lumVal;
        vec3 p_scaled = v_pos * u_extraParams.x;
        float line = texcube(p_scaled, N, lVal * u_params.y * u_extraParams.y, u_params.z);
        float r = 1.0 - smoothstep(lVal - u_e.x, lVal + u_e.x, line);
        
        inkFactor = r;
        crossColor = mix(litColor, u_inkColor.xyz, r);

    }else if(mode == 2){
        // --- Another Modified Crosshatch Effect ---

        vec3 p_scaled = v_pos * u_extraParams.x;
        // Distance fix for mode 2
        if (mode == 2) {
             float dist = length(u_cameraPos.xyz - v_pos);
             float factor = 2.0 / max(dist, 2.0);
             p_scaled *= factor;
        }

        float lVal = 1.0 - lumVal;
        float line = texcube(p_scaled, N, lVal * u_params.y * u_extraParams.y, u_params.z);
        float r = 1.0 - smoothstep(lVal - u_e.x, lVal + u_e.x, line);

        // Layer 2
        vec3 p_scaled2 = v_pos * u_paramsLayer.x;
        if (mode == 2) {
             float dist = length(u_cameraPos.xyz - v_pos);
             float factor = 2.0 / max(dist, 2.0);
             p_scaled2 *= factor;
        }
        float line2 = texcube(p_scaled2, N, u_paramsLayer.y * u_paramsLayer.w, u_paramsLayer.z);
        float r2 = 1.0 - step(0.5, line2); 
        
        // Combine ink density
        inkFactor = max(r, r2);

        vec3 crosshatch = mix(litColor, u_inkColor.xyz, r);
        crossColor = mix(crosshatch, u_inkColor.xyz, r2);

    }else if(mode == 3){
        // ----------------------------------------------------
        // 1. LIT HATCHING (Standard)
        // ----------------------------------------------------
        float lValLit = 1.0 - realLum; 
        
        // Lit Layer 1
        vec3 p_lit = v_pos * u_extraParams.x;
        float lineLit = texcube(p_lit, N, lValLit * u_params.y * u_extraParams.y, u_params.z);
        float rLit = 1.0 - smoothstep(lValLit - u_e.x, lValLit + u_e.x, lineLit);

        // Lit Layer 2
        vec3 p_lit2 = v_pos * u_paramsLayer.x;
        float lineLit2 = texcube(p_lit2, N, u_paramsLayer.y * u_paramsLayer.w, u_paramsLayer.z);
        float rLit2 = 1.0 - step(0.5, lineLit2);
        
        float inkLit = max(rLit, rLit2);

        // ----------------------------------------------------
        // 2. SHADOW HATCHING (Correction Applied)
        // ----------------------------------------------------
        // Raw Shadow: 0.0 (Lit) -> 1.0 (Dark)
        float rawShadow = 1.0 - shadowVal;
        
        // --- CORE CORRECTION ---
        // We use the new Core Bias (u_shadowParams2.y) to adjust the curve.
        // If bias is low (e.g. 0.3), pow(0.5, 0.3) becomes ~0.8.
        // This means a "half-shadow" (blurry area) becomes "mostly dark".
        // This prevents the shadow from shrinking when you increase softness.
        float coreBias = max(u_shadowParams2.y, 0.01);
        float shadowStrength = pow(rawShadow, coreBias);
        
        // Apply global intensity slider
        shadowStrength *= shadowHatchIntensity;

        // Shadow Params
        float shadScale = u_shadowParams1.x;
        float shadDensity = u_shadowParams1.y;
        float shadAngle = u_shadowParams1.z;
        float shadThick = u_shadowParams1.w;
        float shadEpsilon = u_shadowParams2.x;

        // Calculate Shadow Pattern
        // The thickness/density is driven by our corrected 'shadowStrength'.
        // Edges will fade out naturally because shadowStrength drops to 0.0 at the far edge.
        vec3 p_shad = v_pos * shadScale;
        float lineShad = texcube(p_shad, N, shadowStrength * shadDensity * shadThick, shadAngle);
        float rShad = 1.0 - smoothstep(shadowStrength - shadEpsilon, shadowStrength + shadEpsilon, lineShad);

        // ----------------------------------------------------
        // 3. COMBINE (Layering)
        // ----------------------------------------------------
        // We use max() to layer the ink. 
        // This removes the "ghosting" or "blur" effect you saw in Image 2.
        inkFactor = max(inkLit, rShad);

        crossColor = mix(litColor, u_inkColor.xyz, inkFactor);

    }else if(mode == 4){
        //default/simple lighting system
        crossColor = litColor * (0.2 + 0.8 * shadowVal);
        inkFactor = 1.0; // Solid object
    }
    
    // Blend the crosshatch with the lit color (adjust blend factor as desired)
    vec3 finalColor = mix(litColor, crossColor, u_extraParams.z);
    
    // --- Apply the object color override:
    finalColor *= u_objectColor.rgb;
    
    // Calculate Alpha:
    // If paperOpacity is 1.0, alpha is always 1.0 (Solid).
    // If paperOpacity is 0.0, alpha depends entirely on inkFactor (Transparent paper).
    // We assume 'u_tint.a' is an overall fade, but usually 1.0.
    float finalAlpha = clamp(paperOpacity + inkFactor, 0.0, 1.0);
    
    gl_FragColor = vec4(finalColor, finalAlpha);

    //vec4 finalColor4 = vec4(finalColor, 1.0);
    //gl_FragColor = mix(finalColor4, u_tint, u_tint.a);
}