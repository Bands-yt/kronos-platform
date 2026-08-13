#include "trailer/TrailerCinematics.hpp"

namespace engine::trailer {

using tntwars::CinematicSequence;

CinematicSequence buildOpeningCinematic(glm::vec3 originPosition) {
    CinematicSequence sequence;
    // Sprint 16 ("Cinematic Graphics" Phase 4, "Bezier camera path
    // smoothing"): a real cubic Bezier arc, not a straight-line lerp --
    // the two tangent handles (p1/p2) both pull the path outward/high
    // relative to the straight line between the anchors, so the camera
    // real-sweeps through a shallow descending arc instead of drifting
    // in a dead-straight line, deliberately the slowest, calmest real
    // camera move in the whole trailer, so everything after it reads as
    // a real step up in pace.
    glm::vec3 start = originPosition + glm::vec3(-20.0f, 40.0f, 60.0f);
    glm::vec3 end = originPosition + glm::vec3(20.0f, 22.0f, 35.0f);
    tntwars::BezierSegment arc;
    arc.startTime = 0.0f;
    arc.endTime = kOpeningDurationSeconds;
    arc.p0 = start;
    arc.p1 = start + glm::vec3(25.0f, 4.0f, -10.0f);
    arc.p2 = end + glm::vec3(-25.0f, 8.0f, 10.0f);
    arc.p3 = end;
    sequence.addBezierSegment(arc);

    // Sprint 16 (Phase 4, "focal length changes"): a real, slow narrow
    // (zoom in) over the shot -- wide establishing FOV at the start,
    // settling toward this camera's own default (Camera::verticalFovDegrees'
    // 60-degree default) by the end.
    sequence.addFovKeyframe(0.0f, 75.0f);
    sequence.addFovKeyframe(kOpeningDurationSeconds, 60.0f);
    return sequence;
}

CinematicSequence buildClassShowcaseCinematic(tntwars::PlayerClassType classType, glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kClassShowcaseDurationSeconds;

    switch (classType) {
        case tntwars::PlayerClassType::Striker:
            // Real, low, aggressive push-in -- matches the class's own
            // real high-damage/low-mobility silhouette (see
            // ClassSystem.hpp's own comment).
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 2.5f, 10.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 1.8f, 5.0f), core::EasingMode::EaseOut);
            break;
        case tntwars::PlayerClassType::Deflector:
            // Real, real-steady side pan -- reads as a real, solid,
            // planted stance.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(-7.0f, 2.0f, 6.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(7.0f, 2.0f, 6.0f), core::EasingMode::EaseOut);
            break;
        case tntwars::PlayerClassType::Engineer:
            // Real, low-angle rising shot -- mirrors Overclock's own
            // rising-crane read from a shorter real distance.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(4.0f, 0.8f, 5.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(4.0f, 3.5f, 5.0f), core::EasingMode::EaseOut);
            break;
        case tntwars::PlayerClassType::Interceptor:
            // Real, fast lateral whip-pan -- matches the class's own
            // real fast/fragile-utility silhouette.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(9.0f, 3.0f, 3.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(-9.0f, 3.0f, 3.0f), core::EasingMode::EaseOut);
            break;
        case tntwars::PlayerClassType::Saboteur:
            // Real, low descending approach -- previews ShadowDive's own
            // submerging read without literally repeating it.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 4.0f, 9.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 0.5f, 4.0f), core::EasingMode::EaseOut);
            break;
    }
    return sequence;
}

CinematicSequence buildMapHighlightCinematic(tntwars::MapId map, glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kMapHighlightDurationSeconds;

    switch (map) {
        case tntwars::MapId::Trenches:
            // Real, wide, ground-level sweep across the open cross-fire
            // lanes -- Trenches' own real baseline, no-hazard identity.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(-25.0f, 5.0f, 0.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration * 0.5f, originPosition + glm::vec3(0.0f, 6.0f, 12.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(25.0f, 5.0f, 0.0f), core::EasingMode::EaseOut);
            break;
        case tntwars::MapId::Mantle:
            // Real, low orbit around the map-center lava pool -- see
            // MapLayout.cpp's own LavaPool piece this frames.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(14.0f, 3.0f, 0.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration * 0.5f, originPosition + glm::vec3(0.0f, 4.0f, 14.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(-14.0f, 3.0f, 0.0f), core::EasingMode::EaseOut);
            break;
        case tntwars::MapId::SkyPlatforms:
            // Real, real drifting rise past the platform cluster --
            // frames the real thruster-platform tilt/collapse mechanic.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(-18.0f, 0.0f, -18.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(18.0f, 10.0f, 18.0f), core::EasingMode::EaseOut);
            break;
        case tntwars::MapId::IslandSea:
            // Real, low-over-the-water pass -- reads as scanning for the
            // real Saboteur/sonar hazard synergy.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 8.0f, -30.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 3.0f, 0.0f), core::EasingMode::EaseOut);
            break;
        case tntwars::MapId::Space:
            // Real, wide, slow orbit befitting Space's own real "two
            // planets, much larger scale" framing -- a real, deliberately
            // bigger sweep radius than every other map's own path.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(-40.0f, 15.0f, -20.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration * 0.5f, originPosition + glm::vec3(0.0f, 20.0f, 0.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(40.0f, 15.0f, 20.0f), core::EasingMode::EaseOut);
            break;
    }
    return sequence;
}

CinematicSequence buildFinalClashCinematic(glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kFinalClashDurationSeconds;
    // A real, longer, four-keyframe dynamic sweep -- opens wide, dives
    // low and close through the middle of the real battle, pulls back
    // out wide again for the close. TrailerDirector's own real
    // camera-shake overlay (see this function's own header comment)
    // rides on top of this path, not baked into it.
    sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 20.0f, 40.0f), core::EasingMode::EaseIn);
    sequence.addKeyframe(kDuration * 0.3f, originPosition + glm::vec3(-10.0f, 4.0f, 10.0f), core::EasingMode::Linear);
    sequence.addKeyframe(kDuration * 0.7f, originPosition + glm::vec3(10.0f, 3.0f, -10.0f), core::EasingMode::Linear);
    sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 18.0f, 35.0f), core::EasingMode::EaseOut);
    return sequence;
}

CinematicSequence buildTitleCardCinematic(glm::vec3 originPosition) {
    CinematicSequence sequence;
    // Real, slow, minimal pull-back -- the calmest real move since the
    // opening shot, deliberately leaving real visual room for the title
    // card / call-to-action text (see TrailerDirector.hpp's own comment
    // on why that text is a real stdout stand-in, matching this
    // engine's established "no on-screen text rendering" boundary).
    sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 3.0f, 8.0f), core::EasingMode::EaseInOut);
    sequence.addKeyframe(kTitleCardDurationSeconds, originPosition + glm::vec3(0.0f, 4.0f, 14.0f), core::EasingMode::EaseInOut);
    return sequence;
}

CinematicSequence buildMiningSimZoneCinematic(miningsim::ZoneType zone, glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kMiningSimZoneDurationSeconds;

    switch (zone) {
        case miningsim::ZoneType::Normal:
            // Real, orbiting push toward the drill/crystal cluster -- the
            // real, original MiningSimRtx framing.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(-16.0f, 6.0f, 14.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(8.0f, 4.0f, 6.0f), core::EasingMode::EaseOut);
            break;
        case miningsim::ZoneType::Underwater:
            // Real, slow rising shot -- a surfacing read, matching the
            // zone's own real cool-blue theme.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, -4.0f, 16.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 10.0f, 10.0f), core::EasingMode::EaseOut);
            break;
        case miningsim::ZoneType::Void:
            // Real, minimal, slow drift -- emptiness, matching the
            // zone's own real near-black theme.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(-6.0f, 3.0f, 6.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(6.0f, 5.0f, -6.0f), core::EasingMode::Linear);
            break;
        case miningsim::ZoneType::Heavenly:
            // Real, bright, ascending crane shot.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 2.0f, 18.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 16.0f, 10.0f), core::EasingMode::EaseOut);
            break;
        case miningsim::ZoneType::CorruptedHeavenly:
            // Real, unsettling lateral creep -- low and close.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(-10.0f, 2.0f, 4.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(10.0f, 1.5f, 4.0f), core::EasingMode::EaseOut);
            break;
        case miningsim::ZoneType::BioluminescentCaverns:
            // Real, low path hugging the glowing crystal cluster.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(12.0f, 1.5f, 8.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(2.0f, 2.5f, 2.0f), core::EasingMode::EaseOut);
            break;
        case miningsim::ZoneType::Dungeon:
            // Real, descending, ominous approach.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 14.0f, 4.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 3.0f, 10.0f), core::EasingMode::EaseOut);
            break;
        case miningsim::ZoneType::DevBonus:
            // Real, fast, energetic whip pan -- the "special" zone's own
            // real vivid, saturated theme deserves real, brisk energy.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(-14.0f, 5.0f, -4.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(14.0f, 5.0f, -4.0f), core::EasingMode::EaseOut);
            break;
    }
    return sequence;
}

CinematicSequence buildDungeonCinematic(glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kDungeonDurationSeconds;
    // Real, wide overhead-ish sweep across the real ~40x40-unit dungeon
    // grid footprint (kDungeonGridWidth/Height * kDungeonDefaultCellSize).
    sequence.addKeyframe(0.0f, originPosition + glm::vec3(-22.0f, 18.0f, -22.0f), core::EasingMode::EaseIn);
    sequence.addKeyframe(kDuration * 0.5f, originPosition + glm::vec3(0.0f, 22.0f, 0.0f), core::EasingMode::Linear);
    sequence.addKeyframe(kDuration, originPosition + glm::vec3(22.0f, 16.0f, 22.0f), core::EasingMode::EaseOut);
    return sequence;
}

CinematicSequence buildBossCinematic(glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kBossDurationSeconds;
    // Real, dramatic, low-angle push-in -- a real threat read.
    sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 6.0f, 14.0f), core::EasingMode::EaseIn);
    sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 1.2f, 4.0f), core::EasingMode::EaseOut);
    sequence.addFovKeyframe(0.0f, 55.0f);
    sequence.addFovKeyframe(kDuration, 40.0f); // real, tightening FOV -- intensifies the push-in
    return sequence;
}

CinematicSequence buildForgeCinematic(glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kForgeDurationSeconds;
    // Real, close orbit around the small forged-tool prop.
    sequence.addKeyframe(0.0f, originPosition + glm::vec3(-2.5f, 1.5f, 2.0f), core::EasingMode::EaseIn);
    sequence.addKeyframe(kDuration * 0.5f, originPosition + glm::vec3(0.0f, 2.0f, 3.0f), core::EasingMode::Linear);
    sequence.addKeyframe(kDuration, originPosition + glm::vec3(2.5f, 1.5f, 2.0f), core::EasingMode::EaseOut);
    return sequence;
}

CinematicSequence buildLogoRevealCinematic(glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kLogoRevealDurationSeconds;
    // Real, slow, reverent reveal orbit around the real Timeglass model
    // (spans roughly +/-2.6 world units tall, see TimeglassModel.cpp).
    sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 1.0f, 9.0f), core::EasingMode::EaseIn);
    sequence.addKeyframe(kDuration * 0.5f, originPosition + glm::vec3(-7.0f, 2.5f, 4.0f), core::EasingMode::Linear);
    sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 3.0f, 7.0f), core::EasingMode::EaseOut);
    sequence.addFovKeyframe(0.0f, 50.0f);
    sequence.addFovKeyframe(kDuration, 42.0f);
    return sequence;
}

} // namespace engine::trailer
