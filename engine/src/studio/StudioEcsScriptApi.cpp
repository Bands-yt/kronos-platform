#include "studio/StudioEcsScriptApi.hpp"

#include <lua.h>
#include <lualib.h>

#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/Hierarchy.hpp"

namespace engine::studio {

namespace {

core::ECS& ecsFromUpvalue(lua_State* L) {
    return *static_cast<core::ECS*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}

core::EntityId idFromLua(lua_State* L, int idx) {
    return static_cast<core::EntityId>(static_cast<uint32_t>(luaL_checknumber(L, idx)));
}

int luaFindByName(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    core::ECS& ecs = ecsFromUpvalue(L);
    for (auto entity : ecs.view<core::Name>()) {
        const auto* nameComp = ecs.tryGetComponent<core::Name>(entity);
        if (nameComp != nullptr && nameComp->value == name) {
            lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(entity)));
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

int luaGetPosition(lua_State* L) {
    core::EntityId entity = idFromLua(L, 1);
    core::ECS& ecs = ecsFromUpvalue(L);
    auto* transform = ecs.tryGetComponent<core::Transform>(entity);
    if (transform == nullptr) return 0;
    lua_pushnumber(L, transform->position.x);
    lua_pushnumber(L, transform->position.y);
    lua_pushnumber(L, transform->position.z);
    return 3;
}

int luaSetPosition(lua_State* L) {
    core::EntityId entity = idFromLua(L, 1);
    glm::vec3 position(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    core::ECS& ecs = ecsFromUpvalue(L);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;
    return 0;
}

int luaSetColor(lua_State* L) {
    core::EntityId entity = idFromLua(L, 1);
    glm::vec4 color(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4), luaL_optnumber(L, 5, 1.0));
    core::ECS& ecs = ecsFromUpvalue(L);
    if (auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) renderable->baseColor = color;
    return 0;
}

// Alpha Roadmap Phase 2 ("Scene system"): real console access to the same
// core::hierarchy API ExplorerPanel's drag-and-drop reparenting uses --
// see core/Hierarchy.hpp's own doc comments for exactly what each call
// promises (cycle/self-parent rejection, world-position preservation on
// unparent). Deliberately just these two, not a general Instance-creation
// API -- world.createEntity() et al are real, flagged-missing Phase 7
// scope (Lua scripting platform), not this phase's.
int luaSetParent(lua_State* L) {
    core::EntityId child = idFromLua(L, 1);
    core::EntityId parent = idFromLua(L, 2);
    core::ECS& ecs = ecsFromUpvalue(L);
    lua_pushboolean(L, core::hierarchy::setParent(ecs, child, parent) ? 1 : 0);
    return 1;
}

int luaUnparent(lua_State* L) {
    core::EntityId entity = idFromLua(L, 1);
    core::ECS& ecs = ecsFromUpvalue(L);
    core::hierarchy::unparent(ecs, entity);
    return 0;
}

} // namespace

void registerStudioEcsBindings(lua_State* L, core::ECS& ecs) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"findByName", &luaFindByName},
        {"getPosition", &luaGetPosition},
        {"setPosition", &luaSetPosition},
        {"setColor", &luaSetColor},
        {"setParent", &luaSetParent},
        {"unparent", &luaUnparent},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, &ecs);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "world");
}

} // namespace engine::studio
