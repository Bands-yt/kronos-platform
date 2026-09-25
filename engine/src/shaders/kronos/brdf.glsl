#ifndef KRONOS_BRDF_GLSL
#define KRONOS_BRDF_GLSL

#include "common.glsl"

// Microfacet building blocks. Conventions: `a` is GGX alpha
// (= perceptualRoughness^2); visibility terms V() already include the
// 1 / (4 NoV NoL) Cook-Torrance denominator, so specular = D * V * F.

const float kMinPerceptualRoughness = 0.045;
const float kMinRoughness = kMinPerceptualRoughness * kMinPerceptualRoughness;

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

// Heitz 2014 height-correlated Smith-GGX. Unlike the separable
// Schlick-GGX approximation it accounts for masking and shadowing being
// correlated, which removes the over-darkening at grazing angles.
float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return 0.5 / max(lambdaV + lambdaL, 1e-7);
}

float D_GGX_Anisotropic(float NoH, float ToH, float BoH, float at, float ab) {
    float a2 = at * ab;
    vec3 d = vec3(ab * ToH, at * BoH, a2 * NoH);
    float d2 = dot(d, d);
    float b2 = a2 / d2;
    return a2 * b2 * b2 * INV_PI;
}

float V_SmithGGXCorrelated_Anisotropic(float at, float ab, float ToV, float BoV, float ToL, float BoL, float NoV,
                                       float NoL) {
    float lambdaV = NoL * length(vec3(at * ToV, ab * BoV, NoV));
    float lambdaL = NoV * length(vec3(at * ToL, ab * BoL, NoL));
    return 0.5 / max(lambdaV + lambdaL, 1e-7);
}

// Kelemen 2001: cheap visibility for the clearcoat layer, which is always
// smooth-ish, where the full Smith term is not worth its cost.
float V_Kelemen(float LoH) {
    return 0.25 / max(LoH * LoH, 1e-4);
}

// Estevez & Kulla 2017 "Charlie" sheen distribution (inverted Gaussian
// family), with Neubelt & Pettineo 2013's matching visibility.
float D_Charlie(float a, float NoH) {
    float invAlpha = 1.0 / a;
    float sin2h = max(1.0 - NoH * NoH, 0.0078125);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float V_Neubelt(float NoV, float NoL) {
    return 1.0 / (4.0 * max(NoL + NoV - NoL * NoV, 1e-4));
}

vec3 F_Schlick(vec3 f0, float f90, float VoH) {
    return f0 + (vec3(f90) - f0) * pow5(1.0 - VoH);
}

float F_Schlick(float f0, float f90, float VoH) {
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

vec3 importanceSampleGGX(vec2 u, float a) {
    float phi = 2.0 * PI * u.x;
    float cosTheta = sqrt((1.0 - u.y) / (1.0 + (a * a - 1.0) * u.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

#endif
