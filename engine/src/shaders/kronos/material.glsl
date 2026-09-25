#ifndef KRONOS_MATERIAL_GLSL
#define KRONOS_MATERIAL_GLSL

// Surface model: metallic/roughness base layer with optional clearcoat,
// sheen and anisotropy. Requires brdf.glsl and a bound dfgLut.

#include "brdf.glsl"

struct PixelParams {
    vec3 diffuseColor;
    float perceptualRoughness;
    float roughness;
    vec3 f0;
    vec3 dfg;                 // x: A (F0 scale), y: B (F0 bias), z: sheen albedo -- at (NoV, perceptualRoughness)
    vec3 energyCompensation;  // Kulla-Conty style multiscatter scale for single-scatter GGX
    float clearcoat;
    float clearcoatPerceptualRoughness;
    float clearcoatRoughness;
    vec3 sheenColor;
    float sheenPerceptualRoughness;
    float sheenRoughness;
    float sheenDFG;
    float sheenScaling;
    float anisotropy;
    vec3 anisotropicT;
    vec3 anisotropicB;
};

struct ShadingContext {
    vec3 worldPos;
    vec3 N;       // shading normal (normal-mapped)
    vec3 Ngeo;    // interpolated geometric normal
    vec3 V;
    float NoV;
    vec2 pixel;
};

vec3 sampleDFG(float NoV, float perceptualRoughness) {
    return textureLod(dfgLut, vec2(NoV, perceptualRoughness), 0.0).rgb;
}

// Kaplanyan & Hoffman 2016 / Tokuyoshi: widen roughness by the screen-space
// variance of the normal so sub-pixel curvature cannot produce specular
// aliasing (fireflies that TAA cannot resolve).
float specularAntiAliasing(vec3 N, float perceptualRoughness) {
    const float kVariance = 0.15;
    const float kThreshold = 0.2;
    vec3 du = dFdx(N);
    vec3 dv = dFdy(N);
    float variance = kVariance * (dot(du, du) + dot(dv, dv));
    float roughness = perceptualRoughness * perceptualRoughness;
    float kernelRoughness = min(2.0 * variance, kThreshold);
    return sqrt(sqrt(saturate(roughness * roughness + kernelRoughness)));
}

void finalizePixelParams(inout PixelParams p, float NoV) {
    p.perceptualRoughness = clamp(p.perceptualRoughness, kMinPerceptualRoughness, 1.0);
    p.roughness = p.perceptualRoughness * p.perceptualRoughness;
    p.dfg = sampleDFG(NoV, p.perceptualRoughness);
    float Ess = p.dfg.x + p.dfg.y;
    p.energyCompensation = 1.0 + p.f0 * (1.0 / max(Ess, 1e-3) - 1.0);

    if (p.clearcoat > 0.0) {
        p.clearcoatPerceptualRoughness = clamp(p.clearcoatPerceptualRoughness, kMinPerceptualRoughness, 1.0);
        p.clearcoatRoughness = p.clearcoatPerceptualRoughness * p.clearcoatPerceptualRoughness;
        // A clearcoat interface (IOR 1.5) over the base changes the base's
        // own F0 as seen from inside the coat (Filament's f0ClearCoatToSurface).
        vec3 f0 = p.f0;
        vec3 t = f0 * (f0 * (0.941892 - 0.263008 * f0) + 0.346479) - 0.0285998;
        p.f0 = mix(f0, saturate(t), p.clearcoat);
    }

    if (max(p.sheenColor.r, max(p.sheenColor.g, p.sheenColor.b)) > 0.0) {
        p.sheenPerceptualRoughness = clamp(p.sheenPerceptualRoughness, kMinPerceptualRoughness, 1.0);
        p.sheenRoughness = p.sheenPerceptualRoughness * p.sheenPerceptualRoughness;
        p.sheenDFG = sampleDFG(NoV, p.sheenPerceptualRoughness).z;
        p.sheenScaling = 1.0 - max(p.sheenColor.r, max(p.sheenColor.g, p.sheenColor.b)) * p.sheenDFG;
    } else {
        p.sheenScaling = 1.0;
        p.sheenDFG = 0.0;
    }
}

vec3 specularLobe(ShadingContext s, PixelParams p, vec3 L, vec3 H, float NoL, float NoH, float LoH) {
    float D;
    float Vis;
    if (p.anisotropy != 0.0) {
        float at = max(p.roughness * (1.0 + p.anisotropy), kMinRoughness);
        float ab = max(p.roughness * (1.0 - p.anisotropy), kMinRoughness);
        vec3 T = p.anisotropicT;
        vec3 B = p.anisotropicB;
        D = D_GGX_Anisotropic(NoH, dot(T, H), dot(B, H), at, ab);
        Vis = V_SmithGGXCorrelated_Anisotropic(at, ab, dot(T, s.V), dot(B, s.V), dot(T, L), dot(B, L), s.NoV, NoL);
    } else {
        D = D_GGX(NoH, p.roughness);
        Vis = V_SmithGGXCorrelated(s.NoV, NoL, p.roughness);
    }
    vec3 F = F_Schlick(p.f0, saturate(dot(p.f0, vec3(50.0 * 0.33))), LoH);
    return (D * Vis) * F * p.energyCompensation;
}

// Outgoing radiance for one light of incident radiance `radiance` from
// direction L, including the cosine term. `visibility` is shadowing.
vec3 surfaceShading(ShadingContext s, PixelParams p, vec3 L, vec3 radiance, float visibility) {
    float NoL = saturate(dot(s.N, L));
    vec3 H = normalize(s.V + L);
    float NoH = saturate(dot(s.N, H));
    float LoH = saturate(dot(L, H));

    vec3 Fr = specularLobe(s, p, L, H, NoL, NoH, LoH);
    // Light reflected by the specular layer is not available to diffuse:
    // scale Lambert by 1 - directional specular albedo.
    vec3 specularAlbedo = p.f0 * p.dfg.x + p.dfg.y;
    vec3 Fd = p.diffuseColor * INV_PI * (1.0 - specularAlbedo);
    vec3 color = (Fd + Fr) * NoL;

    if (p.sheenScaling < 1.0) {
        float Dsh = D_Charlie(p.sheenRoughness, NoH);
        float Vsh = V_Neubelt(s.NoV, NoL);
        color = color * p.sheenScaling + p.sheenColor * (Dsh * Vsh) * NoL;
    }

    if (p.clearcoat > 0.0) {
        float ccNoL = saturate(dot(s.Ngeo, L));
        float ccNoH = saturate(dot(s.Ngeo, H));
        float Fc = F_Schlick(0.04, 1.0, LoH) * p.clearcoat;
        float ccLobe = D_GGX(ccNoH, p.clearcoatRoughness) * V_Kelemen(LoH) * Fc;
        color = color * (1.0 - Fc) + vec3(ccLobe * ccNoL);
    }
    return color * radiance * visibility;
}

#endif
