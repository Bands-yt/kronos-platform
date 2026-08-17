#pragma once

namespace engine::core {

class ECS;
class Scripting;

// Kronos ("Studio QoL Sprint" -- "Instant Lua Script Hot-Reload"): the
// real diff-and-reload step Application::tick() already ran inline
// (Application.cpp's own "Phase 7, Lua hot-reload" comment) -- pulled
// out here so Studio's own PhysicsPreviewPlugin can run the *identical*
// logic during Play testing instead of a hand-copied second version that
// could silently drift from engine_runtime's real behavior. Scans every
// live core::Script component; any whose `source` no longer matches
// `loadedSource` gets its stale scriptId real-unloaded and the new
// source real-(re)loaded, exactly as before. Safe to call every tick --
// a no-op scan when nothing changed.
void tickScriptHotReload(ECS& ecs, Scripting& scripting);

} // namespace engine::core
