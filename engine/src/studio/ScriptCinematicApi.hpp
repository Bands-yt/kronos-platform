#pragma once

#include <vector>

#include "cinematic/CameraRail.hpp"
#include "cinematic/OfflineExport.hpp"
#include "cinematic/Sequencer.hpp"
#include "core/PhysicalCamera.hpp"

struct lua_State;

namespace engine::studio {

// Kronos ("Cinematic Sequencer Luau & TypeScript Bindings" -- Beta
// Roadmap): the real Luau-facing half of engine::cinematic's Sequencer/
// CameraRail/OfflineExport/core::PhysicalCamera -- same "flat global
// table, entities are plain numbers" convention core::ScriptWorldApi.hpp
// and core::ScriptMeshApi.hpp already establish, registered as a global
// `cinematic` table.
//
// Takes the three live data refs it actually needs (Sequence&,
// CameraRail&, ExportSettings&), not a whole studio::plugins::
// MovieModePlugin& -- same "exactly what it needs, not its owner"
// convention ScriptWorldApi's own constructor already follows
// (ECS&/Physics&/RuntimeAnimationPlayer&, not core::Application&). In
// Studio, those three refs are studio::plugins::MovieModePlugin's own
// sequence()/rail()/exportSettings() accessors -- a script and the
// Movie Mode panel's timeline UI genuinely share the same live sequence,
// so a track a script adds shows up in the timeline and vice versa.
//
// core::PhysicalCamera itself has no ECS component/owning entity
// anywhere in this engine (see PhysicalCamera.hpp's own header comment:
// it's a pure conversion between cinematographer units and the
// renderer's own focus-distance/range/CoC knobs) -- this class owns
// exactly ONE, as a real, small, stateful "the camera a script is
// currently reasoning about" (set its fields, then ask real physical
// questions: field of view, depth-of-field range, hyperfocal distance,
// relative exposure). Not a multi-camera handle table -- a second
// physical camera is real, separate, deferred scope.
//
// A note on the global table name: engine_runtime's own
// core::TrailerScriptApi (core/TrailerScriptApi.hpp) also registers a
// `cinematic` table, but into core::Application::scripting_ -- a
// completely separate Luau VM/process from Studio's
// studio::panels::DebugConsolePanel, which is this class's own real
// call site. The two tables never coexist in the same lua_State, so
// there's no real collision, just a shared, deliberately consistent
// name for "the cinematic authoring surface," each scoped to what its
// own process actually has live (engine_runtime: a pre-authored
// trailer's playback controls; Studio: the Sequence/rail/export data
// model itself).
//
// Real, honest scope on export: buildExportSchedule() below is real --
// it validates ExportSettings and computes the real per-frame job list
// (cinematic::buildExportSchedule(), OfflineExport.hpp), the same
// schedule studio::plugins::MovieModePlugin::buildExport() already
// builds for its own "Render Movie Sequence" modal. It does NOT execute
// a real GPU capture to disk -- the only real frame-capture code in this
// engine (trailer::CaptureRig::captureFrame()) writes PPM only, is
// deliberately never exercised by engine_tests (see that class's own
// header comment: "real, GPU-touching"), and EXR has no writer library
// anywhere in this codebase's dependency tree. Wiring a real multi-frame
// PNG/EXR capture loop is real, separate, GPU-verifiable-only scope this
// class doesn't attempt to fake with a function that would silently
// no-op.
class ScriptCinematicApi {
public:
    ScriptCinematicApi(cinematic::Sequence& sequence, cinematic::CameraRail& rail,
                        cinematic::ExportSettings& exportSettings);

    void registerInto(lua_State* L);

private:
    static int luaAddTrack(lua_State* L);
    static int luaTrackCount(lua_State* L);
    static int luaRemoveTrack(lua_State* L);
    static int luaSetTrackTarget(lua_State* L);
    static int luaSetTrackMuted(lua_State* L);
    static int luaAddKeyframe(lua_State* L);
    static int luaSampleChannel(lua_State* L);
    static int luaAddEvent(lua_State* L);

    static int luaSetFrameRate(lua_State* L);
    static int luaFrameRate(lua_State* L);
    static int luaDurationSeconds(lua_State* L);
    static int luaPlay(lua_State* L);
    static int luaPause(lua_State* L);
    static int luaIsPlaying(lua_State* L);
    static int luaSetPlayhead(lua_State* L);
    static int luaPlayheadSeconds(lua_State* L);
    static int luaStepFrames(lua_State* L);
    static int luaSetLoopRegion(lua_State* L);
    static int luaLoopEnabled(lua_State* L);
    static int luaAdvance(lua_State* L);

    static int luaAddRailPoint(lua_State* L);
    static int luaRailPointCount(lua_State* L);
    static int luaClearRail(lua_State* L);
    static int luaSampleRailPosition(lua_State* L);
    static int luaSampleRail(lua_State* L);

    static int luaSetPhysicalCamera(lua_State* L);
    static int luaVerticalFovDegrees(lua_State* L);
    static int luaDepthOfFieldRangeMeters(lua_State* L);
    static int luaHyperfocalDistanceMeters(lua_State* L);
    static int luaRelativeExposure(lua_State* L);

    static int luaBuildExportSchedule(lua_State* L);
    static int luaExportFrameCount(lua_State* L);

    cinematic::Sequence& sequence_;
    cinematic::CameraRail& rail_;
    cinematic::ExportSettings& exportSettings_;
    core::PhysicalCamera physicalCamera_;
    std::vector<cinematic::ExportFrameJob> lastSchedule_;
};

} // namespace engine::studio
