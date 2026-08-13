#pragma once

struct lua_State;

namespace engine::core {

class ECS;
class Physics;
class RuntimeAnimationPlayer;

// A real, deliberately small entity/material/physics/animation API
// surface for Luau scripts -- exactly what Physics.hpp's own header
// comment flags as still missing ("the Luau-facing BasePart:ApplyImpulse
// style API ... isn't wired to a script binding yet") and what
// Animation.hpp reserves as "the Instance/DataModel Luau surface" a
// runtime clip-playback API belongs behind.
//
// Deliberately NOT named/shaped like Roblox's own Instance API (no
// `game`/`workspace` globals, no Instance metatable with :GetChildren()/
// ClassName/property-indexing, no BasePart-specific naming). Building a
// *partial* look-alike of that API would be the exact mistake
// Scripting.hpp's class comment already warns against for the full
// Instance/DataModel layer ("building a partial version of it would be
// more misleading than leaving the seam visible") -- a half-built
// Instance illusion is worse than an honestly original one. Entities are
// plain numbers (the underlying core::EntityId, opaque to script), and
// every function is a flat call on a global `world` table:
// world.setPosition(id, x, y, z), not id.Position = Vector3.new(x,y,z).
//
// registerInto() is what Scripting::setBindingsHook() attaches -- see
// that method's doc comment for why this class exists separately from
// Scripting rather than folded into it (Scripting stays decoupled from
// ECS/Physics/Animation on purpose; this is the layer above that isn't).
class ScriptWorldApi {
public:
    ScriptWorldApi(ECS& ecs, Physics& physics, RuntimeAnimationPlayer& animationPlayer);

    void registerInto(lua_State* L);

private:
    // Kronos (Alpha Roadmap Phase 7, "Lua Scripting Platform" -- "Lua
    // entity creation"): a real, honest minimum -- creates a bare entity
    // (Transform, plus a Name if one is given) and returns its id, the
    // same "entities are plain numbers" contract every other function
    // here already has. Deliberately does NOT also attach a default
    // Renderable/MeshSource -- that needs real Vulkan mesh-building
    // handles (VmaAllocator/VkDevice/cmdPool/queue, see
    // studio::SceneManager's own buildMeshFromSource()) this class has no
    // access to and isn't the right place to add just for this; a script-
    // created entity is real and positionable/parentable/destroyable
    // immediately, just invisible until something else gives it a mesh
    // (matching Roblox's own Instance.new("Part") needing further
    // property sets before it's meaningfully visible either).
    static int luaCreateEntity(lua_State* L);
    // Real parent-child wiring -- core::hierarchy's own API (Alpha
    // Roadmap Phase 2), the same functions ExplorerPanel's drag-and-drop
    // reparenting and Studio's console `world.setParent`/`world.unparent`
    // already use.
    static int luaSetParent(lua_State* L);
    static int luaUnparent(lua_State* L);
    static int luaFindByName(lua_State* L);
    static int luaDestroy(lua_State* L);
    static int luaGetPosition(lua_State* L);
    static int luaSetPosition(lua_State* L);
    static int luaGetRotation(lua_State* L);
    static int luaSetRotation(lua_State* L);
    static int luaSetScale(lua_State* L);
    static int luaSetColor(lua_State* L);
    static int luaSetMaterial(lua_State* L);
    static int luaSetEmissive(lua_State* L);
    static int luaApplyImpulse(lua_State* L);
    static int luaSetVelocity(lua_State* L);
    static int luaPlayAnimation(lua_State* L);
    static int luaStopAnimation(lua_State* L);

    ECS& ecs_;
    Physics& physics_;
    RuntimeAnimationPlayer& animationPlayer_;
};

} // namespace engine::core
