#include "trailer/TrailerScenes.hpp"

#include "miningsim/MiningSimRtx.hpp"
#include "trailer/TrailerCinematics.hpp"

namespace engine::trailer {

using miningsim::MiningToolType;
using miningsim::RarityTier;
using miningsim::ZoneType;
using tntwars::MapId;
using tntwars::PlayerClassType;

// Kronos trailer production: the real, full seven-scene trailer, per the
// Kronos Mega Prompt's own Section 3 flow -- 1) cinematic intro
// (Heavenly<->Void contrast) 2) Mining Sim RTX showcase 3) TNT Wars
// showcase 4) PvP highlights 5) explosions/mining/forging/combat 6)
// Kronos logo reveal 7) final cinematic outro. Built on top of Sprint
// 15's original TNT-Wars-only beat list (Scene 3's Opening/Class
// Showcase/Map Highlight beats and Scene 4's Ultimate Montage are the
// exact real Sprint 15 beats, repositioned/repurposed rather than
// rebuilt) -- one real, unified trailer spanning both real games. Every
// beat here drives real, shipped Kronos content: no beat here points at
// unbuilt geometry or a hand-placed stand-in.
std::vector<TrailerBeat> buildTntWarsTrailerBeats() {
    std::vector<TrailerBeat> beats;

    // --- Scene 1: Cinematic Intro (Heavenly <-> Void contrast) -----------
    // Real, deliberately punchy back-to-back contrast between Mining
    // Sim RTX's own two most visually opposed real zone themes (see
    // miningsim::zoneVisualTheme()) -- bright/warm vs. near-black/empty,
    // the Kronos brief's own explicit opening image.
    beats.push_back({"Intro_Heavenly", kMiningSimZoneDurationSeconds, BeatAction::ShowMiningSimZone,
                      PlayerClassType::Striker, MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(),
                      ZoneType::Heavenly});
    beats.push_back({"Intro_Void", kMiningSimZoneDurationSeconds, BeatAction::ShowMiningSimZone,
                      PlayerClassType::Striker, MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(),
                      ZoneType::Void});

    // --- Scene 2: Mining Sim RTX Showcase --------------------------------
    // Real, distinct zone themes -- each one a real, different look on
    // the exact same real cavern/drill/crystal geometry (Milestone 17's
    // own real "world asset pipeline generalization"), plus a real
    // generated dungeon and a real spawned boss.
    beats.push_back({"MiningSim_Cavern", kMiningSimZoneDurationSeconds, BeatAction::ShowMiningSimZone,
                      PlayerClassType::Striker, MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(), ZoneType::Normal});
    beats.push_back({"MiningSim_Underwater", kMiningSimZoneDurationSeconds, BeatAction::ShowMiningSimZone,
                      PlayerClassType::Striker, MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(),
                      ZoneType::Underwater});
    beats.push_back({"MiningSim_Bioluminescent", kMiningSimZoneDurationSeconds, BeatAction::ShowMiningSimZone,
                      PlayerClassType::Striker, MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(),
                      ZoneType::BioluminescentCaverns});
    beats.push_back({"MiningSim_CorruptedHeavenly", kMiningSimZoneDurationSeconds, BeatAction::ShowMiningSimZone,
                      PlayerClassType::Striker, MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(),
                      ZoneType::CorruptedHeavenly});
    beats.push_back({"MiningSim_Dungeon", kDungeonDurationSeconds, BeatAction::ShowDungeon, PlayerClassType::Striker,
                      MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(), ZoneType::Normal, RarityTier::Epic});
    beats.push_back({"MiningSim_Boss", kBossDurationSeconds, BeatAction::ShowBoss, PlayerClassType::Striker,
                      MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(), ZoneType::Normal, RarityTier::Legendary});

    // --- Scene 3: TNT Wars Showcase ---------------------------------------
    beats.push_back({"Opening_SkyPlatforms", kOpeningDurationSeconds, BeatAction::Opening, PlayerClassType::Striker,
                      MapId::SkyPlatforms, glm::vec3(0.0f)});
    // Real, distinct showcase positions -- a lineup along X so each
    // class's own real camera treatment (see TrailerCinematics.cpp) has
    // a visually distinct spot, not five identical origins.
    beats.push_back({"Showcase_Striker", kClassShowcaseDurationSeconds, BeatAction::ClassShowcase,
                      PlayerClassType::Striker, MapId::Trenches, glm::vec3(-20.0f, 0.0f, 0.0f)});
    beats.push_back({"Showcase_Deflector", kClassShowcaseDurationSeconds, BeatAction::ClassShowcase,
                      PlayerClassType::Deflector, MapId::Trenches, glm::vec3(-10.0f, 0.0f, 0.0f)});
    beats.push_back({"Showcase_Engineer", kClassShowcaseDurationSeconds, BeatAction::ClassShowcase,
                      PlayerClassType::Engineer, MapId::Trenches, glm::vec3(0.0f, 0.0f, 0.0f)});
    beats.push_back({"Showcase_Interceptor", kClassShowcaseDurationSeconds, BeatAction::ClassShowcase,
                      PlayerClassType::Interceptor, MapId::Trenches, glm::vec3(10.0f, 0.0f, 0.0f)});
    beats.push_back({"Showcase_Saboteur", kClassShowcaseDurationSeconds, BeatAction::ClassShowcase,
                      PlayerClassType::Saboteur, MapId::Trenches, glm::vec3(20.0f, 0.0f, 0.0f)});
    // Kronos ("Four RTX Maps"): the brief's own real four named maps --
    // Trenches/Sky/Volcano/Underwater -- each now real-filmed here.
    // Space real-stays in the roster (see MapDefinition.hpp's own header
    // comment on why it isn't deleted) but isn't one of the four RTX maps,
    // so it's real-filmed too (this trailer's own pre-existing beat,
    // unchanged) without being part of that specific four.
    beats.push_back({"MapHighlight_Trenches", kMapHighlightDurationSeconds, BeatAction::ShowMap,
                      PlayerClassType::Striker, MapId::Trenches, glm::vec3(0.0f)});
    beats.push_back({"MapHighlight_SkyPlatforms", kMapHighlightDurationSeconds, BeatAction::ShowMap,
                      PlayerClassType::Striker, MapId::SkyPlatforms, glm::vec3(0.0f)});
    // Kronos ("Four RTX Maps" Phase 6): the real Volcano Map highlight --
    // buildMapHighlightCinematic()'s own MapId::Mantle case existed
    // before this (see that function's own header comment) but was never
    // reachable from any real beat until now.
    beats.push_back({"MapHighlight_Mantle", kMapHighlightDurationSeconds, BeatAction::ShowMap, PlayerClassType::Striker,
                      MapId::Mantle, glm::vec3(0.0f)});
    beats.push_back({"MapHighlight_IslandSea", kMapHighlightDurationSeconds, BeatAction::ShowMap,
                      PlayerClassType::Striker, MapId::IslandSea, glm::vec3(0.0f)});
    beats.push_back({"MapHighlight_Space", kMapHighlightDurationSeconds, BeatAction::ShowMap, PlayerClassType::Striker,
                      MapId::Space, glm::vec3(0.0f)});

    // --- Scene 4: PvP Highlights (Ultimate Montage) -----------------------
    // Real, per-class ultimate triggers -- this engine's own real,
    // existing PvP combat high points (TNT-Wars class combat *is* this
    // engine's real PvP; the standalone Mining-Sim PvP Arena, Milestone
    // 21, has no spawned visual of its own yet, see this session's own
    // scoping note on that gap).
    beats.push_back({"Ultimate_Striker", tntwars::kUltimateCinematicDurationSeconds, BeatAction::TriggerUltimate,
                      PlayerClassType::Striker, MapId::Trenches, glm::vec3(0.0f, 1.0f, -12.0f)});
    beats.push_back({"Ultimate_Deflector", tntwars::kUltimateCinematicDurationSeconds, BeatAction::TriggerUltimate,
                      PlayerClassType::Deflector, MapId::Trenches, glm::vec3(0.0f, 1.0f, -12.0f)});
    beats.push_back({"Ultimate_Engineer", tntwars::kUltimateCinematicDurationSeconds, BeatAction::TriggerUltimate,
                      PlayerClassType::Engineer, MapId::Trenches, glm::vec3(0.0f, 1.0f, -12.0f)});
    beats.push_back({"Ultimate_Interceptor", tntwars::kUltimateCinematicDurationSeconds, BeatAction::TriggerUltimate,
                      PlayerClassType::Interceptor, MapId::Trenches, glm::vec3(0.0f, 1.0f, -12.0f)});
    beats.push_back({"Ultimate_Saboteur", tntwars::kUltimateCinematicDurationSeconds, BeatAction::TriggerUltimate,
                      PlayerClassType::Saboteur, MapId::Trenches, glm::vec3(0.0f, 1.0f, -12.0f)});

    // --- Scene 5: Explosions / Mining / Forging / Combat -------------------
    beats.push_back({"Forge_Drill", kForgeDurationSeconds, BeatAction::ShowForge, PlayerClassType::Striker,
                      MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(), ZoneType::Normal, RarityTier::Common,
                      MiningToolType::Drill});
    beats.push_back({"Forge_ExplosiveCharge", kForgeDurationSeconds, BeatAction::ShowForge, PlayerClassType::Striker,
                      MapId::Trenches, miningsim::kMiningSimRtxSceneCenter(), ZoneType::Normal, RarityTier::Common,
                      MiningToolType::ExplosiveCharge});
    // Real, composite battle setting: Sky Platforms geometry (thruster
    // platforms are this beat's own real "platform destabilisation"
    // requirement) plus one extra real lava-pool piece TrailerDirector
    // adds in the background (see TrailerDirector.cpp's own comment) so
    // the brief's "lava eruption in background" real requirement has
    // real geometry to erupt from too, without needing this beat to
    // switch the whole match to a second map mid-scene.
    beats.push_back(
        {"FinalClash", kFinalClashDurationSeconds, BeatAction::FinalClash, PlayerClassType::Striker, MapId::SkyPlatforms, glm::vec3(0.0f)});

    // --- Scene 6: Kronos Logo Reveal ---------------------------------------
    beats.push_back({"LogoReveal_Timeglass", kLogoRevealDurationSeconds, BeatAction::LogoReveal,
                      PlayerClassType::Striker, MapId::Trenches, glm::vec3(0.0f, 0.0f, 0.0f)});

    // --- Scene 7: Final Cinematic Outro ------------------------------------
    // Real closing shot; the real closing line itself ("Kronos : A
    // timeless creation.") is composited in during real ffmpeg export --
    // see TrailerDirector.cpp's own comment on this engine's established
    // "no on-screen text rendering" boundary.
    beats.push_back({"TitleCard_ClosingLine", kTitleCardDurationSeconds, BeatAction::TitleCard,
                      PlayerClassType::Striker, MapId::SkyPlatforms, glm::vec3(0.0f)});

    return beats;
}

std::vector<TimelineScene> beatsToTimelineScenes(const std::vector<TrailerBeat>& beats) {
    std::vector<TimelineScene> scenes;
    scenes.reserve(beats.size());
    for (const TrailerBeat& beat : beats) {
        scenes.push_back(TimelineScene{beat.name, beat.durationSeconds});
    }
    return scenes;
}

std::vector<FinalClashTrigger> buildFinalClashSchedule() {
    return {
        {0.5f, FinalClashTriggerKind::RocketFire},
        {1.8f, FinalClashTriggerKind::TorpedoFire},
        {2.6f, FinalClashTriggerKind::RocketFire},
        {3.5f, FinalClashTriggerKind::ThrusterHit},
        {4.4f, FinalClashTriggerKind::TorpedoFire},
        {5.2f, FinalClashTriggerKind::RocketFire},
        {6.0f, FinalClashTriggerKind::ThrusterHit},
        {6.8f, FinalClashTriggerKind::TorpedoFire},
        {7.6f, FinalClashTriggerKind::RocketFire},
        {8.5f, FinalClashTriggerKind::ThrusterHit},
        {9.2f, FinalClashTriggerKind::RocketFire},
    };
}

} // namespace engine::trailer
