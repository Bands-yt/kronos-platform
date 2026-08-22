#include "core/PhysicalCamera.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kEpsilon = 1e-6f;

// Millimetres per world unit. Kronos world units are metres, and the
// thin-lens maths below is in millimetres, so this conversion has to be
// explicit -- getting it wrong by 1000x is the classic way physical
// camera code produces either no blur at all or nothing but blur.
constexpr float kMmPerMeter = 1000.0f;

// Largest blur spot a viewer accepts as "sharp", on the sensor. 0.03mm is
// the long-standing full-frame convention; scaling it by sensor size
// keeps it meaningful on smaller sensors.
float circleOfConfusionLimitMm(const SensorSize& sensor) {
    const float fullFrameDiagonal = 43.27f; // sqrt(36^2 + 24^2)
    const float diagonal = std::sqrt(sensor.widthMm * sensor.widthMm + sensor.heightMm * sensor.heightMm);
    return 0.03f * (diagonal / fullFrameDiagonal);
}
} // namespace

float verticalFovDegrees(const PhysicalCamera& camera) {
    if (camera.focalLengthMm < kEpsilon) return 90.0f;
    const float fovRadians = 2.0f * std::atan((camera.sensor.heightMm * 0.5f) / camera.focalLengthMm);
    return fovRadians * 180.0f / kPi;
}

float focalLengthFromVerticalFov(float fovDegrees, const SensorSize& sensor) {
    const float clamped = std::clamp(fovDegrees, 1.0f, 179.0f);
    const float halfFovRadians = (clamped * kPi / 180.0f) * 0.5f;
    const float tangent = std::tan(halfFovRadians);
    if (tangent < kEpsilon) return 1000.0f;
    return (sensor.heightMm * 0.5f) / tangent;
}

float hyperfocalDistanceMeters(const PhysicalCamera& camera) {
    const float coc = circleOfConfusionLimitMm(camera.sensor);
    if (camera.aperture < kEpsilon || coc < kEpsilon) return 1e9f;
    // H = f^2 / (N * c) + f, in millimetres.
    const float hyperfocalMm =
        (camera.focalLengthMm * camera.focalLengthMm) / (camera.aperture * coc) + camera.focalLengthMm;
    return hyperfocalMm / kMmPerMeter;
}

bool depthOfFieldRangeMeters(const PhysicalCamera& camera, float& outNear, float& outFar) {
    const float hyperfocalMm = hyperfocalDistanceMeters(camera) * kMmPerMeter;
    const float focusMm = std::max(camera.focusDistanceMeters, 0.001f) * kMmPerMeter;
    const float focal = camera.focalLengthMm;

    const float nearDenominator = hyperfocalMm + focusMm - 2.0f * focal;
    outNear = nearDenominator > kEpsilon ? ((hyperfocalMm - focal) * focusMm) / nearDenominator / kMmPerMeter
                                          : camera.focusDistanceMeters;

    // At or beyond hyperfocal the far bound is genuinely infinite. Say so
    // rather than returning a huge number the caller would treat as real.
    const float farDenominator = hyperfocalMm - focusMm;
    if (farDenominator <= kEpsilon) return false;
    outFar = ((hyperfocalMm - focal) * focusMm) / farDenominator / kMmPerMeter;
    return true;
}

RendererDofParams toRendererDofParams(const PhysicalCamera& camera, float imageHeightPx) {
    RendererDofParams params;
    params.focusDistance = camera.focusDistanceMeters;

    float nearMeters = camera.focusDistanceMeters;
    float farMeters = camera.focusDistanceMeters;
    if (depthOfFieldRangeMeters(camera, nearMeters, farMeters)) {
        // Half the in-focus span reads as "range" to the existing pass.
        params.focusRange = std::max((farMeters - nearMeters) * 0.5f, 0.01f);
    } else {
        // Infinite far bound: everything past the near limit is sharp, so
        // give the pass a range large enough to mean "do not blur the background".
        params.focusRange = std::max(camera.focusDistanceMeters * 4.0f, 50.0f);
    }

    // Circle of confusion for an object at infinity, converted from a
    // physical size on the sensor into pixels at this render height.
    if (camera.aperture > kEpsilon && camera.focusDistanceMeters > kEpsilon) {
        const float apertureDiameterMm = camera.focalLengthMm / camera.aperture;
        const float focusMm = camera.focusDistanceMeters * kMmPerMeter;
        const float cocMm =
            apertureDiameterMm * (camera.focalLengthMm / std::max(focusMm - camera.focalLengthMm, kEpsilon));
        const float pixelsPerMm = imageHeightPx / std::max(camera.sensor.heightMm, kEpsilon);
        params.maxCoCRadiusPx = std::clamp(cocMm * pixelsPerMm * 0.5f, 0.0f, 32.0f);
    }
    return params;
}

float relativeExposure(const PhysicalCamera& camera) {
    // Exposure scales with shutter time and ISO, and inversely with the
    // square of the f-number. Normalised against the struct defaults
    // (f/2.8, 1/48s, ISO 100) so those give exactly 1.0.
    constexpr float kReferenceAperture = 2.8f;
    constexpr float kReferenceShutter = 1.0f / 48.0f;
    constexpr float kReferenceIso = 100.0f;
    if (camera.aperture < kEpsilon) return 1.0f;

    const float apertureTerm = (kReferenceAperture * kReferenceAperture) / (camera.aperture * camera.aperture);
    const float shutterTerm = camera.shutterSpeedSeconds / kReferenceShutter;
    const float isoTerm = camera.isoSensitivity / kReferenceIso;
    return apertureTerm * shutterTerm * isoTerm;
}

} // namespace engine::core
