#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "core/ScriptWorldApi.hpp"

#include <algorithm>

#include <lua.h>
#include <lualib.h>

#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/Hierarchy.hpp"
#include "core/Physics.hpp"
#include "core/RuntimeAnimationPlayer.hpp"

namespace engine::core {

namespace {
EntityId idFromLua(lua_State* L, int idx) {
    return static_cast<EntityId>(static_cast<uint32_t>(luaL_checknumber(L, idx)));
}

// Every luaX function reads `this` off upvalue 1 -- see registerInto()'s
// use of lua_pushlightuserdata + lua_pushcclosure for each entry, the
// same closure-per-function pattern Scripting::registerBindings() uses.
ScriptWorldApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptWorldApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}
} // namespace

ScriptWorldApi::ScriptWorldApi(ECS& ecs, Physics& physics, RuntimeAnimationPlayer& animationPlayer)
    : ecs_(ecs), physics_(physics), animationPlayer_(animationPlayer) {}

int ScriptWorldApi::luaCreateEntity(lua_State* L) {
    const char* name = lua_isnone(L, 1) ? "" : luaL_checkstring(L, 1);
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    EntityId entity = ecs.createEntity(name);
    lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(entity)));
    return 1;
}

int ScriptWorldApi::luaSetParent(lua_State* L) {
    EntityId child = idFromLua(L, 1);
    EntityId parent = idFromLua(L, 2);
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    lua_pushboolean(L, hierarchy::setParent(ecs, child, parent) ? 1 : 0);
    return 1;
}

int ScriptWorldApi::luaUnparent(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    hierarchy::unparent(ecs, entity);
    return 0;
}

int ScriptWorldApi::luaFindByName(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    for (auto entity : ecs.view<Name>()) {
        const auto* nameComp = ecs.tryGetComponent<Name>(entity);
        if (nameComp != nullptr && nameComp->value == name) {
            lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(entity)));
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

int ScriptWorldApi::luaDestroy(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    selfFromUpvalue(L)->ecs_.destroyEntity(entity);
    return 0;
}

int ScriptWorldApi::luaGetPosition(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    auto* transform = ecs.tryGetComponent<Transform>(entity);
    if (transform == nullptr) return 0;
    lua_pushnumber(L, transform->position.x);
    lua_pushnumber(L, transform->position.y);
    lua_pushnumber(L, transform->position.z);
    return 3;
}

int ScriptWorldApi::luaSetPosition(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    glm::vec3 position(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    if (auto* transform = ecs.tryGetComponent<Transform>(entity)) transform->position = position;
    return 0;
}

int ScriptWorldApi::luaGetRotation(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    auto* transform = ecs.tryGetComponent<Transform>(entity);
    if (transform == nullptr) return 0;
    glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(transform->rotation));
    lua_pushnumber(L, eulerDegrees.x);
    lua_pushnumber(L, eulerDegrees.y);
    lua_pushnumber(L, eulerDegrees.z);
    return 3;
}

int ScriptWorldApi::luaSetRotation(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    glm::vec3 eulerDegrees(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    if (auto* transform = ecs.tryGetComponent<Transform>(entity)) {
        transform->rotation = glm::quat(glm::radians(eulerDegrees));
    }
    return 0;
}

int ScriptWorldApi::luaSetScale(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    glm::vec3 scale(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    if (auto* transform = ecs.tryGetComponent<Transform>(entity)) transform->scale = scale;
    return 0;
}

int ScriptWorldApi::luaSetColor(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    glm::vec4 color(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4), luaL_optnumber(L, 5, 1.0));
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    if (auto* renderable = ecs.tryGetComponent<Renderable>(entity)) renderable->baseColor = color;
    return 0;
}

int ScriptWorldApi::luaSetMaterial(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    float metallic = static_cast<float>(luaL_checknumber(L, 2));
    float roughness = static_cast<float>(luaL_checknumber(L, 3));
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    if (auto* renderable = ecs.tryGetComponent<Renderable>(entity)) {
        renderable->metallic = metallic;
        renderable->roughness = roughness;
    }
    return 0;
}

int ScriptWorldApi::luaSetEmissive(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    glm::vec3 color(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    float intensity = static_cast<float>(luaL_checknumber(L, 5));
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    if (auto* renderable = ecs.tryGetComponent<Renderable>(entity)) {
        renderable->emissiveColor = color;
        renderable->emissiveIntensity = intensity;
    }
    return 0;
}

int ScriptWorldApi::luaApplyImpulse(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    glm::vec3 impulse(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    ScriptWorldApi* self = selfFromUpvalue(L);
    self->physics_.applyImpulse(entity, self->ecs_, impulse);
    return 0;
}

int ScriptWorldApi::luaSetVelocity(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    ScriptWorldApi* self = selfFromUpvalue(L);
    self->physics_.setHorizontalVelocity(entity, self->ecs_, glm::vec2(x, z));
    self->physics_.setVerticalVelocity(entity, self->ecs_, y);
    return 0;
}

int ScriptWorldApi::luaPlayAnimation(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    bool looping = lua_isnone(L, 2) ? true : lua_toboolean(L, 2) != 0;
    ScriptWorldApi* self = selfFromUpvalue(L);
    RuntimeAnimationPlayer::Handle handle = self->animationPlayer_.play(path, looping);
    if (handle == RuntimeAnimationPlayer::kInvalidHandle) {
        lua_pushnil(L);
    } else {
        lua_pushnumber(L, static_cast<double>(handle));
    }
    return 1;
}

int ScriptWorldApi::luaStopAnimation(lua_State* L) {
    auto handle = static_cast<RuntimeAnimationPlayer::Handle>(luaL_checknumber(L, 1));
    selfFromUpvalue(L)->animationPlayer_.stop(handle);
    return 0;
}

int ScriptWorldApi::luaRaycast(lua_State* L) {
    glm::vec3 origin(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    glm::vec3 direction(luaL_checknumber(L, 4), luaL_checknumber(L, 5), luaL_checknumber(L, 6));
    float maxDistance = static_cast<float>(luaL_checknumber(L, 7));
    Physics& physics = selfFromUpvalue(L)->physics_;
    Physics::RaycastHit hit = physics.raycast(origin, direction, maxDistance);
    if (!hit.hit) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "hit");
    lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(hit.entity)));
    lua_setfield(L, -2, "entityId");
    lua_pushnumber(L, hit.point.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, hit.point.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, hit.point.z);
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, hit.normal.x);
    lua_setfield(L, -2, "nx");
    lua_pushnumber(L, hit.normal.y);
    lua_setfield(L, -2, "ny");
    lua_pushnumber(L, hit.normal.z);
    lua_setfield(L, -2, "nz");
    lua_pushnumber(L, hit.distance);
    lua_setfield(L, -2, "distance");
    return 1;
}

int ScriptWorldApi::luaSpawnDynamicBox(lua_State* L) {
    ScriptWorldApi* self = selfFromUpvalue(L);
    if (self->spawnBoxMeshHandle_ == 0xFFFFFFFFu) {
        // Real, honest failure -- no spawn-box mesh has been registered
        // yet (see setSpawnBoxMeshHandle()'s own comment); a bare
        // physics body with an invalid meshHandle would silently fail
        // to render, which is a much harder bug for a script author to
        // notice than a clean `nil` return here.
        lua_pushnil(L);
        return 1;
    }

    glm::vec3 position(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    glm::vec3 halfExtent(luaL_checknumber(L, 4), luaL_checknumber(L, 5), luaL_checknumber(L, 6));
    float mass = static_cast<float>(luaL_checknumber(L, 7));
    glm::vec4 color(luaL_optnumber(L, 8, 0.8), luaL_optnumber(L, 9, 0.8), luaL_optnumber(L, 10, 0.8), 1.0);

    // Kronos ("securely expose... physics functions"): a script-supplied
    // shape/mass is real-clamped before it ever reaches Jolt -- a
    // zero/negative extent would produce a degenerate collision shape,
    // and an unbounded one could tank frame time or blow past this
    // Alpha's real memory/perf budgets. Matches the sane, small range
    // every hand-authored physics prop elsewhere in this codebase
    // (main.cpp's own pushableBox/slidingCrate/oreDrop props) already
    // sits within.
    constexpr float kMinHalfExtent = 0.05f;
    constexpr float kMaxHalfExtent = 5.0f;
    constexpr float kMinMass = 0.01f;
    constexpr float kMaxMass = 500.0f;
    halfExtent = glm::clamp(halfExtent, glm::vec3(kMinHalfExtent), glm::vec3(kMaxHalfExtent));
    mass = std::clamp(mass, kMinMass, kMaxMass);

    EntityId entity = self->physics_.createDynamicBox(self->ecs_, position, halfExtent, mass);

    // The shared spawn-box mesh is a fixed 1x1x1 unit cube (see
    // setSpawnBoxMeshHandle()'s own real caller in main.cpp) -- Transform
    // scale stretches it to actually match the real, requested
    // half-extent, so the visible box is the real size it looks like it
    // is, not just "close enough" the way a couple of main.cpp's own
    // older hand-placed props already accept.
    if (auto* renderable = self->ecs_.tryGetComponent<Renderable>(entity)) {
        renderable->meshHandle = self->spawnBoxMeshHandle_;
        renderable->baseColor = color;
    }
    if (auto* transform = self->ecs_.tryGetComponent<Transform>(entity)) {
        transform->scale = halfExtent / 0.5f;
    }

    lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(entity)));
    return 1;
}

void ScriptWorldApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"createEntity", &ScriptWorldApi::luaCreateEntity},
        {"setParent", &ScriptWorldApi::luaSetParent},
        {"unparent", &ScriptWorldApi::luaUnparent},
        {"findByName", &ScriptWorldApi::luaFindByName},
        {"destroy", &ScriptWorldApi::luaDestroy},
        {"getPosition", &ScriptWorldApi::luaGetPosition},
        {"setPosition", &ScriptWorldApi::luaSetPosition},
        {"getRotation", &ScriptWorldApi::luaGetRotation},
        {"setRotation", &ScriptWorldApi::luaSetRotation},
        {"setScale", &ScriptWorldApi::luaSetScale},
        {"setColor", &ScriptWorldApi::luaSetColor},
        {"setMaterial", &ScriptWorldApi::luaSetMaterial},
        {"setEmissive", &ScriptWorldApi::luaSetEmissive},
        {"applyImpulse", &ScriptWorldApi::luaApplyImpulse},
        {"setVelocity", &ScriptWorldApi::luaSetVelocity},
        {"playAnimation", &ScriptWorldApi::luaPlayAnimation},
        {"stopAnimation", &ScriptWorldApi::luaStopAnimation},
        {"raycast", &ScriptWorldApi::luaRaycast},
        {"spawnDynamicBox", &ScriptWorldApi::luaSpawnDynamicBox},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "world");
}

} // namespace engine::core
