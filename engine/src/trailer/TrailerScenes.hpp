#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "miningsim/MiningTools.hpp"
#include "miningsim/Rarity.hpp"
#include "miningsim/Zone.hpp"
#include "tntwars/ClassSystem.hpp"
#include "tntwars/MapDefinition.hpp"
#include "trailer/TrailerTimeline.hpp"

namespace engine::trailer {

// What real, one-shot action (if any) TrailerDirector::tick() dispatches
// exactly once, right when a beat starts -- a real, pure enum; no
// GPU/gameplay behavior lives here, just what kind of trigger this beat
// represents (see TrailerDirector.cpp's own dispatch switch).
// ShowMiningSimZone/ShowDungeon/ShowBoss/ShowForge/LogoReveal (Kronos
// trailer production) are the real Mining-Sim-side beats, added
// alongside the original Sprint 15 TNT-Wars beats -- one real,
// unified trailer, both real games.
enum class BeatAction {
    None,
    Opening,
    ShowMap,
    ClassShowcase,
    TriggerUltimate,
    FinalClash,
    ShowMiningSimZone,
    ShowDungeon,
    ShowBoss,
    ShowForge,
    LogoReveal,
    TitleCard,
};

// One real, flat entry in the TNT-Wars trailer -- Sprint 15's brief,
// section by section, laid out as one ordered real list rather than a
// nested "6 scenes, each with sub-beats" structure: flat is simpler to
// query/test/scrub (a Studio timeline scrubber or TrailerTimeline query
// just wants "the Nth real beat", not two levels of indexing), and nothing
// in this trailer's real structure actually needs cross-beat nesting.
struct TrailerBeat {
    std::string name;
    float durationSeconds = 0.0f;
    BeatAction action = BeatAction::None;
    // Meaningful for ClassShowcase/TriggerUltimate beats.
    tntwars::PlayerClassType classType = tntwars::PlayerClassType::Striker;
    // Meaningful for ShowMap beats (and FinalClash, which always runs on
    // Sky Platforms -- see buildTntWarsTrailerBeats()' own comment).
    tntwars::MapId mapId = tntwars::MapId::Trenches;
    // Real, per-beat world-space reference point TrailerDirector's real
    // camera path (see TrailerCinematics.hpp) and look-at target orbit
    // around/aim toward.
    glm::vec3 cameraOrigin{0.0f};
    // Kronos trailer production: meaningful for ShowMiningSimZone beats
    // (which real zone theme, see miningsim::zoneVisualTheme()) and
    // ShowDungeon/ShowBoss beats (real rarity -- see `rarity` below).
    miningsim::ZoneType zoneType = miningsim::ZoneType::Normal;
    // Meaningful for ShowDungeon/ShowBoss beats -- the real rarity tier
    // that dungeon/boss is generated/spawned at.
    miningsim::RarityTier rarity = miningsim::RarityTier::Common;
    // Meaningful for ShowForge beats -- which real MiningToolType this
    // forge-reveal shot shows.
    miningsim::MiningToolType tool = miningsim::MiningToolType::Pickaxe;
};

// Sprint 15 ("TNT-Wars Trailer Production")'s real, exact trailer
// content: Scene 1 (Opening, 1 beat) -> Scene 2 (Class Showcase, 5
// beats, one per tntwars::PlayerClassType) -> Scene 3 (Map Highlights, 4
// beats, one per tntwars::MapId) -> Scene 4 (Ultimate Montage, 5 beats,
// one per class's real ultimate) -> Scene 5 (Final Clash, 1 beat) ->
// Scene 6 (Title Card + Call to Action, 1 beat) -- 17 real beats total,
// ~68.5 real seconds. Pure data + pure construction -- no GPU dependency
// at all, real and independently testable from TrailerDirector's own
// GPU-touching playback.
[[nodiscard]] std::vector<TrailerBeat> buildTntWarsTrailerBeats();

// Real convenience: the same real beat list, reduced to plain
// TimelineScene entries (name + duration only) for anything that only
// needs the real scene-timing shape -- TrailerTimeline itself, or a
// Studio scrubber that doesn't care about a beat's trigger/camera data.
[[nodiscard]] std::vector<TimelineScene> beatsToTimelineScenes(const std::vector<TrailerBeat>& beats);

// Scene 5 (Final Clash)'s real, fixed, deterministic sub-beat trigger
// schedule -- rockets/torpedoes/thruster hits interleaved across the
// real kFinalClashDurationSeconds window. Real, pure data (not
// randomized -- see TrailerDirector.hpp's own comment on why this whole
// trailer's playback must be exactly reproducible), dispatched by
// TrailerDirector via real per-tick edge-detection against elapsed local
// time, the same "crossed a checkpoint this tick" technique
// MatchFlowController-adjacent code elsewhere in this engine already
// uses for one-shot triggers.
enum class FinalClashTriggerKind { RocketFire, TorpedoFire, ThrusterHit };
struct FinalClashTrigger {
    float localTimeSeconds = 0.0f;
    FinalClashTriggerKind kind = FinalClashTriggerKind::RocketFire;
};
// Real, fixed, ascending-time-ordered schedule, every entry strictly
// within [0, kFinalClashDurationSeconds).
[[nodiscard]] std::vector<FinalClashTrigger> buildFinalClashSchedule();

} // namespace engine::trailer
