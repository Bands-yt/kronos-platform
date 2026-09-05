#pragma once

struct lua_State;

namespace engine::core {

class ECS;

// Kronos ("Live Collaboration & In-Studio 3D Modeling Pipeline" -- Beta
// Roadmap, Dynamic Mesh API): the real Luau-facing half of
// core::EditableMesh (EditableMesh.hpp) and core::EditableMeshComponent
// (EditableMeshComponent.hpp) -- same "flat global table, entities are
// plain numbers" convention core::ScriptWorldApi.hpp already establishes
// (see that header's own class comment for why this is deliberately NOT
// a partial Instance-style look-alike), just for mesh topology instead
// of transform/physics/animation.
//
// Real, honest scope: every function below reads/writes an entity's
// EditableMeshComponent directly -- it does NOT re-upload the result to
// the GPU itself (this class has no VmaAllocator/VkDevice/cmdPool/queue
// access, same non-goal ScriptWorldApi::luaCreateEntity()'s own comment
// already states for a bare script-created entity's Renderable). Every
// successful mutation below DOES bump EditableMeshComponent::editVersion
// (see that field's own header comment) -- that's the real "dirty since
// last upload" flag a future pass was originally deferred to add;
// studio::plugins::ModelingModePlugin::update() now watches it and
// re-uploads automatically, once per frame, for any entity whose
// editVersion has moved past what it last uploaded, so a script-driven
// edit becomes visible in the Studio viewport even with the panel
// closed. engine_runtime has no equivalent sweep -- a mesh edited this
// way outside Studio stays CPU-side only, matching the same "Vulkan-
// coupled re-upload is real, separate scope" boundary.
//
// Wired into studio::panels::DebugConsolePanel's own live Scripting
// instance (Studio's REPL console) -- NOT into engine_runtime's
// Application::scripting_, which has no ModelingModePlugin/re-upload
// sweep to make a script-driven edit visible; adding it there would
// register a `mesh` table whose calls silently never render, which is
// worse than not having it. This class itself is also fully, headlessly
// tested (test_main.cpp) independent of either call site, the same
// "reachable through a real Luau VM, no GPU/window needed" pattern
// ScriptWorldApi's own tests already establish.
class ScriptMeshApi {
public:
    explicit ScriptMeshApi(ECS& ecs);

    void registerInto(lua_State* L);

private:
    // Attaches a fresh EditableMeshComponent, seeded via
    // EditableMesh::createBox(), to an already-existing entity -- the
    // one real seed this class knows how to build, matching
    // ModelingModePlugin's own "Start Editing" scope note (that plugin's
    // header comment) exactly. Returns false (no-op) for an invalid
    // entity.
    static int luaBeginEditingBox(lua_State* L);
    static int luaVertexCount(lua_State* L);
    static int luaFaceCount(lua_State* L);
    static int luaGetVertexPosition(lua_State* L);
    static int luaSetVertexPosition(lua_State* L);
    static int luaSetVertexUv(lua_State* L);
    static int luaExtrudeFace(lua_State* L);
    static int luaInsetFace(lua_State* L);
    static int luaSubdivideFace(lua_State* L);
    static int luaMergeVertices(lua_State* L);

    ECS& ecs_;
};

} // namespace engine::core
