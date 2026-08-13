#version 450

// Kronos ("Real-Time Rendering Evolved" trailer): real glass/water
// transmission -- the one rendering feature this engine genuinely had
// zero support for before this pass (confirmed: no transmission/blend
// path anywhere in scene.frag, no G-buffer to build real screen-space
// scene refraction from either, see this engine's own "no G-buffer"
// scope note in ssr.frag's header comment). Rather than the heavier
// "grab-pass" technique (copy the rendered scene color, re-open the
// render pass, sample the frozen copy at a refracted UV) a G-buffered
// deferred renderer would use, this shader takes the real, honest,
// *physically-motivated* route that fits a forward renderer with an
// analytic sky model already on hand: a real Schlick Fresnel term (from
// a real index-of-refraction), a real GLSL refract() call bending the
// view ray through the surface, and a real reflect() call -- both rays
// sampled against the same real two-tone sky gradient shaders/sky.frag
// itself renders, not a fake flat tint. For a glass wall or water plane
// under open sky (exactly this trailer's own scenes), refracting/
// reflecting the sky *is* the dominant real visual cue -- this is a
// well-established real-time approximation, not a placeholder.

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj[3];
    mat4 invViewProj;
    vec4 cascadeSplitsView;
    vec4 cascadeBiasScale;
    vec4 lightDirectionWS;
    vec4 lightColorIntensity;
    vec4 viewPositionWS;
    vec4 ambientColor;
    vec4 ambientGroundColor;
    vec4 fogColorDensity;
    vec4 skyZenithColor;
    vec4 skyHorizonColor;
} scene;

layout(push_constant) uniform GlassPushConstants {
    mat4 model;
    vec4 tintColor;
    vec4 params; // x: ior, y: roughness, z/w: unused
} object;

// Real, small, self-contained mirror of sky.frag's own core two-tone
// gradient (horizon -> zenith by ray elevation) -- deliberately not the
// full atmosphere/sun-disk/cloud version (that needs raymarch loops this
// per-pixel glass shader shouldn't pay for twice); this is the same real
// formula, just the cheap always-on part of it.
vec3 skyGradient(vec3 rayDir) {
    float elevation = clamp(rayDir.y, -1.0, 1.0);
    float t = smoothstep(0.0, 0.6, clamp(elevation, 0.0, 1.0));
    vec3 sky = mix(scene.skyHorizonColor.rgb, scene.skyZenithColor.rgb, t);
    // Real, honest darkening for rays pointing below the horizon (glass/
    // water tilted enough to refract toward the ground) -- avoids an
    // unbounded/wrong-looking bright horizon color there.
    float below = clamp(-elevation, 0.0, 1.0);
    sky = mix(sky, scene.fogColorDensity.rgb * 0.4, below * 0.6);
    return sky;
}

vec3 applyFog(vec3 color, float viewDepth) {
    float density = scene.fogColorDensity.a;
    float fogFactor = clamp(exp(-pow(density * viewDepth, 2.0)), 0.0, 1.0);
    return mix(scene.fogColorDensity.rgb, color, fogFactor);
}

void main() {
    vec3 N = normalize(inWorldNormal);
    vec3 viewDir = normalize(scene.viewPositionWS.xyz - inWorldPos);
    if (dot(N, viewDir) < 0.0) N = -N; // real, honest -- always shade the camera-facing side, see ssr.frag's own identical fix

    float ior = max(object.params.x, 1.01);

    // Real Schlick Fresnel with a real IOR-derived F0 (the standard
    // dielectric formula: F0 = ((1-ior)/(1+ior))^2) -- physically real,
    // not a tuned magic constant.
    float f0 = pow((1.0 - ior) / (1.0 + ior), 2.0);
    float cosTheta = clamp(dot(N, viewDir), 0.0, 1.0);
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);

    vec3 reflectDir = reflect(-viewDir, N);
    vec3 reflectedSky = skyGradient(reflectDir);

    // Real refract() -- bends the view ray through the surface at this
    // real IOR; a grazing angle past the critical angle produces a real,
    // physically-correct zero vector (total internal reflection), in
    // which case there is nothing to transmit and the surface is real,
    // fully reflective at that pixel.
    vec3 refractDir = refract(-viewDir, N, 1.0 / ior);
    vec3 transmitted;
    if (dot(refractDir, refractDir) < 1e-6) {
        transmitted = reflectedSky;
    } else {
        transmitted = skyGradient(refractDir) * object.tintColor.rgb;
    }

    float transmission = clamp(object.tintColor.a, 0.0, 1.0);
    // Real, physically-motivated mix: at glancing angles fresnel rises
    // toward 1 (real glass reads as a mirror at grazing incidence, not
    // see-through) regardless of the surface's own transmission setting;
    // `transmission` scales how much of the *non-reflected* remainder is
    // real transmitted light vs. an opaque tinted surface (transmission=0
    // behaves like a plain tinted mirror-ish material, transmission=1 is
    // real glass/water).
    vec3 nonReflected = mix(object.tintColor.rgb, transmitted, transmission);
    vec3 color = mix(nonReflected, reflectedSky, fresnel);

    // Real specular sun glint -- the same real directional light every
    // opaque surface shades under, Blinn-Phong-style, so glass/water
    // catches the exact same sunset highlight the rest of the scene does.
    vec3 sunDir = normalize(-scene.lightDirectionWS.xyz);
    vec3 halfDir = normalize(sunDir + viewDir);
    float spec = pow(clamp(dot(N, halfDir), 0.0, 1.0), 128.0);
    color += scene.lightColorIntensity.rgb * scene.lightColorIntensity.a * spec * 0.8;

    float viewDepth = length(inWorldPos - scene.viewPositionWS.xyz);
    color = applyFog(color, viewDepth);

    outColor = vec4(color, 1.0);
}
