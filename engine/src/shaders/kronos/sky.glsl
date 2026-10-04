#ifndef KRONOS_SKY_GLSL
#define KRONOS_SKY_GLSL

// Procedural sky shared by sky.frag (background), the IBL environment
// capture (ibl_capture.comp) and cloud shadows, so reflections and
// lighting always see exactly the sky that is drawn.

#include "common.glsl"

vec2 raySphereIntersect(vec3 ro, vec3 rd, float radius) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return vec2(1.0, -1.0);
    float s = sqrt(disc);
    return vec2(-b - s, -b + s);
}

vec3 computeAtmosphere(vec3 rayOrigin, vec3 rayDir, vec3 sunDir, float sunIntensity, float mieStrength) {
    const float kRg = 6360000.0;
    const float kRt = 6420000.0;
    const float kHr = 7994.0;
    const float kHm = 1200.0;
    const vec3 kBetaR = vec3(5.5e-6, 13.0e-6, 22.4e-6);
    const float kBetaMBase = 21e-6;
    const float kMieExtinctionFactor = 1.1;
    const float kMieG = 0.758;

    vec3 ro = rayOrigin - vec3(0.0, -kRg, 0.0);
    vec2 atmHit = raySphereIntersect(ro, rayDir, kRt);
    if (atmHit.y < 0.0) return vec3(0.0);

    float tMin = max(atmHit.x, 0.0);
    float tMax = atmHit.y;
    vec2 groundHit = raySphereIntersect(ro, rayDir, kRg);
    if (groundHit.x > 0.0) tMax = min(tMax, groundHit.x);

    const int kPrimarySteps = 16;
    const int kSecondarySteps = 8;
    float segmentLength = (tMax - tMin) / float(kPrimarySteps);
    if (segmentLength <= 0.0) return vec3(0.0);

    float mu = dot(rayDir, sunDir);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g2 = kMieG * kMieG;
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + mu * mu)) /
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
    return (sumR * kBetaR * phaseR + sumM * (kBetaMBase * mieStrength) * phaseM) * sunIntensity;
}

float cloudHash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float cloudValueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(cloudHash(i + vec3(0, 0, 0)), cloudHash(i + vec3(1, 0, 0)), u.x),
                   mix(cloudHash(i + vec3(0, 1, 0)), cloudHash(i + vec3(1, 1, 0)), u.x), u.y),
               mix(mix(cloudHash(i + vec3(0, 0, 1)), cloudHash(i + vec3(1, 0, 1)), u.x),
                   mix(cloudHash(i + vec3(0, 1, 1)), cloudHash(i + vec3(1, 1, 1)), u.x), u.y),
               u.z);
}

float cloudFbm(vec3 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += amp * cloudValueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return sum;
}

const float kCloudBase = 500.0;
const float kCloudTop = 650.0;

vec4 computeClouds(vec3 rayOrigin, vec3 rayDir, vec3 sunDir, float coverage, float speed, float time) {
    if (rayDir.y <= 0.01) return vec4(0.0);
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
        float density = smoothstep(0.85 - coverage, 1.15 - coverage, cloudFbm((samplePos + wind) * 0.0035));
        if (density > 0.01) {
            float shadowDensity = 0.0;
            vec3 shadowPos = samplePos;
            for (int j = 0; j < 4; ++j) {
                shadowPos += sunDir * 40.0;
                shadowDensity += smoothstep(1.0 - coverage, 1.0, cloudFbm((shadowPos + wind) * 0.0035));
            }
            float sunVisibility = exp(-shadowDensity * 1.2);
            float heightFrac = saturate((samplePos.y - kCloudBase) / (kCloudTop - kCloudBase));
            vec3 cloudColor = mix(vec3(0.55, 0.58, 0.65), vec3(1.05, 1.02, 0.98), sunVisibility * (0.4 + 0.6 * heightFrac));
            float sampleTransmittance = exp(-density * stepSize * 0.02);
            accumColor += transmittance * (1.0 - sampleTransmittance) * cloudColor;
            transmittance *= sampleTransmittance;
            if (transmittance < 0.01) break;
        }
        t += stepSize;
    }
    return vec4(accumColor, 1.0 - transmittance);
}

// Fraction of sunlight reaching `worldPos` through the cloud layer.
float cloudShadowAt(vec3 worldPos, vec3 sunDir, float coverage, float speed, float time) {
    const float kCloudMidAltitude = 0.5 * (kCloudBase + kCloudTop);
    if (sunDir.y <= 0.01) return 1.0;
    float t = (kCloudMidAltitude - worldPos.y) / sunDir.y;
    if (t <= 0.0) return 1.0;
    vec3 samplePos = worldPos + sunDir * t;
    vec3 wind = vec3(time * speed, 0.0, time * speed * 0.6);
    float density = smoothstep(0.85 - coverage, 1.15 - coverage, cloudFbm((samplePos + wind) * 0.0035));
    return mix(1.0, 0.35, density);
}

struct SkyInputs {
    vec3 zenith;
    vec3 horizon;
    vec3 sunDir;
    vec3 origin;
    vec4 atmosphere; // SceneUBO::atmosphereParams
    vec4 clouds;     // SceneUBO::cloudParams
};

// Gradient, horizon haze and optional single scattering; no sun, no clouds.
vec3 skyBackground(SkyInputs s, vec3 dir) {
    float elevation = clamp(dir.y, 0.0, 1.0);
    vec3 sky = mix(s.horizon, s.zenith, smoothstep(0.0, 0.6, elevation));
    float haze = 1.0 - smoothstep(0.0, 0.12, elevation);
    sky = mix(sky, s.horizon * 1.25, haze * 0.35);
    if (s.atmosphere.x > 0.5) {
        // Below the horizon the ray hits the ground almost at once and comes out
        // dark; hold the horizon value so gaps under distant terrain don't band.
        vec3 scatterDir = normalize(vec3(dir.x, max(dir.y, 0.0), dir.z));
        sky += computeAtmosphere(s.origin, scatterDir, s.sunDir, s.atmosphere.y, s.atmosphere.z);
    }
    return sky;
}

vec3 compositeClouds(SkyInputs s, vec3 dir, vec3 background) {
    if (s.clouds.x < 0.5) return background;
    vec4 c = computeClouds(s.origin, dir, s.sunDir, s.clouds.y, s.clouds.z, s.clouds.w);
    // Distant clouds take on the horizon haze and thin out towards it, so the
    // layer has no hard edge where it stops just above the horizon.
    float lowness = 1.0 - smoothstep(0.0, 0.35, dir.y);
    vec3 color = mix(c.rgb, s.horizon * 1.15, lowness * 0.65);
    return mix(background, color, saturate(c.a) * smoothstep(0.01, 0.12, dir.y));
}

// Sky radiance along `dir`, excluding the sun disk (sky.frag adds that
// itself; the IBL keeps the sun out because it is already an analytic light).
vec3 skyRadiance(SkyInputs s, vec3 dir) {
    return compositeClouds(s, dir, skyBackground(s, dir));
}

#endif
