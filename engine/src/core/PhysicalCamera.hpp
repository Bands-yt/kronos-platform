#pragma once

namespace engine::core {

// Kronos Cinematic Suite: physical camera parameters.
//
// The renderer already exposes depth of field as focus distance / focus
// range / circle-of-confusion radius (see Renderer::setDepthOfFieldParams).
// Those are the right knobs for a *renderer* and the wrong ones for a
// *cinematographer*, who thinks in millimetres and f-stops. This converts
// between the two using the real thin-lens relations rather than an
// invented curve, so a 35mm at f/1.4 defocuses the way a real 35mm at
// f/1.4 does.
//
// Sensor size matters and is not a detail: the same focal length is a
// wide lens on full-frame and a portrait lens on Super 35, so it is an
// explicit parameter rather than a hidden constant.

struct SensorSize {
    float widthMm = 36.0f;   // full-frame default
    float heightMm = 24.0f;
};

namespace sensor_presets {
inline constexpr SensorSize kFullFrame35{36.0f, 24.0f};
inline constexpr SensorSize kSuper35{24.89f, 18.66f};
inline constexpr SensorSize kMicroFourThirds{17.3f, 13.0f};
inline constexpr SensorSize kApsC{23.6f, 15.6f};
} // namespace sensor_presets

struct PhysicalCamera {
    float focalLengthMm = 35.0f;
    // f-number (focal length / aperture diameter). Smaller = wider open =
    // shallower depth of field.
    float aperture = 2.8f;
    // Distance to whatever should be sharp, in world units (metres).
    float focusDistanceMeters = 5.0f;
    SensorSize sensor = sensor_presets::kFullFrame35;

    // Exposure controls. Kept here alongside the optics because a
    // cinematographer changes them together -- opening the aperture for a
    // shallower depth of field also brightens the image, and pretending
    // otherwise is what makes virtual cameras feel unphysical.
    float isoSensitivity = 100.0f;
    float shutterSpeedSeconds = 1.0f / 48.0f; // 180-degree shutter at 24fps
};

// Vertical field of view implied by the focal length and sensor height.
// This is the real relation, so switching to a 14mm lens really produces
// a 14mm field of view instead of an artistic approximation.
[[nodiscard]] float verticalFovDegrees(const PhysicalCamera& camera);

// The inverse, for importing an existing FOV-authored shot into physical
// terms without changing how it looks.
[[nodiscard]] float focalLengthFromVerticalFov(float fovDegrees, const SensorSize& sensor);

// Hyperfocal distance: focus here and everything from roughly half this
// distance to infinity is acceptably sharp.
[[nodiscard]] float hyperfocalDistanceMeters(const PhysicalCamera& camera);

// The near and far bounds of acceptable sharpness. Returns false when the
// far bound is at infinity (focus at or beyond hyperfocal), in which case
// `outFar` is left untouched -- reporting a real infinity as some large
// finite number would quietly lie to whatever consumes it.
[[nodiscard]] bool depthOfFieldRangeMeters(const PhysicalCamera& camera, float& outNear, float& outFar);

// What the existing renderer DoF pass needs, derived from the optics
// above rather than dialled in by hand.
struct RendererDofParams {
    float focusDistance = 5.0f;
    float focusRange = 10.0f;
    float maxCoCRadiusPx = 6.0f;
};

// `imageHeightPx` is needed because a circle of confusion is a physical
// size on the sensor, and the renderer wants it in pixels -- the same
// lens produces a larger pixel blur at a higher render resolution.
[[nodiscard]] RendererDofParams toRendererDofParams(const PhysicalCamera& camera, float imageHeightPx);

// Relative exposure implied by aperture, shutter and ISO, normalised so
// the struct's defaults give 1.0. Used to keep image brightness stable
// when a shot changes aperture for depth-of-field reasons.
[[nodiscard]] float relativeExposure(const PhysicalCamera& camera);

} // namespace engine::core
