#ifndef KRONOS_IBL_GLSL
#define KRONOS_IBL_GLSL

// Image-based lighting: split-sum specular from a GGX-prefiltered capture
// of the live sky, SH9 irradiance, and Fdez-Aguera 2019 multiple-scattering
// compensation. Requires scene_resources.glsl + material.glsl.
//
// Diffuse ambient stays the artist-authored sky/ground hemisphere (every
// existing lighting preset is tuned around it). The captured sky's SH is
// used to *normalize* specular reflections to that ambient level (the
// "reflection normalization" of Lazarov 2013 / Call of Duty): an indoor
// preset with dim ambient no longer reflects a bright outdoor sky, while
// the reflections keep the sky's directional structure.

vec3 hemisphereAmbient(vec3 N) {
    return mix(scene.ambientGroundColor.rgb, scene.ambientColor.rgb, N.y * 0.5 + 0.5);
}

vec3 evaluateSH9(vec3 n) {
    vec3 r = envSH.shIrradiance[0].rgb;
    r += envSH.shIrradiance[1].rgb * n.y;
    r += envSH.shIrradiance[2].rgb * n.z;
    r += envSH.shIrradiance[3].rgb * n.x;
    r += envSH.shIrradiance[4].rgb * (n.x * n.y);
    r += envSH.shIrradiance[5].rgb * (n.y * n.z);
    r += envSH.shIrradiance[6].rgb * (3.0 * n.z * n.z - 1.0);
    r += envSH.shIrradiance[7].rgb * (n.x * n.z);
    r += envSH.shIrradiance[8].rgb * (n.x * n.x - n.y * n.y);
    return max(r, vec3(0.0));
}

bool iblAvailable() {
    return scene.iblParams.w > 0.5;
}

float reflectionNormalization(vec3 N) {
    if (!iblAvailable()) return 1.0;
    float target = luminance(hemisphereAmbient(N));
    float captured = luminance(evaluateSH9(N));
    float ratio = clamp(target / max(captured, 1e-4), 0.0, 2.0);
    return mix(1.0, ratio, scene.iblParams.y);
}

vec3 prefilteredRadiance(vec3 R, float perceptualRoughness) {
    float lod = perceptualRoughness * scene.iblParams.z;
    return textureLod(envPrefiltered, R, lod).rgb;
}

// Lagarde & de Rousiers 2014: rough lobes are centred between R and N.
vec3 specularDominantDirection(vec3 N, vec3 R, float roughness) {
    float s = 1.0 - roughness;
    return normalize(mix(N, R, s * (sqrt(s) + roughness)));
}

float specularOcclusion(float NoV, float ao, float roughness) {
    return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// Normal maps can bend R below the geometric horizon; fade those
// reflections out instead of sampling the ground-side of the capture.
float horizonOcclusion(vec3 R, vec3 Ngeo) {
    float h = saturate(1.0 + dot(R, Ngeo));
    return h * h;
}

vec3 anisotropicBentNormal(ShadingContext s, PixelParams p) {
    vec3 dir = p.anisotropy >= 0.0 ? p.anisotropicB : p.anisotropicT;
    vec3 anisoTangent = cross(dir, s.V);
    vec3 anisoNormal = cross(anisoTangent, dir);
    float bend = abs(p.anisotropy) * saturate(5.0 * p.perceptualRoughness);
    return normalize(mix(s.N, anisoNormal, bend));
}

// Specular radiance arriving along the (dominant) reflection direction.
// Split out so ray-traced reflections can replace it for glossy surfaces.
vec3 iblSpecularRadiance(ShadingContext s, PixelParams p, out vec3 R) {
    vec3 Nr = p.anisotropy != 0.0 ? anisotropicBentNormal(s, p) : s.N;
    R = reflect(-s.V, Nr);
    vec3 Rd = specularDominantDirection(Nr, R, p.roughness);
    if (!iblAvailable()) return hemisphereAmbient(Rd);
    return prefilteredRadiance(Rd, p.perceptualRoughness) * reflectionNormalization(s.N) * scene.iblParams.x;
}

// `irradiance` is radiance-equivalent (E / PI); normally hemisphereAmbient(N).
vec3 evaluateIBL(ShadingContext s, PixelParams p, float diffuseAO, vec3 irradiance, vec3 radiance, vec3 R) {
    vec3 FssEss = p.f0 * p.dfg.x + p.dfg.y;
    float Ess = p.dfg.x + p.dfg.y;
    float Ems = 1.0 - Ess;
    vec3 Favg = p.f0 + (1.0 - p.f0) / 21.0;
    vec3 Fms = FssEss * Favg / (1.0 - Ems * Favg);

    float specAO = specularOcclusion(s.NoV, diffuseAO, p.roughness) * horizonOcclusion(R, s.Ngeo);
    vec3 Fr = (FssEss * radiance + Fms * Ems * irradiance) * specAO;
    vec3 Fd = p.diffuseColor * (1.0 - FssEss - Fms * Ems) * irradiance * diffuseAO;
    vec3 color = Fd + Fr;

    float norm = iblAvailable() ? reflectionNormalization(s.N) * scene.iblParams.x : 1.0;
    if (p.sheenScaling < 1.0) {
        vec3 sheenRadiance = prefilteredRadiance(R, p.sheenPerceptualRoughness) * norm;
        color = color * p.sheenScaling + p.sheenColor * p.sheenDFG * sheenRadiance * specAO;
    }

    if (p.clearcoat > 0.0) {
        float ccNoV = saturate(dot(s.Ngeo, s.V));
        vec3 Rc = reflect(-s.V, s.Ngeo);
        float Fc = F_Schlick(0.04, 1.0, ccNoV) * p.clearcoat;
        float ccAO = specularOcclusion(ccNoV, diffuseAO, p.clearcoatRoughness);
        color = color * (1.0 - Fc) + prefilteredRadiance(Rc, p.clearcoatPerceptualRoughness) * norm * Fc * ccAO;
    }
    return color;
}

#endif
