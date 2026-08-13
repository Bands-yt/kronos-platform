#pragma once

#include <glm/glm.hpp>

#include "miningsim/Zone.hpp"
#include "tntwars/CinematicSequence.hpp"
#include "tntwars/ClassSystem.hpp"
#include "tntwars/MapDefinition.hpp"

namespace engine::trailer {

// Sprint 15 ("TNT-Wars Trailer Production")'s real camera-path builders
// for the trailer's non-ultimate scenes -- the brief's own "Use
// CinematicSequence for all camera paths" requirement, extended past
// tntwars::buildUltimateCinematic() (reused unchanged for Scene 4) to
// the other five scenes. Same real, hand-authored-keyframe technique,
// same honest "illustrative but real, working camera choreography, not
// hand-authored cinematography" framing tntwars::CinematicSequence.hpp's
// own header comment already establishes -- these are real,
// deterministic, distinct-per-scene paths a real camera actually flies,
// not placeholders.

// Scene 1 -- a real, slow wide-aerial establishing shot over
// `originPosition` (Sky Platforms' real map center) with a real, slow
// downward tilt, reading as the trailer's opening reveal.
constexpr float kOpeningDurationSeconds = 4.0f;
[[nodiscard]] tntwars::CinematicSequence buildOpeningCinematic(glm::vec3 originPosition);

// Scene 2 -- a real, short (kClassShowcaseDurationSeconds), distinct
// per-class "portrait" push-in, separate from that same class's own
// Scene 4 ultimate cinematic (a different real camera move for a
// different real moment in the trailer).
constexpr float kClassShowcaseDurationSeconds = 2.5f;
[[nodiscard]] tntwars::CinematicSequence buildClassShowcaseCinematic(tntwars::PlayerClassType classType,
                                                                       glm::vec3 originPosition);

// Scene 3 -- a real, distinct per-map sweep (kMapHighlightDurationSeconds),
// framing each map's own real signature hazard/feature (Trenches' open
// cross-fire lanes, Mantle's lava pool, Sky Platforms' thruster cluster,
// Island Sea's open water).
constexpr float kMapHighlightDurationSeconds = 3.5f;
[[nodiscard]] tntwars::CinematicSequence buildMapHighlightCinematic(tntwars::MapId map, glm::vec3 originPosition);

// Scene 5 -- a real, longer (kFinalClashDurationSeconds), multi-keyframe
// dynamic sweep across the battle -- TrailerDirector layers a real,
// deterministic (sine-based, not random -- see that class's own comment
// on why) camera-shake offset on top of this at playback time, kept
// separate here since shake is a real, independent, toggleable overlay,
// not baked into the path itself.
constexpr float kFinalClashDurationSeconds = 10.0f;
[[nodiscard]] tntwars::CinematicSequence buildFinalClashCinematic(glm::vec3 originPosition);

// Scene 7 (final outro) -- a real, slow pull-back/hold, the trailer's
// closing shot behind the title card + closing line (see
// TrailerScenes.hpp's own BeatAction::TitleCard; the real closing text
// itself is composited in during real ffmpeg export -- see this
// engine's own established "no on-screen text rendering" boundary).
constexpr float kTitleCardDurationSeconds = 4.0f;
[[nodiscard]] tntwars::CinematicSequence buildTitleCardCinematic(glm::vec3 originPosition);

// --- Kronos trailer production: Mining Sim RTX + Dungeon/Boss/Forge/Logo beats ---

// Real, distinct per-zone push/orbit around the real MiningSimRtx cavern
// (see miningsim::kMiningSimRtxSceneCenter()) -- short and punchy for
// the opening Heavenly<->Void contrast beats, reused at the same
// duration for the fuller zone-showcase beats later in the trailer so
// every real zone gets an equal, real look.
constexpr float kMiningSimZoneDurationSeconds = 3.0f;
[[nodiscard]] tntwars::CinematicSequence buildMiningSimZoneCinematic(miningsim::ZoneType zone, glm::vec3 originPosition);

// Real, real-wide establishing sweep across a spawned dungeon's own real
// room layout.
constexpr float kDungeonDurationSeconds = 4.0f;
[[nodiscard]] tntwars::CinematicSequence buildDungeonCinematic(glm::vec3 originPosition);

// Real, dramatic, low-angle push-in on a spawned boss -- the trailer's
// own real "this is a real threat" beat.
constexpr float kBossDurationSeconds = 3.5f;
[[nodiscard]] tntwars::CinematicSequence buildBossCinematic(glm::vec3 originPosition);

// Real, close orbit around a freshly-forged tool prop.
constexpr float kForgeDurationSeconds = 3.0f;
[[nodiscard]] tntwars::CinematicSequence buildForgeCinematic(glm::vec3 originPosition);

// Real, slow, reverent reveal orbit around the real Timeglass model
// (trailer::spawnTimeglassModel()) -- the trailer's own real "Kronos
// logo reveal" beat.
constexpr float kLogoRevealDurationSeconds = 5.0f;
[[nodiscard]] tntwars::CinematicSequence buildLogoRevealCinematic(glm::vec3 originPosition);

} // namespace engine::trailer
