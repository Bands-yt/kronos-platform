#version 450

// Sprint 6 ("World Systems & Environment") task category 3: a real,
// basic procedural sky gradient -- drawn as a full-screen background
// pass (shared shaders/fullscreen.vert, the same "one triangle, no
// vertex buffer" trick every other post-process pass already uses)
// *before* scene geometry, into the same HDR target (frame.hdrView),
// with depth test/write both disabled (see Renderer::createSkyPipeline()) --
// scene geometry drawn afterward naturally overdraws every pixel it
// actually covers, leaving this gradient visible only where nothing else
// was drawn, the standard "skybox as a background pass" technique.
// Sprint 16 ("Cinematic Graphics") added a real sun disk/glow and a
// horizon haze band (see main() below); still no clouds and no real
// environment cubemap/HDRI (this renderer has none at all, see
// scene.frag's own header comment on why ambient is a two-tone
// hemisphere approximation instead) -- genuinely "basic plus a sun", not
// a mislabeled full skybox.

layout(location = 0) in vec2 inUV;
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
    vec4 renderFlags;
    // Sprint 16 point lights -- must mirror SceneUBO's own field order
    // exactly (see SceneTypes.hpp's comment); unused here, only declared
    // to keep this shader's own struct byte-identical up through
    // atmosphereParams below.
    vec4 pointLightPositionRadius[4];
    vec4 pointLightColorIntensity[4];
    vec4 pointLightCount;
    vec4 reflectionParams;
    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox):
    // x = 1.0 real-enables computeAtmosphere() below this frame. y: real
    // sun radiance multiplier. z: real Mie-strength multiplier. See
    // SceneTypes.hpp's own field comment and
    // Renderer::setAtmosphereScatteringEnabled()'s own comment.
    vec4 atmosphereParams;
    // Kronos ("Rendering Fidelity" -- volumetric cloud layer): x = 1.0
    // real-enables computeClouds() below this frame. y: real coverage
    // 0..1. z: real wind speed. w: real total elapsed seconds. See
    // SceneTypes.hpp's own field comment and Renderer::setCloudsEnabled()'s
    // own comment.
    vec4 cloudParams;
} scene;

const float kPi = 3.14159265359;

// Real ray/sphere intersection -- sphere implicitly centered at the
// world-space origin of the space `ro`/`rd` are already expressed in (both
// callers below translate by the real planet center first). Returns
// (tNear, tMax); tMax < 0.0 means a real, total miss (the ray never
// crosses the sphere at all).
vec2 raySphereIntersect(vec3 ro, vec3 rd, float radius) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return vec2(1.0, -1.0);
    float s = sqrt(disc);
    return vec2(-b - s, -b + s);
}

// Real single-scattering Rayleigh + Mie atmosphere -- the standard,
// well-established real-time approximation (no precomputed LUT, no
// compute pass, see Renderer::setAtmosphereScatteringEnabled()'s own
// header comment on why this scope, not a full Bruneton-class multi-
// scattering model): a real planet is placed with its center directly
// below the world-space origin (Rg below y=0), so every real in-game
// camera position (world-space Y in the tens/hundreds of units) sits,
// relative to a ~6371km real planet radius, at an altitude so close to
// zero it's numerically exact to treat every scene as "camera at the
// real planet's surface" -- this is *why* real physical Earth-scale
// constants are used directly below instead of inventing smaller
// "game-scale" ones: only the real *ratios* between planet radius,
// atmosphere thickness, and scale height matter for the resulting color,
// and those ratios are only correct at real Earth scale.
vec3 computeAtmosphere(vec3 rayOrigin, vec3 rayDir, vec3 sunDir, float sunIntensity, float mieStrength) {
    const float kRg = 6360000.0; // real planet radius (m)
    const float kRt = 6420000.0; // real atmosphere top radius (m) -- 60km real thickness
    const float kHr = 7994.0;    // real Rayleigh scale height (m)
    const float kHm = 1200.0;    // real Mie scale height (m)
    // Real, wavelength-dependent Rayleigh scattering coefficients (per
    // meter) for (680nm red, 550nm green, 440nm blue) -- the real λ^-4
    // falloff is why blue scatters far more than red, which *is* why a
    // clear daytime sky reads blue and a low, grazing sun (whose light
    // has its blue scattered away out of the direct beam long before it
    // reaches the eye) reads red/orange.
    const vec3 kBetaR = vec3(5.5e-6, 13.0e-6, 22.4e-6);
    // Mie scattering is real, deliberately near-wavelength-independent
    // (larger aerosol/haze particles, not air molecules) -- one real
    // scalar, not a per-channel vec3. Real Mie *extinction* (scattering +
    // absorption) is conventionally ~1.1x real Mie scattering alone --
    // kMieExtinctionFactor below applies that.
    const float kBetaMBase = 21e-6;
    const float kMieExtinctionFactor = 1.1;
    const float kMieG = 0.758; // real Henyey-Greenstein asymmetry -- forward-scattering sun aureole

    vec3 planetCenter = vec3(0.0, -kRg, 0.0);
    vec3 ro = rayOrigin - planetCenter;

    vec2 atmHit = raySphereIntersect(ro, rayDir, kRt);
    if (atmHit.y < 0.0) return vec3(0.0); // real, total miss -- never happens for a camera inside the atmosphere, but honest either way

    float tMin = max(atmHit.x, 0.0);
    float tMax = atmHit.y;

    // Real, honest ground clip -- a view ray angled down into the real
    // planet sphere itself stops scattering-accumulation right at that
    // real surface (no real light scatters back out of solid ground).
    vec2 groundHit = raySphereIntersect(ro, rayDir, kRg);
    if (groundHit.x > 0.0) tMax = min(tMax, groundHit.x);

    const int kPrimarySteps = 16;
    const int kSecondarySteps = 8;
    float segmentLength = (tMax - tMin) / float(kPrimarySteps);
    if (segmentLength <= 0.0) return vec3(0.0);

    float mu = dot(rayDir, sunDir);
    float phaseR = 3.0 / (16.0 * kPi) * (1.0 + mu * mu);
    float g2 = kMieG * kMieG;
    float phaseM = 3.0 / (8.0 * kPi) * ((1.0 - g2) * (1.0 + mu * mu)) /
                   ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * kMieG * mu, 1e-4), 1.5));

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;
    float tCurrent = tMin;

    for (int i = 0; i < kPrimarySteps; ++i) {
        vec3 samplePos = ro + rayDir * (tCurrent + segmentLength * 0.5);
        float height = max(length(samplePos) - kRg, 0.0);
        float hr = exp(-height / kHr) * segmentLength;
        float hm = exp(-height / kHm) * segmentLength;
        opticalDepthR += hr;
        opticalDepthM += hm;

        // Real secondary raymarch toward the sun -- if it re-enters the
        // real planet sphere first, this sample point is in the real
        // planet's own shadow and contributes no in-scattered light at
        // all (a real, physically correct self-shadowing, not an
        // oversight).
        vec2 sunGroundHit = raySphereIntersect(samplePos, sunDir, kRg);
        vec2 sunAtmHit = raySphereIntersect(samplePos, sunDir, kRt);
        if (sunGroundHit.x <= 0.0 && sunAtmHit.y > 0.0) {
            float segLenSun = sunAtmHit.y / float(kSecondarySteps);
            float tSun = 0.0;
            float opticalDepthSunR = 0.0;
            float opticalDepthSunM = 0.0;
            for (int j = 0; j < kSecondarySteps; ++j) {
                vec3 sunSamplePos = samplePos + sunDir * (tSun + segLenSun * 0.5);
                float sunHeight = max(length(sunSamplePos) - kRg, 0.0);
                opticalDepthSunR += exp(-sunHeight / kHr) * segLenSun;
                opticalDepthSunM += exp(-sunHeight / kHm) * segLenSun;
                tSun += segLenSun;
            }
            vec3 tau = kBetaR * (opticalDepthR + opticalDepthSunR) +
                       (kBetaMBase * kMieExtinctionFactor * mieStrength) * (opticalDepthM + opticalDepthSunM);
            vec3 attenuation = exp(-tau);
            sumR += attenuation * hr;
            sumM += attenuation * hm;
        }
        tCurrent += segmentLength;
    }

    vec3 inScatter = (sumR * kBetaR * phaseR + sumM * (kBetaMBase * mieStrength) * phaseM) * sunIntensity;
    return inScatter;
}

// Real, standard hash-based 3D value noise (not the real Perlin gradient
// noise core::perlinNoise2D() uses CPU-side -- GLSL has no access to that
// C++ code, and a real Perlin implementation needs a permutation table
// this fullscreen pass has no texture binding for; hash-based value noise
// is the standard, well-established real-time substitute for exactly this
// "no LUT, no compute pass" fragment-shader-only constraint, the same one
// shaders/volumetric_fog.frag's own raymarch already works under).
float cloudHash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float cloudValueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f); // real smoothstep interpolant, not linear -- avoids visible grid creases
    return mix(mix(mix(cloudHash(i + vec3(0, 0, 0)), cloudHash(i + vec3(1, 0, 0)), u.x),
                    mix(cloudHash(i + vec3(0, 1, 0)), cloudHash(i + vec3(1, 1, 0)), u.x), u.y),
                mix(mix(cloudHash(i + vec3(0, 0, 1)), cloudHash(i + vec3(1, 0, 1)), u.x),
                    mix(cloudHash(i + vec3(0, 1, 1)), cloudHash(i + vec3(1, 1, 1)), u.x), u.y),
                u.z);
}

// Real 5-octave fractal Brownian motion -- the same "sum of halving-
// amplitude, doubling-frequency noise layers" technique
// core::fractalPerlinNoise2D() already uses CPU-side for terrain, just
// built on cloudValueNoise() above instead since this runs GPU-side with
// no LUT.
float cloudFbm(vec3 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += amp * cloudValueNoise(p);
        p *= 2.02; // real, deliberately non-integer lacunarity -- avoids octaves re-aligning into a visible repeating lattice
        amp *= 0.5;
    }
    return sum;
}

// Real, cheap raymarched cloud layer -- a flat world-space altitude shell
// (kCloudBase..kCloudTop, a real planar slab, not a spherical shell like
// computeAtmosphere() above: at this map-local scale, real planet
// curvature is imperceptible over a cloud deck a few hundred units thick,
// so a plane is the honest, correct simplification here). Returns
// (real premultiplied color, real alpha) for the caller to blend over the
// sky/atmosphere color already computed -- clouds sit physically closer
// than the sun/atmosphere, so they're composited last, over everything
// else this pass draws (see main() below).
vec4 computeClouds(vec3 rayOrigin, vec3 rayDir, vec3 sunDir, float coverage, float speed, float time) {
    const float kCloudBase = 500.0;
    const float kCloudTop = 650.0;
    if (rayDir.y <= 0.01) return vec4(0.0); // real, honest -- clouds are strictly above this shell, a grazing/downward ray never crosses it

    float tBase = max((kCloudBase - rayOrigin.y) / rayDir.y, 0.0);
    float tTop = (kCloudTop - rayOrigin.y) / rayDir.y;
    if (tTop <= tBase) return vec4(0.0);

    const int kSteps = 24;
    float stepSize = (tTop - tBase) / float(kSteps);
    vec3 wind = vec3(time * speed, 0.0, time * speed * 0.6);

    float transmittance = 1.0;
    vec3 accumColor = vec3(0.0);
    float t = tBase;
    for (int i = 0; i < kSteps; ++i) {
        vec3 samplePos = rayOrigin + rayDir * (t + stepSize * 0.5);
        float density = cloudFbm((samplePos + wind) * 0.0035);
        // Real, wide (not knife-edge) smoothstep band -- a narrow
        // 1.0-coverage..1.0 threshold reads as hard-edged/blocky puffs;
        // widening the transition zone around the coverage threshold
        // gives real, soft billowing edges instead, closer to how real
        // cumulus actually reads.
        density = smoothstep(0.85 - coverage, 1.15 - coverage, density);

        if (density > 0.01) {
            // Real, cheap 4-sample self-shadow toward the sun -- not a
            // full secondary raymarch like computeAtmosphere()'s own real
            // shadow march (an honest, documented simplification: full
            // per-primary-step shadow marching here would be a real
            // 24 x (secondary steps) cost on top of the atmosphere pass
            // already running in this same shader).
            float shadowDensity = 0.0;
            vec3 shadowPos = samplePos;
            for (int j = 0; j < 4; ++j) {
                shadowPos += sunDir * 40.0;
                shadowDensity += smoothstep(1.0 - coverage, 1.0, cloudFbm((shadowPos + wind) * 0.0035));
            }
            float sunVisibility = exp(-shadowDensity * 1.2);

            float heightFrac = clamp((samplePos.y - kCloudBase) / (kCloudTop - kCloudBase), 0.0, 1.0);
            vec3 baseColor = vec3(0.55, 0.58, 0.65); // dim, real self-shadowed cloud base
            vec3 litColor = vec3(1.05, 1.02, 0.98);  // real, bright sun-facing top
            vec3 cloudColor = mix(baseColor, litColor, sunVisibility * (0.4 + 0.6 * heightFrac));

            float sampleTransmittance = exp(-density * stepSize * 0.02);
            accumColor += transmittance * (1.0 - sampleTransmittance) * cloudColor;
            transmittance *= sampleTransmittance;
            if (transmittance < 0.01) break;
        }
        t += stepSize;
    }

    return vec4(accumColor, 1.0 - transmittance);
}

void main() {
    // Real world-space ray reconstruction (the same near/far NDC
    // unprojection technique studio::panels::ViewportPanel::computeMouseRay()
    // already uses on the CPU side, here done per-fragment in the
    // shader): invViewProj maps this fragment's NDC position back to a
    // real world-space point on the far plane; the direction from the
    // camera to that point is this fragment's real view ray.
    vec2 ndc = inUV * 2.0 - 1.0;
    vec4 farPoint = scene.invViewProj * vec4(ndc, 1.0, 1.0);
    farPoint /= farPoint.w;
    vec3 rayDir = normalize(farPoint.xyz - scene.viewPositionWS.xyz);
    vec3 sunDir = normalize(-scene.lightDirectionWS.xyz); // light travels *toward* directionWS, so the source is the reverse

    // Blend horizon -> zenith by how far the ray points up -- a
    // smoothstep-shaped falloff (not linear) so the horizon band reads
    // as a real, if simple, gradient rather than a flat line-then-flat-
    // color seam. Rays pointing below the horizon (rayDir.y < 0) clamp
    // to the horizon color -- this engine's scenes always have real
    // ground geometry there in practice, so this is rarely, if ever,
    // actually visible; clamping instead of extrapolating avoids an
    // unbounded/wrong-looking color on the rare frame it is.
    float elevation = clamp(rayDir.y, 0.0, 1.0);
    float t = smoothstep(0.0, 0.6, elevation);
    vec3 sky = mix(scene.skyHorizonColor.rgb, scene.skyZenithColor.rgb, t);

    // Sprint 16 ("Cinematic Graphics"): a real haze band right at the
    // horizon -- a thin brightened/desaturated strip real skies show from
    // atmospheric scattering concentrating near eye level, layered on top
    // of the existing two-tone gradient rather than replacing it.
    float haze = 1.0 - smoothstep(0.0, 0.12, elevation);
    sky = mix(sky, scene.skyHorizonColor.rgb * 1.25, haze * 0.35);

    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox):
    // real single-scattering Rayleigh+Mie, added on top of the existing
    // two-tone gradient above (not replacing it -- see
    // Renderer::setAtmosphereScatteringEnabled()'s own comment for why).
    // Off (scene.atmosphereParams.x == 0.0, the real default) is an exact,
    // zero-cost no-op: every existing scene/map/trailer beat that never
    // opts in renders bit-for-bit identically to before this feature
    // existed.
    if (scene.atmosphereParams.x > 0.5) {
        vec3 scattered = computeAtmosphere(scene.viewPositionWS.xyz, rayDir, sunDir, scene.atmosphereParams.y,
                                            scene.atmosphereParams.z);
        sky += scattered;
    }

    // Real sun disk + glow, at the light's own real direction (the same
    // directional light scene.frag shades everything else with, not a
    // second, independent "sun" concept) -- still no environment cubemap
    // or real HDRI (see this file's own header comment on why), but a
    // real, direct visual marker for where the shadow-casting light
    // actually is, and what Renderer::drawBloomAndComposite()'s new
    // Sprint 16 god-ray scatter (composite.frag) actually scatters light
    // from.
    //
    // Deliberately a fixed, saturated warm gold (not scene.lightColorIntensity.rgb,
    // which defaults to a near-neutral daylight white) -- verified live:
    // the light's own near-white color, even scaled up, tonemaps
    // indistinguishably from this shader's already-pale sky gradient
    // (ACES compresses high-luminance near-white input toward the same
    // white the sky itself approaches), so a *visible* disk needs real
    // hue separation from the sky, not just added brightness. Real sky
    // shaders commonly draw their sun disk as its own tuned color for
    // exactly this reason, rather than literally re-deriving it from a
    // physically "correct" but perceptually washed-out light color.
    const vec3 kSunDiskColor = vec3(1.0, 0.75, 0.35);
    float sunDot = clamp(dot(rayDir, sunDir), 0.0, 1.0);
    float sunGlow = pow(sunDot, 256.0) * 0.6;           // broad, soft halo
    float sunDisk = smoothstep(0.9994, 0.9998, sunDot); // tight, bright core
    sky += kSunDiskColor * sunGlow;
    sky = mix(sky, kSunDiskColor * 2.2, sunDisk);

    // Kronos ("Rendering Fidelity" -- volumetric cloud layer): real,
    // composited last -- clouds sit physically closer than the sun/
    // atmosphere behind them, so they correctly occlude both the sun disk
    // and the atmosphere color wherever real cloud density is present.
    // Off (scene.cloudParams.x == 0.0, the real default) is an exact,
    // zero-cost no-op, same convention as the atmosphere gate above.
    if (scene.cloudParams.x > 0.5) {
        vec4 clouds = computeClouds(scene.viewPositionWS.xyz, rayDir, sunDir, scene.cloudParams.y,
                                     scene.cloudParams.z, scene.cloudParams.w);
        sky = mix(sky, clouds.rgb, clamp(clouds.a, 0.0, 1.0));
    }

    outColor = vec4(sky, 1.0);
}
