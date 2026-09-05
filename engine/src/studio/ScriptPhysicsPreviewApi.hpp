#pragma once

struct lua_State;

namespace engine::core {
class ECS;
}

namespace engine::studio::plugins {
class PhysicsPreviewPlugin;
}

namespace engine::studio {

// Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" -- Luau
// Studio API Bindings, "deterministic physics step triggers"): a real,
// small `physics` table exposing studio::plugins::PhysicsPreviewPlugin's
// pause()/resume()/stepOnce() -- see that class's own header comment for
// why a script-driven step only makes sense while paused (update()'s own
// per-frame auto-step is suspended then, so calling step() can't
// double-step the same tick).
//
// Registered into PhysicsPreviewPlugin's own scripting_ VM (the one real
// core::Scripting instance Studio ever runs against a live core::Physics
// -- see that class's header comment) -- NOT into
// studio::panels::DebugConsolePanel's separate VM, which has no
// core::Physics of its own at all (see studio::registerStudioEcsBindings()'s
// own header comment on that exact gap).
class ScriptPhysicsPreviewApi {
public:
    explicit ScriptPhysicsPreviewApi(plugins::PhysicsPreviewPlugin& preview, core::ECS& ecs);

    void registerInto(lua_State* L);

private:
    static int luaPause(lua_State* L);
    static int luaResume(lua_State* L);
    static int luaIsPaused(lua_State* L);
    static int luaIsPlaying(lua_State* L);
    // physics.step(dt) -> true if a real step ran, false (real, honest --
    // not an error) if not currently playing+paused.
    static int luaStep(lua_State* L);

    plugins::PhysicsPreviewPlugin& preview_;
    core::ECS& ecs_;
};

} // namespace engine::studio
