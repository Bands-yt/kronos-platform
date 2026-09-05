#include "core/ScriptMeshApi.hpp"

#include <glm/glm.hpp>

#include <lua.h>
#include <lualib.h>

#include "core/ECS.hpp"
#include "core/EditableMeshComponent.hpp"

namespace engine::core {

namespace {
EntityId idFromLua(lua_State* L, int idx) {
    return static_cast<EntityId>(static_cast<uint32_t>(luaL_checknumber(L, idx)));
}

// Every luaX function reads `this` off upvalue 1 -- same closure-per-
// function pattern ScriptWorldApi.cpp's own selfFromUpvalue() already
// establishes.
ScriptMeshApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptMeshApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}
} // namespace

ScriptMeshApi::ScriptMeshApi(ECS& ecs) : ecs_(ecs) {}

int ScriptMeshApi::luaBeginEditingBox(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    float hx = static_cast<float>(luaL_checknumber(L, 2));
    float hy = static_cast<float>(luaL_checknumber(L, 3));
    float hz = static_cast<float>(luaL_checknumber(L, 4));
    ECS& ecs = selfFromUpvalue(L)->ecs_;
    if (!ecs.raw().valid(entity)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    auto& component = ecs.addComponent<EditableMeshComponent>(entity);
    component.mesh = EditableMesh::createBox(glm::vec3(hx, hy, hz));
    ++component.editVersion;
    lua_pushboolean(L, 1);
    return 1;
}

int ScriptMeshApi::luaVertexCount(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    lua_pushnumber(L, component != nullptr ? static_cast<double>(component->mesh.vertexCount()) : 0.0);
    return 1;
}

int ScriptMeshApi::luaFaceCount(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    lua_pushnumber(L, component != nullptr ? static_cast<double>(component->mesh.faceCount()) : 0.0);
    return 1;
}

int ScriptMeshApi::luaGetVertexPosition(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    uint32_t index = static_cast<uint32_t>(luaL_checknumber(L, 2));
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    if (component == nullptr || index >= component->mesh.vertexCount()) return 0;
    const glm::vec3& p = component->mesh.vertices()[index].position;
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    lua_pushnumber(L, p.z);
    return 3;
}

int ScriptMeshApi::luaSetVertexPosition(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    uint32_t index = static_cast<uint32_t>(luaL_checknumber(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    bool ok = component != nullptr && index < component->mesh.vertexCount();
    if (ok) {
        component->mesh.setVertexPosition(index, glm::vec3(x, y, z));
        ++component->editVersion;
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int ScriptMeshApi::luaSetVertexUv(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    uint32_t index = static_cast<uint32_t>(luaL_checknumber(L, 2));
    float u = static_cast<float>(luaL_checknumber(L, 3));
    float v = static_cast<float>(luaL_checknumber(L, 4));
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    bool ok = component != nullptr && index < component->mesh.vertexCount();
    if (ok) {
        component->mesh.setVertexUv(index, glm::vec2(u, v));
        ++component->editVersion;
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int ScriptMeshApi::luaExtrudeFace(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    size_t faceIndex = static_cast<size_t>(luaL_checknumber(L, 2));
    float distance = static_cast<float>(luaL_checknumber(L, 3));
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    bool ok = component != nullptr && component->mesh.extrudeFace(faceIndex, distance);
    if (ok) ++component->editVersion;
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int ScriptMeshApi::luaInsetFace(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    size_t faceIndex = static_cast<size_t>(luaL_checknumber(L, 2));
    float amount = static_cast<float>(luaL_checknumber(L, 3));
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    bool ok = component != nullptr && component->mesh.insetFace(faceIndex, amount);
    if (ok) ++component->editVersion;
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int ScriptMeshApi::luaSubdivideFace(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    size_t faceIndex = static_cast<size_t>(luaL_checknumber(L, 2));
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    bool ok = component != nullptr && component->mesh.subdivideFace(faceIndex);
    if (ok) ++component->editVersion;
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int ScriptMeshApi::luaMergeVertices(lua_State* L) {
    EntityId entity = idFromLua(L, 1);
    float threshold = static_cast<float>(luaL_checknumber(L, 2));
    auto* component = selfFromUpvalue(L)->ecs_.tryGetComponent<EditableMeshComponent>(entity);
    size_t merged = component != nullptr ? component->mesh.mergeVertices(threshold) : 0;
    if (merged > 0) ++component->editVersion;
    lua_pushnumber(L, static_cast<double>(merged));
    return 1;
}

void ScriptMeshApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"beginEditingBox", &ScriptMeshApi::luaBeginEditingBox},
        {"vertexCount", &ScriptMeshApi::luaVertexCount},
        {"faceCount", &ScriptMeshApi::luaFaceCount},
        {"getVertexPosition", &ScriptMeshApi::luaGetVertexPosition},
        {"setVertexPosition", &ScriptMeshApi::luaSetVertexPosition},
        {"setVertexUv", &ScriptMeshApi::luaSetVertexUv},
        {"extrudeFace", &ScriptMeshApi::luaExtrudeFace},
        {"insetFace", &ScriptMeshApi::luaInsetFace},
        {"subdivideFace", &ScriptMeshApi::luaSubdivideFace},
        {"mergeVertices", &ScriptMeshApi::luaMergeVertices},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "mesh");
}

} // namespace engine::core
