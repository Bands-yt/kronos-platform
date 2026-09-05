#pragma once

struct lua_State;

namespace engine::core {
class Renderer;
}

namespace engine::studio {

// Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" -- Luau
// Studio API Bindings): a real, deliberately narrow Luau-facing surface
// over core::Renderer's own post-processing tuning knobs -- registered
// as a global `render` table into Studio's trusted DebugConsolePanel VM,
// the same home core::ScriptMeshApi/studio::ScriptCinematicApi/
// studio::registerStudioEcsBindings already use (a first-party developer
// console, not an untrusted third-party gameplay script's VM).
//
// Real, deliberate security boundary, stated plainly (same "never trust
// a script's own raw numbers with a live physics engine" posture
// core::ScriptWorldApi::luaSpawnDynamicBox()'s own header comment
// already establishes for Physics): this exposes ONLY the same real,
// numeric/enum tuning knobs studio::plugins::LightingToolsPlugin's own
// ImGui sliders already drive (exposure, bloom, depth of field,
// cinematic mode, tonemap operator, LUT strength/loading). There is no
// binding anywhere in this class -- by construction, not by a runtime
// permission check -- that returns a raw VkPipeline/VkDevice/
// VkCommandBuffer/VkDescriptorSet/VkImage or any other live Vulkan
// handle to script. A script reachable through this table cannot
// rebuild a pipeline, submit a command buffer, or touch GPU memory
// directly; every value that crosses this boundary is a plain Luau
// number, boolean, or string.
//
// Motion blur/shutter angle is deliberately NOT exposed here: it is a
// process-global Renderer setting trailer::CaptureRig::exportSequence()
// force-overrides to 0 for the whole duration of an export (see that
// method's own comment on why the in-shader velocity-buffer blur is
// incompatible with sequence scrubbing) -- a script mutating it
// mid-export would race that override in a way this class has no way to
// observe or prevent. Every other knob here is safe to change at any
// time, live or mid-export.
class ScriptRenderApi {
public:
    explicit ScriptRenderApi(core::Renderer& renderer);

    void registerInto(lua_State* L);

private:
    static int luaSetExposure(lua_State* L);
    static int luaExposure(lua_State* L);
    static int luaSetBloomSettings(lua_State* L);
    static int luaBloomSettings(lua_State* L); // -> threshold, softKnee, intensity
    static int luaSetCinematicMode(lua_State* L);
    static int luaIsCinematicModeEnabled(lua_State* L);
    static int luaSetDepthOfFieldEnabled(lua_State* L);
    static int luaIsDepthOfFieldEnabled(lua_State* L);
    static int luaSetDepthOfFieldParams(lua_State* L);
    static int luaDepthOfFieldParams(lua_State* L); // -> focusDistance, focusRange, maxCoCRadiusPx
    // "aces" or "agx", case-insensitive -- luaL_error() on anything else
    // rather than silently falling back, so a typo is a real, loud
    // script error instead of a silently-ignored setting.
    static int luaSetTonemapOperator(lua_State* L);
    static int luaTonemapOperator(lua_State* L); // -> "aces" | "agx"
    static int luaSetColorGradingLutStrength(lua_State* L);
    static int luaColorGradingLutStrength(lua_State* L);
    // Real GPU work (Renderer::loadColorGradingLut()) -- -> true, or
    // false + a real error string. Never exercised by engine_tests (no
    // live device there), same "GPU-touching, headlessly untestable"
    // scope every other real Vulkan call in this codebase already states.
    static int luaLoadColorGradingLut(lua_State* L);
    static int luaResetColorGradingLutToIdentity(lua_State* L);

    core::Renderer& renderer_;
};

} // namespace engine::studio
