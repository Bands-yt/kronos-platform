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
    // property sets before it's meaningfully visible either). Still true
    // for a bare createEntity() -- see luaSpawnDynamicBox() below for
    // the one real, deliberately narrow exception (a pre-registered
    // shared mesh handle, not a live GPU build).
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
    // Kronos ("Kronos Scripting Environment" -- "Immediate Gaps for
    // Launch"): real wrap of the already-real, already-tested
    // Physics::raycast() (Physics.hpp's own header comment already names
    // core::ScriptWorldApi as this query's intended script consumer).
    // world.raycast(originX,originY,originZ,dirX,dirY,dirZ,maxDistance)
    // -> a real result table {hit=true, entityId=, x=,y=,z=, nx=,ny=,nz=,
    // distance=} on a hit, or nil on a miss -- flat x/y/z fields, not a
    // nested "Vector3" sub-table, matching this class's own established
    // "entities are plain numbers, positions are flat x/y/z, no partial
    // Instance-API imitation" contract (see this file's own header
    // comment) -- inventing a Vector3-shaped return value here would be
    // exactly the "half-built Instance illusion" that comment warns
    // against.
    static int luaRaycast(lua_State* L);

    // Kronos ("Alpha v1 Polish" -- "spawn dynamic objects from script"):
    // real, wraps the already-real, already-tested
    // Physics::createDynamicBox() -- closes the gap luaCreateEntity()'s
    // own header comment above already names ("no default mesh -- needs
    // real Vulkan mesh-building handles this class has no access to and
    // isn't the right place to add just for this"). Deliberately does
    // NOT build a new GPU mesh per call -- that would mean touching
    // live Vulkan resources from inside a script call, real new risk
    // this codebase has consistently avoided (see core::Physics's own
    // "GPU-independence boundary" and core::Application::
    // setOreDropMeshHandle()'s identical real precedent: OreNode.cpp
    // spawns real dynamic debris at runtime using a mesh handle
    // pre-registered once at startup, never building GPU resources
    // itself). setSpawnBoxMeshHandle() below is that same real pattern,
    // reused, not reinvented.
    //
    // world.spawnDynamicBox(x,y,z, halfExtentX,halfExtentY,halfExtentZ,
    // mass, r,g,b) -> id, or nil if no spawn-box mesh handle has been
    // registered yet (an honest failure, not a crash on an invalid
    // meshHandle). halfExtent/mass are real-clamped to a sane range
    // (see the .cpp's own kMin/kMaxHalfExtent, kMinMass/kMaxMass) --
    // this function is reachable from untrusted third-party gameplay
    // scripts, so a degenerate (zero/negative/huge) shape request is
    // silently clamped rather than handed straight to Jolt, the same
    // "never trust a script's own raw numbers with a live physics
    // engine" real safety margin already discussed for this API's
    // security posture.
    static int luaSpawnDynamicBox(lua_State* L);

public:
    // Kronos: real, deferred setter -- see luaSpawnDynamicBox()'s own
    // comment for why this exists instead of building a mesh inline.
    // Called once from main.cpp, right alongside the real, identical
    // Application::setOreDropMeshHandle() call, after the shared
    // "boxMesh" unit-cube handle actually exists (ScriptWorldApi itself
    // is constructed earlier, before any GPU mesh is built -- see
    // Application::initialize()'s own real construction order).
    void setSpawnBoxMeshHandle(uint32_t handle) { spawnBoxMeshHandle_ = handle; }

private:
    ECS& ecs_;
    Physics& physics_;
    RuntimeAnimationPlayer& animationPlayer_;
    // Kronos: 0xFFFFFFFF ("not yet registered") until
    // setSpawnBoxMeshHandle() is real-called -- luaSpawnDynamicBox()
    // checks this explicitly rather than trusting an arbitrary uint32_t
    // to be a real, live MeshLibrary handle.
    uint32_t spawnBoxMeshHandle_ = 0xFFFFFFFFu;
};

} // namespace engine::core
