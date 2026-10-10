#include "core/RobloxServices.hpp"
#include "core/ScriptInstanceApi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"
#include "core/PartBodies.hpp"
#include "core/Physics.hpp"
#include "core/RobloxDataStore.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/RobloxRemoteNet.hpp"
#include "core/Scripting.hpp"

namespace engine::core {
namespace {

constexpr int kInstanceTag = 60;
constexpr const char* kMetaKey = "kronos.Instance.mt";
constexpr const char* kCacheKey = "kronos.Instance.cache";
constexpr const char* kMethodsKey = "kronos.Instance.methods";
constexpr const char* kEcsKey = "kronos.Instance.ecs";
constexpr int kSignalTag = 61;
constexpr int kConnectionTag = 62;
constexpr const char* kSignalMetaKey = "kronos.Signal.mt";
constexpr const char* kConnectionMetaKey = "kronos.Connection.mt";
constexpr const char* kHubKey = "kronos.Signal.hub";
constexpr const char* kGuardKey = "kronos.Signal.guard";
constexpr const char* kStringRequireKey = "kronos.require.string";

struct Proxy {
    InstanceRef ref;
};

struct SignalProxy {
    InstanceRef ref;
    char event[128];
};

struct ConnectionProxy {
    uint64_t id;
};

// Lives in each VM's registry; tells the hub when the VM closes.
struct VmGuard {
    std::shared_ptr<SignalHub> hub;
    lua_State* vm;
};

ECS& ecsOf(lua_State* L) { return *static_cast<ECS*>(lua_tolightuserdata(L, lua_upvalueindex(1))); }

void pushInstance(lua_State* L, InstanceRef ref) {
    if (ref == kNoInstance) {
        lua_pushnil(L);
        return;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, kCacheKey);
    lua_pushnumber(L, static_cast<double>(ref));
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    auto* proxy = static_cast<Proxy*>(lua_newuserdatatagged(L, sizeof(Proxy), kInstanceTag));
    proxy->ref = ref;
    lua_getfield(L, LUA_REGISTRYINDEX, kMetaKey);
    lua_setmetatable(L, -2);
    lua_pushnumber(L, static_cast<double>(ref));
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);
    lua_remove(L, -2);
}

Proxy* toProxy(lua_State* L, int index) { return static_cast<Proxy*>(lua_touserdatatagged(L, index, kInstanceTag)); }

InstanceRef checkSelf(lua_State* L, const char* method) {
    Proxy* proxy = toProxy(L, 1);
    if (proxy == nullptr) luaL_error(L, "Expected ':' not '.' calling member function %s", method);
    return proxy->ref;
}

InstanceRef checkInstanceArg(lua_State* L, int index, const char* method) {
    Proxy* proxy = toProxy(L, index);
    if (proxy == nullptr) luaL_error(L, "%s expects an Instance as argument %d", method, index - 1);
    return proxy->ref;
}

// The __type a Roblox datatype table carries, or "".
std::string datatypeOf(lua_State* L, int index) {
    if (!lua_istable(L, index) || !lua_getmetatable(L, index)) return {};
    lua_rawgetfield(L, -1, "__type");
    std::string type = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 2);
    return type;
}

double rawNumber(lua_State* L, int index, const char* field) {
    lua_rawgetfield(L, index, field);
    const double value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return value;
}

glm::vec3 rawVec(lua_State* L, int index, const char* a, const char* b, const char* c) {
    index = lua_absindex(L, index);
    return {static_cast<float>(rawNumber(L, index, a)), static_cast<float>(rawNumber(L, index, b)),
            static_cast<float>(rawNumber(L, index, c))};
}

// --- InstanceValue <-> Luau --------------------------------------------------

void callConstructor(lua_State* L, const char* global, const char* constructor, int argCount) {
    lua_getglobal(L, global);
    lua_getfield(L, -1, constructor);
    lua_remove(L, -2);
    lua_insert(L, -(argCount + 1));
    lua_call(L, argCount, 1);
}

void pushValue(lua_State* L, const InstanceValue& v) {
    switch (v.type) {
        case InstanceValue::Type::Nil: lua_pushnil(L); return;
        case InstanceValue::Type::Bool: lua_pushboolean(L, v.boolean ? 1 : 0); return;
        case InstanceValue::Type::Number: lua_pushnumber(L, v.number); return;
        case InstanceValue::Type::String: lua_pushlstring(L, v.text.data(), v.text.size()); return;
        case InstanceValue::Type::Vector3:
            lua_pushnumber(L, v.vec.x);
            lua_pushnumber(L, v.vec.y);
            lua_pushnumber(L, v.vec.z);
            callConstructor(L, "Vector3", "new", 3);
            return;
        case InstanceValue::Type::Color3:
        case InstanceValue::Type::BrickColor:
            lua_pushnumber(L, v.vec.x);
            lua_pushnumber(L, v.vec.y);
            lua_pushnumber(L, v.vec.z);
            callConstructor(L, "Color3", "new", 3);
            if (v.type == InstanceValue::Type::BrickColor) callConstructor(L, "BrickColor", "new", 1);
            return;
        case InstanceValue::Type::CFrame: {
            const glm::mat3 m = glm::mat3_cast(v.rot);
            lua_pushnumber(L, v.vec.x);
            lua_pushnumber(L, v.vec.y);
            lua_pushnumber(L, v.vec.z);
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) lua_pushnumber(L, m[column][row]);
            }
            callConstructor(L, "CFrame", "new", 12);
            return;
        }
        case InstanceValue::Type::Enum:
            lua_getglobal(L, "Enum");
            lua_rawgetfield(L, -1, v.enumType.c_str());
            lua_remove(L, -2);
            if (lua_istable(L, -1)) {
                lua_rawgetfield(L, -1, v.text.c_str());
                lua_remove(L, -2);
            }
            return;
        case InstanceValue::Type::Instance: pushInstance(L, v.ref); return;
        case InstanceValue::Type::Vector2:
        case InstanceValue::Type::UDim:
            lua_pushnumber(L, v.vec.x);
            lua_pushnumber(L, v.vec.y);
            callConstructor(L, v.type == InstanceValue::Type::Vector2 ? "Vector2" : "UDim", "new", 2);
            return;
        case InstanceValue::Type::UDim2:
            lua_pushnumber(L, v.vec.x);
            lua_pushnumber(L, v.vec.y);
            lua_pushnumber(L, v.vec.z);
            lua_pushnumber(L, v.number);
            callConstructor(L, "UDim2", "new", 4);
            return;
    }
    lua_pushnil(L);
}

const char* luauTypeName(lua_State* L, int index) {
    if (toProxy(L, index) != nullptr) return "Instance";
    static thread_local std::string type;
    type = datatypeOf(L, index);
    return type.empty() ? luaL_typename(L, index) : type.c_str();
}

// Reads a value of any attribute-able type.
bool toAnyValue(lua_State* L, int index, InstanceValue& out) {
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
        case LUA_TNIL: out = InstanceValue{}; return true;
        case LUA_TBOOLEAN: out = InstanceValue::ofBool(lua_toboolean(L, index) != 0); return true;
        case LUA_TNUMBER: out = InstanceValue::ofNumber(lua_tonumber(L, index)); return true;
        case LUA_TSTRING: out = InstanceValue::ofString(lua_tostring(L, index)); return true;
        default: break;
    }
    if (Proxy* proxy = toProxy(L, index)) {
        out = InstanceValue::ofInstance(proxy->ref);
        return true;
    }
    const std::string type = datatypeOf(L, index);
    if (type == "Vector3") {
        out = InstanceValue::ofVector3(rawVec(L, index, "X", "Y", "Z"));
    } else if (type == "Color3") {
        out = InstanceValue::ofColor3(rawVec(L, index, "R", "G", "B"));
    } else if (type == "BrickColor") {
        lua_rawgetfield(L, index, "Color");
        out = InstanceValue::ofBrickColor(rawVec(L, -1, "R", "G", "B"));
        lua_pop(L, 1);
    } else if (type == "CFrame") {
        glm::mat3 m(1.0f);
        for (int i = 0; i < 9; ++i) {
            lua_rawgeti(L, index, i + 1);
            m[i % 3][i / 3] = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
        out = InstanceValue::ofCFrame(rawVec(L, index, "X", "Y", "Z"), glm::normalize(glm::quat_cast(m)));
    } else if (type == "Vector2") {
        const glm::vec3 v = rawVec(L, index, "X", "Y", "X");
        out = InstanceValue::ofVector2(v.x, v.y);
    } else if (type == "UDim") {
        out = InstanceValue::ofUDim(static_cast<float>(rawNumber(L, index, "Scale")),
                                    static_cast<float>(rawNumber(L, index, "Offset")));
    } else if (type == "UDim2") {
        lua_rawgetfield(L, index, "X");
        lua_rawgetfield(L, index, "Y");
        out = InstanceValue::ofUDim2(static_cast<float>(rawNumber(L, -2, "Scale")), static_cast<float>(rawNumber(L, -2, "Offset")),
                                     static_cast<float>(rawNumber(L, -1, "Scale")), static_cast<float>(rawNumber(L, -1, "Offset")));
        lua_pop(L, 2);
    } else if (type == "EnumItem") {
        lua_rawgetfield(L, index, "EnumType");
        const std::string enumType = luaL_tolstring(L, -1, nullptr);
        lua_pop(L, 2);
        lua_rawgetfield(L, index, "Name");
        const std::string item = lua_tostring(L, -1);
        lua_pop(L, 1);
        out = InstanceValue::ofEnum(enumType, item, static_cast<int>(rawNumber(L, index, "Value")));
    } else {
        return false;
    }
    return true;
}

// Reads a value for a property of the given type, Roblox-style coercions included.
bool toPropertyValue(lua_State* L, int index, const PropertyDef& property, InstanceValue& out) {
    index = lua_absindex(L, index);
    switch (property.type) {
        case PropertyType::Bool:
            if (!lua_isboolean(L, index)) return false;
            out = InstanceValue::ofBool(lua_toboolean(L, index) != 0);
            return true;
        case PropertyType::Number:
            if (lua_type(L, index) != LUA_TNUMBER) return false;
            out = InstanceValue::ofNumber(lua_tonumber(L, index));
            return true;
        case PropertyType::String:
            if (lua_type(L, index) != LUA_TSTRING && lua_type(L, index) != LUA_TNUMBER) return false;
            out = InstanceValue::ofString(lua_tostring(L, index));
            return true;
        case PropertyType::Instance:
            if (lua_isnil(L, index)) {
                out = InstanceValue{};
                return true;
            }
            if (toProxy(L, index) == nullptr) return false;
            out = InstanceValue::ofInstance(toProxy(L, index)->ref);
            return true;
        case PropertyType::Enum: {
            // An EnumItem, its name, or its number.
            lua_getglobal(L, "Enum");
            lua_rawgetfield(L, -1, property.enumType.c_str());
            lua_remove(L, -2);
            const int enumIndex = lua_gettop(L);
            bool ok = false;
            if (lua_type(L, index) == LUA_TSTRING) {
                lua_rawgetfield(L, enumIndex, lua_tostring(L, index));
                if (!lua_isnil(L, -1)) ok = toAnyValue(L, -1, out);
                lua_pop(L, 1);
            } else if (lua_type(L, index) == LUA_TNUMBER) {
                lua_getfield(L, enumIndex, "FromValue");
                lua_pushvalue(L, enumIndex);
                lua_pushvalue(L, index);
                lua_call(L, 2, 1);
                if (!lua_isnil(L, -1)) ok = toAnyValue(L, -1, out);
                lua_pop(L, 1);
            } else if (datatypeOf(L, index) == "EnumItem") {
                lua_rawgetfield(L, index, "EnumType");
                ok = lua_rawequal(L, -1, enumIndex) != 0 && toAnyValue(L, index, out);
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            return ok;
        }
        case PropertyType::Vector3: return datatypeOf(L, index) == "Vector3" && toAnyValue(L, index, out);
        case PropertyType::CFrame: return datatypeOf(L, index) == "CFrame" && toAnyValue(L, index, out);
        case PropertyType::Color3: return datatypeOf(L, index) == "Color3" && toAnyValue(L, index, out);
        case PropertyType::BrickColor: return datatypeOf(L, index) == "BrickColor" && toAnyValue(L, index, out);
        case PropertyType::Vector2: return datatypeOf(L, index) == "Vector2" && toAnyValue(L, index, out);
        case PropertyType::UDim: return datatypeOf(L, index) == "UDim" && toAnyValue(L, index, out);
        case PropertyType::UDim2: return datatypeOf(L, index) == "UDim2" && toAnyValue(L, index, out);
    }
    return false;
}

const char* propertyTypeName(const PropertyDef& property) {
    switch (property.type) {
        case PropertyType::Bool: return "boolean";
        case PropertyType::Number: return "number";
        case PropertyType::String: return "string";
        case PropertyType::Vector3: return "Vector3";
        case PropertyType::CFrame: return "CFrame";
        case PropertyType::Color3: return "Color3";
        case PropertyType::BrickColor: return "BrickColor";
        case PropertyType::Enum: return "EnumItem";
        case PropertyType::Instance: return "Instance";
        case PropertyType::Vector2: return "Vector2";
        case PropertyType::UDim: return "UDim";
        case PropertyType::UDim2: return "UDim2";
    }
    return "value";
}

// Members that exist in Roblox and arrive in a later bridge step.
bool isPlannedMember(const char* key) {
    static const char* const kPlanned[] = {
        "Kick", "GetMouse", "Team", "TeamColor", "LoadAnimation", "EquipTool", "UnequipTools", "Animator",
        "Play", "Stop", "Pause", "Resume", "GetPivot",
        "PivotTo", "MoveTo", "SetPrimaryPartCFrame", "GetPrimaryPartCFrame", "GetMass", "ApplyImpulse",
        "AssemblyLinearVelocity", "Velocity", "BindToRenderStep", "UnbindFromRenderStep"};
    for (const char* planned : kPlanned) {
        if (std::strcmp(key, planned) == 0) return true;
    }
    return false;
}

// --- signals -----------------------------------------------------------------

SignalHub& hubOf(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kHubKey);
    auto* hub = static_cast<SignalHub*>(lua_tolightuserdata(L, -1));
    lua_pop(L, 1);
    if (hub == nullptr) luaL_error(L, "signals are not available in this script");
    return *hub;
}

void pushSignal(lua_State* L, InstanceRef ref, const std::string& event) {
    if (event.size() >= sizeof(SignalProxy::event)) luaL_error(L, "the name \"%s\" is too long", event.c_str());
    auto* signal = static_cast<SignalProxy*>(lua_newuserdatatagged(L, sizeof(SignalProxy), kSignalTag));
    signal->ref = ref;
    std::memcpy(signal->event, event.c_str(), event.size() + 1);
    lua_getfield(L, LUA_REGISTRYINDEX, kSignalMetaKey);
    lua_setmetatable(L, -2);
}

SignalProxy* checkSignal(lua_State* L, const char* method) {
    auto* signal = static_cast<SignalProxy*>(lua_touserdatatagged(L, 1, kSignalTag));
    if (signal == nullptr) luaL_error(L, "Expected ':' not '.' calling member function %s", method);
    return signal;
}

void pushConnection(lua_State* L, uint64_t id) {
    auto* connection = static_cast<ConnectionProxy*>(lua_newuserdatatagged(L, sizeof(ConnectionProxy), kConnectionTag));
    connection->id = id;
    lua_getfield(L, LUA_REGISTRYINDEX, kConnectionMetaKey);
    lua_setmetatable(L, -2);
}

// OnServerEvent is the server's to listen to, OnClientEvent the client's.
void checkEventSide(lua_State* L, const char* event) {
    const RunContext context = runContextOf(L);
    if (context == RunContext::Client && std::strcmp(event, "OnServerEvent") == 0) {
        luaL_error(L, "OnServerEvent can only be used on the server");
    }
    if (context == RunContext::Server && std::strcmp(event, "OnClientEvent") == 0) {
        luaL_error(L, "OnClientEvent can only be used on the client");
    }
}

int connectSignal(lua_State* L, const char* method, bool once) {
    SignalProxy* signal = checkSignal(L, method);
    checkEventSide(L, signal->event);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    SignalHub& hub = hubOf(L);
    lua_pushvalue(L, 2);
    const int fnRef = lua_ref(L, -1);
    lua_pop(L, 1);
    pushConnection(L, hub.connect(lua_mainthread(L), signal->ref, signal->event, fnRef, once, lua_getthreaddata(L)));
    return 1;
}

int sConnect(lua_State* L) { return connectSignal(L, "Connect", false); }
int sOnce(lua_State* L) { return connectSignal(L, "Once", true); }

int sWait(lua_State* L) {
    SignalProxy* signal = checkSignal(L, "Wait");
    checkEventSide(L, signal->event);
    auto* scripting = static_cast<Scripting*>(lua_callbacks(L)->userdata);
    if (scripting == nullptr || !lua_isyieldable(L)) luaL_error(L, "Wait can't be used here; it must run in a script thread");
    hubOf(L).connectWait(L, signal->ref, signal->event);
    scripting->suspendCurrent(L);
    return lua_yield(L, 0);
}

int signalToString(lua_State* L) {
    const auto* signal = static_cast<SignalProxy*>(lua_touserdatatagged(L, 1, kSignalTag));
    std::string name = signal != nullptr ? signal->event : "";
    if (const size_t colon = name.find(':'); colon != std::string::npos) name = name.substr(0, colon);
    lua_pushstring(L, ("Signal " + name).c_str());
    return 1;
}

int connectionIndex(lua_State* L) {
    const auto* connection = static_cast<ConnectionProxy*>(lua_touserdatatagged(L, 1, kConnectionTag));
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "Connected") == 0) {
        lua_pushboolean(L, hubOf(L).isConnected(connection->id) ? 1 : 0);
        return 1;
    }
    if (std::strcmp(key, "Disconnect") == 0 || std::strcmp(key, "disconnect") == 0) {
        lua_pushvalue(L, lua_upvalueindex(1));
        return 1;
    }
    luaL_error(L, "%s is not a valid member of RBXScriptConnection", key);
    return 0;
}

int cDisconnect(lua_State* L) {
    const auto* connection = static_cast<ConnectionProxy*>(lua_touserdatatagged(L, 1, kConnectionTag));
    if (connection == nullptr) luaL_error(L, "Expected ':' not '.' calling member function Disconnect");
    hubOf(L).disconnect(connection->id);
    return 0;
}

// Set-only function members, kept by the SignalHub.
bool isCallback(const std::string& cls, const char* key) {
    if (instances::classIsA(cls, "RemoteFunction")) {
        return std::strcmp(key, "OnServerInvoke") == 0 || std::strcmp(key, "OnClientInvoke") == 0;
    }
    return instances::classIsA(cls, "BindableFunction") && std::strcmp(key, "OnInvoke") == 0;
}

void setCallback(lua_State* L, InstanceRef ref, const char* key) {
    const RunContext context = runContextOf(L);
    if ((context == RunContext::Client && std::strcmp(key, "OnServerInvoke") == 0) ||
        (context == RunContext::Server && std::strcmp(key, "OnClientInvoke") == 0)) {
        luaL_error(L, "%s can only be set on the %s", key, context == RunContext::Client ? "server" : "client");
    }
    SignalHub& hub = hubOf(L);
    if (lua_isnil(L, 3)) {
        hub.setCallback(lua_mainthread(L), ref, key, -1, nullptr);
        return;
    }
    if (!lua_isfunction(L, 3)) luaL_error(L, "%s must be set to a function, got %s", key, luaL_typename(L, 3));
    lua_pushvalue(L, 3);
    const int fnRef = lua_ref(L, -1);
    lua_pop(L, 1);
    hub.setCallback(lua_mainthread(L), ref, key, fnRef, lua_getthreaddata(L));
}

// A client sees ServerStorage and ServerScriptService empty, as in Roblox.
bool hiddenFromClient(lua_State* L, ECS& ecs, InstanceRef ref) {
    if (runContextOf(L) != RunContext::Client) return false;
    for (const char* name : {"ServerStorage", "ServerScriptService"}) {
        const InstanceRef service = instances::findService(ecs, name);
        if (service != kNoInstance && (ref == service || instances::isDescendantOf(ecs, ref, service))) return true;
    }
    return false;
}

std::vector<InstanceRef> visibleChildren(lua_State* L, ECS& ecs, InstanceRef ref) {
    if (hiddenFromClient(L, ecs, ref)) return {};
    return instances::children(ecs, ref);
}

std::vector<InstanceRef> visibleDescendants(lua_State* L, ECS& ecs, InstanceRef ref) {
    std::vector<InstanceRef> list = instances::descendants(ecs, ref);
    if (runContextOf(L) != RunContext::Client) return list;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](InstanceRef d) { return hiddenFromClient(L, ecs, instances::parent(ecs, d)); }),
               list.end());
    return list;
}

// --- metamethods -------------------------------------------------------------

int instanceIndex(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef ref = toProxy(L, 1)->ref;
    if (lua_type(L, 2) != LUA_TSTRING) {
        luaL_error(L, "%s is not a valid member of %s", luaL_tolstring(L, 2, nullptr),
                   instances::className(ecs, ref).c_str());
    }
    const char* key = lua_tostring(L, 2);
    const bool alive = instances::isAlive(ecs, ref);
    const std::string cls = alive ? instances::className(ecs, ref) : std::string("Instance");

    if (isCallback(cls, key)) {
        luaL_error(L, "%s is a callback member of %s; you can only set the callback value, get is not available", key,
                   cls.c_str());
    }
    if (const PropertyDef* property = instances::findProperty(cls, key)) {
        // As in Roblox, the server has no local player.
        if (property->name == "LocalPlayer" && runContextOf(L) == RunContext::Server) {
            lua_pushnil(L);
            return 1;
        }
        InstanceValue value;
        if (alive && instances::getProperty(ecs, ref, *property, value)) {
            pushValue(L, value);
        } else {
            lua_pushnil(L);
        }
        return 1;
    }

    if (instances::classHasMethod(cls, key)) {
        lua_getfield(L, LUA_REGISTRYINDEX, kMethodsKey);
        lua_rawgetfield(L, -1, key);
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 2);
    }
    if (instances::classHasEvent(cls, key)) {
        pushSignal(L, ref, key);
        return 1;
    }

    if (!alive) luaL_error(L, "%s is not a valid member of a destroyed Instance", key);

    if (std::strcmp(key, "entity") == 0) {
        const EntityId e = instances::entityOf(ecs, ref);
        if (e == kNullEntity) {
            lua_pushnil(L);
        } else {
            lua_pushnumber(L, static_cast<double>(entt::to_integral(e)));
        }
        return 1;
    }

    for (InstanceRef child : visibleChildren(L, ecs, ref)) {
        if (instances::name(ecs, child) == key) {
            pushInstance(L, child);
            return 1;
        }
    }
    if (ref == kGameInstance) {
        const ClassDef* def = instances::findClass(key);
        if (def != nullptr && def->service) {
            std::string error;
            pushInstance(L, instances::getService(ecs, key, error));
            return 1;
        }
    }

    if (isPlannedMember(key)) {
        luaL_error(L, "%s is not in Kronos yet; it is planned for the 4.3 Roblox bridge", key);
    }
    luaL_error(L, "%s is not a valid member of %s \"%s\"", key, cls.c_str(), instances::fullName(ecs, ref).c_str());
    return 0;
}

int instanceNewIndex(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef ref = toProxy(L, 1)->ref;
    const char* key = luaL_checkstring(L, 2);
    if (!instances::isAlive(ecs, ref)) {
        if (std::strcmp(key, "Parent") == 0) luaL_error(L, "The Parent property of a destroyed Instance is locked");
        luaL_error(L, "Unable to assign property %s of a destroyed Instance", key);
    }
    const std::string cls = instances::className(ecs, ref);
    if (isCallback(cls, key)) {
        setCallback(L, ref, key);
        return 0;
    }
    const PropertyDef* property = instances::findProperty(cls, key);
    if (property == nullptr) {
        if (isPlannedMember(key)) {
            luaL_error(L, "%s is not in Kronos yet; it is planned for the 4.3 Roblox bridge", key);
        }
        luaL_error(L, "%s is not a valid member of %s \"%s\"", key, cls.c_str(),
                   instances::fullName(ecs, ref).c_str());
    }
    if (property->readOnly) luaL_error(L, "Unable to assign property %s. Property is read only", key);

    InstanceValue value;
    if (!toPropertyValue(L, 3, *property, value)) {
        luaL_error(L, "Unable to assign property %s. %s expected, got %s", key, propertyTypeName(*property),
                   luauTypeName(L, 3));
    }
    if (property->name == "Parent") {
        std::string error;
        if (!instances::setParent(ecs, ref, value.ref, error)) luaL_error(L, "%s", error.c_str());
        return 0;
    }
    if ((ref == kGameInstance || ref == kWorkspaceInstance || ref == kRunServiceInstance) && property->name == "Name") {
        luaL_error(L, "Unable to rename %s", instances::name(ecs, ref).c_str());
    }
    instances::setProperty(ecs, ref, *property, value);
    return 0;
}

int instanceToString(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef ref = toProxy(L, 1)->ref;
    const std::string text = instances::isAlive(ecs, ref) ? instances::name(ecs, ref) : std::string("<destroyed>");
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// --- methods -----------------------------------------------------------------

void pushList(lua_State* L, const std::vector<InstanceRef>& refs) {
    lua_createtable(L, static_cast<int>(refs.size()), 0);
    int i = 1;
    for (InstanceRef ref : refs) {
        pushInstance(L, ref);
        lua_rawseti(L, -2, i++);
    }
}

int mGetChildren(lua_State* L) {
    pushList(L, visibleChildren(L, ecsOf(L), checkSelf(L, "GetChildren")));
    return 1;
}

int mGetDescendants(lua_State* L) {
    pushList(L, visibleDescendants(L, ecsOf(L), checkSelf(L, "GetDescendants")));
    return 1;
}

template <typename Match>
InstanceRef findChild(lua_State* L, ECS& ecs, InstanceRef ref, bool recursive, const Match& match) {
    const std::vector<InstanceRef> list = recursive ? visibleDescendants(L, ecs, ref) : visibleChildren(L, ecs, ref);
    for (InstanceRef child : list) {
        if (match(child)) return child;
    }
    return kNoInstance;
}

int mFindFirstChild(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "FindFirstChild");
    const std::string wanted = luaL_checkstring(L, 2);
    const bool recursive = lua_toboolean(L, 3) != 0;
    pushInstance(L, findChild(L, ecs, self, recursive, [&](InstanceRef c) { return instances::name(ecs, c) == wanted; }));
    return 1;
}

int mFindFirstChildOfClass(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "FindFirstChildOfClass");
    const std::string wanted = luaL_checkstring(L, 2);
    pushInstance(L, findChild(L, ecs, self, false, [&](InstanceRef c) { return instances::className(ecs, c) == wanted; }));
    return 1;
}

int mFindFirstChildWhichIsA(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "FindFirstChildWhichIsA");
    const std::string wanted = luaL_checkstring(L, 2);
    const bool recursive = lua_toboolean(L, 3) != 0;
    pushInstance(L, findChild(L, ecs, self, recursive,
                              [&](InstanceRef c) { return instances::classIsA(instances::className(ecs, c), wanted); }));
    return 1;
}

template <typename Match>
int findAncestor(lua_State* L, const char* method, const Match& match) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, method);
    const std::string wanted = luaL_checkstring(L, 2);
    int guard = 0;
    for (InstanceRef at = instances::parent(ecs, self); at != kNoInstance && guard < 1024;
         at = instances::parent(ecs, at), ++guard) {
        if (match(ecs, at, wanted)) {
            pushInstance(L, at);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

int mFindFirstAncestor(lua_State* L) {
    return findAncestor(L, "FindFirstAncestor",
                        [](ECS& ecs, InstanceRef at, const std::string& w) { return instances::name(ecs, at) == w; });
}

int mFindFirstAncestorOfClass(lua_State* L) {
    return findAncestor(L, "FindFirstAncestorOfClass", [](ECS& ecs, InstanceRef at, const std::string& w) {
        return instances::className(ecs, at) == w;
    });
}

int mFindFirstAncestorWhichIsA(lua_State* L) {
    return findAncestor(L, "FindFirstAncestorWhichIsA", [](ECS& ecs, InstanceRef at, const std::string& w) {
        return instances::classIsA(instances::className(ecs, at), w);
    });
}

int mIsA(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "IsA");
    lua_pushboolean(L, instances::classIsA(instances::className(ecs, self), luaL_checkstring(L, 2)) ? 1 : 0);
    return 1;
}

int mIsDescendantOf(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "IsDescendantOf");
    const InstanceRef other = lua_isnil(L, 2) ? kNoInstance : checkInstanceArg(L, 2, "IsDescendantOf");
    lua_pushboolean(L, other != kNoInstance && instances::isDescendantOf(ecs, self, other) ? 1 : 0);
    return 1;
}

int mIsAncestorOf(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "IsAncestorOf");
    const InstanceRef other = lua_isnil(L, 2) ? kNoInstance : checkInstanceArg(L, 2, "IsAncestorOf");
    lua_pushboolean(L, other != kNoInstance && instances::isDescendantOf(ecs, other, self) ? 1 : 0);
    return 1;
}

int mClone(lua_State* L) {
    pushInstance(L, instances::clone(ecsOf(L), checkSelf(L, "Clone")));
    return 1;
}

int mDestroy(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "Destroy");
    if (self == kGameInstance || self == kWorkspaceInstance || self == kRunServiceInstance) {
        luaL_error(L, "%s cannot be destroyed", instances::name(ecs, self).c_str());
    }
    instances::destroy(ecs, self);
    return 0;
}

int mClearAllChildren(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "ClearAllChildren");
    for (InstanceRef child : instances::children(ecs, self)) {
        if (child != kWorkspaceInstance) instances::destroy(ecs, child);
    }
    return 0;
}

int mGetFullName(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const std::string text = instances::fullName(ecs, checkSelf(L, "GetFullName"));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int mGetAttribute(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "GetAttribute");
    const InstanceValue* value = instances::attribute(ecs, self, luaL_checkstring(L, 2));
    if (value == nullptr) {
        lua_pushnil(L);
    } else {
        pushValue(L, *value);
    }
    return 1;
}

int mSetAttribute(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "SetAttribute");
    const std::string attributeName = luaL_checkstring(L, 2);
    InstanceValue value;
    if (!toAnyValue(L, 3, value) || value.type == InstanceValue::Type::Instance) {
        luaL_error(L, "SetAttribute: %s is not a supported attribute type", luauTypeName(L, 3));
    }
    instances::setAttribute(ecs, self, attributeName, value);
    return 0;
}

int mGetAttributes(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "GetAttributes");
    lua_newtable(L);
    for (const auto& [key, value] : instances::attributes(ecs, self)) {
        pushValue(L, value);
        lua_setfield(L, -2, key.c_str());
    }
    return 1;
}

int mGetService(lua_State* L) {
    ECS& ecs = ecsOf(L);
    checkSelf(L, "GetService");
    std::string error;
    const InstanceRef service = instances::getService(ecs, luaL_checkstring(L, 2), error);
    if (service == kNoInstance) luaL_error(L, "%s", error.c_str());
    pushInstance(L, service);
    return 1;
}

int mFindService(lua_State* L) {
    ECS& ecs = ecsOf(L);
    checkSelf(L, "FindService");
    pushInstance(L, instances::findService(ecs, luaL_checkstring(L, 2)));
    return 1;
}

int mGetPropertyChangedSignal(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "GetPropertyChangedSignal");
    const std::string property = luaL_checkstring(L, 2);
    const std::string cls = instances::isAlive(ecs, self) ? instances::className(ecs, self) : std::string("Instance");
    if (instances::findProperty(cls, property) == nullptr) luaL_error(L, "%s is not a valid property name.", property.c_str());
    pushSignal(L, self, "Changed:" + property);
    return 1;
}

int mGetAttributeChangedSignal(lua_State* L) {
    const InstanceRef self = checkSelf(L, "GetAttributeChangedSignal");
    pushSignal(L, self, std::string("AttributeChanged:") + luaL_checkstring(L, 2));
    return 1;
}

int mFire(lua_State* L) {
    const InstanceRef self = checkSelf(L, "Fire");
    std::vector<SignalArg> args;
    for (int i = 2; i <= lua_gettop(L); ++i) args.push_back(toSignalArg(L, i));
    hubOf(L).fire(self, "Event", std::move(args));
    return 0;
}

// --- remotes -----------------------------------------------------------------

void checkClass(lua_State* L, ECS& ecs, InstanceRef self, const char* base, const char* method) {
    const std::string cls = instances::className(ecs, self);
    if (!instances::classIsA(cls, base)) luaL_error(L, "%s is not a valid member of %s", method, cls.c_str());
}

InstanceRef playerArg(lua_State* L, ECS& ecs, int index, const char* method) {
    Proxy* proxy = toProxy(L, index);
    if (proxy == nullptr || instances::className(ecs, proxy->ref) != "Player") {
        luaL_error(L, "%s: player argument must be a Player object", method);
    }
    return proxy->ref;
}

std::vector<SignalArg> argsFrom(lua_State* L, int first, std::vector<SignalArg> args = {}) {
    for (int i = first; i <= lua_gettop(L); ++i) args.push_back(toSignalArg(L, i));
    return args;
}

SignalArg localPlayerArg(ECS& ecs) { return SignalArg::of(InstanceValue::ofInstance(players::localPlayer(ecs))); }

// This process has a client only for its local player; other players' clients
// are on other computers.
bool hasLocalClient(ECS& ecs, InstanceRef player) {
    return signals::runService(ecs).client && player == players::localPlayer(ecs);
}

int mFireServer(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "FireServer");
    checkClass(L, ecs, self, "BaseRemoteEvent", "FireServer");
    if (runContextOf(L) == RunContext::Server) luaL_error(L, "FireServer can only be called from the client");
    if (signals::runService(ecs).server) {
        hubOf(L).fire(self, "OnServerEvent", argsFrom(L, 2, {localPlayerArg(ecs)}));
    } else if (std::string error; !remotenet::fireServer(ecs, self, argsFrom(L, 2), error)) {
        luaL_error(L, "%s", error.c_str());
    }
    return 0;
}

int mFireClient(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "FireClient");
    checkClass(L, ecs, self, "BaseRemoteEvent", "FireClient");
    if (runContextOf(L) == RunContext::Client) luaL_error(L, "FireClient can only be called from the server");
    const InstanceRef player = playerArg(L, ecs, 2, "FireClient");
    if (hasLocalClient(ecs, player)) {
        hubOf(L).fire(self, "OnClientEvent", argsFrom(L, 3));
    } else if (remotenet::netIdFor(ecs, player) != 0) {
        if (std::string error; !remotenet::fireClient(ecs, self, player, argsFrom(L, 3), error)) {
            luaL_error(L, "%s", error.c_str());
        }
    }
    return 0;
}

int mFireAllClients(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "FireAllClients");
    checkClass(L, ecs, self, "BaseRemoteEvent", "FireAllClients");
    if (runContextOf(L) == RunContext::Client) luaL_error(L, "FireAllClients can only be called from the server");
    if (signals::runService(ecs).client) hubOf(L).fire(self, "OnClientEvent", argsFrom(L, 2));
    if (remotenet::isServer(ecs)) {
        if (std::string error; !remotenet::fireAllClients(ecs, self, argsFrom(L, 2), error)) {
            luaL_error(L, "%s", error.c_str());
        }
    }
    return 0;
}

// invokeStart(self, method, ...) -> id; the Luau wrappers below wait for it.
int invokeStart(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const std::string method = luaL_checkstring(L, 2);
    const InstanceRef self = checkSelf(L, method.c_str());
    const RunContext context = runContextOf(L);
    std::vector<SignalArg> args;
    std::string callback;
    if (method == "Invoke") {
        checkClass(L, ecs, self, "BindableFunction", "Invoke");
        args = argsFrom(L, 3);
        callback = "OnInvoke";
    } else if (method == "InvokeServer") {
        checkClass(L, ecs, self, "RemoteFunction", "InvokeServer");
        if (context == RunContext::Server) luaL_error(L, "InvokeServer can only be called from the client");
        if (!signals::runService(ecs).server) {
            std::string error;
            const uint64_t id = remotenet::invokeServer(ecs, self, argsFrom(L, 3), error);
            if (id == 0) luaL_error(L, "%s", error.c_str());
            lua_pushnumber(L, static_cast<double>(id));
            return 1;
        }
        args = argsFrom(L, 3, {localPlayerArg(ecs)});
        callback = "OnServerInvoke";
    } else {
        checkClass(L, ecs, self, "RemoteFunction", "InvokeClient");
        if (context == RunContext::Client) luaL_error(L, "InvokeClient can only be called from the server");
        const InstanceRef player = playerArg(L, ecs, 3, "InvokeClient");
        if (!hasLocalClient(ecs, player) && remotenet::netIdFor(ecs, player) != 0) {
            std::string error;
            const uint64_t id = remotenet::invokeClient(ecs, self, player, argsFrom(L, 4), error);
            if (id == 0) luaL_error(L, "%s", error.c_str());
            lua_pushnumber(L, static_cast<double>(id));
            return 1;
        }
        if (!hasLocalClient(ecs, player)) {
            luaL_error(L, "InvokeClient: %s's client isn't running in this process", instances::name(ecs, player).c_str());
        }
        args = argsFrom(L, 4);
        callback = "OnClientInvoke";
    }
    lua_pushnumber(L, static_cast<double>(hubOf(L).invoke(self, callback, std::move(args))));
    return 1;
}

uint64_t invokeId(lua_State* L) { return static_cast<uint64_t>(luaL_checknumber(L, 1)); }

int invokeIsDone(lua_State* L) {
    lua_pushboolean(L, hubOf(L).invokeDone(invokeId(L)) ? 1 : 0);
    return 1;
}

int invokeTake(lua_State* L) {
    SignalHub::InvokeResult result = hubOf(L).takeInvoke(invokeId(L));
    if (!result.ok) {
        lua_pushlstring(L, result.error.data(), result.error.size());
        lua_error(L);
    }
    lua_checkstack(L, static_cast<int>(result.values.size()) + 4);
    for (const SignalArg& value : result.values) pushSignalArg(L, value);
    return static_cast<int>(result.values.size());
}

// Called by the trampoline in the callback's VM with (id, pcall results...).
int invokeFinish(lua_State* L) {
    const uint64_t id = invokeId(L);
    const bool ok = lua_toboolean(L, 2) != 0;
    std::vector<SignalArg> values;
    std::string error;
    if (ok) {
        values = argsFrom(L, 3);
    } else {
        error = luaL_tolstring(L, 3, nullptr);
    }
    hubOf(L).finishInvoke(id, ok, std::move(values), std::move(error));
    return 0;
}

const char* const kInvokeSource = R"LUAU(
local start, isDone, take, wait = ...
local function result(id)
	while not isDone(id) do wait() end
	return take(id)
end
return function(self, ...) return result(start(self, "InvokeServer", ...)) end,
	function(self, ...) return result(start(self, "InvokeClient", ...)) end,
	function(self, ...) return result(start(self, "Invoke", ...)) end
)LUAU";

const char* const kTrampolineSource = R"LUAU(
local finish = ...
return function(callback, id, ...)
	finish(id, pcall(callback, ...))
end
)LUAU";

template <bool (*Read)(const RunServiceState&)>
int runServiceQuery(lua_State* L) {
    checkSelf(L, "RunService method");
    RunServiceState state = signals::runService(ecsOf(L));
    // Scripts in a Server or Client VM see only their own side.
    if (const RunContext context = runContextOf(L); context != RunContext::Own) {
        state.server = context == RunContext::Server;
        state.client = context == RunContext::Client;
    }
    lua_pushboolean(L, Read(state) ? 1 : 0);
    return 1;
}
bool readServer(const RunServiceState& s) { return s.server; }
bool readClient(const RunServiceState& s) { return s.client; }
bool readStudio(const RunServiceState& s) { return s.studio; }
bool readRunning(const RunServiceState& s) { return s.running; }
bool readRunMode(const RunServiceState& s) { return s.studio && s.running; }
bool readEdit(const RunServiceState& s) { return s.studio && !s.running; }

// --- DataStoreService ---------------------------------------------------------

struct DataStoreHandle {
    std::string name;
    std::string scope;
};

struct DataStoreHandles {
    std::map<std::pair<std::string, std::string>, InstanceRef> byName;
    std::unordered_map<InstanceRef, DataStoreHandle> byRef;
};

void requireServer(lua_State* L) {
    const RunContext context = runContextOf(L);
    const bool server = context == RunContext::Own ? signals::runService(ecsOf(L)).server : context == RunContext::Server;
    if (!server) luaL_error(L, "DataStore can't be accessed from client");
}

int mGetDataStore(lua_State* L) {
    ECS& ecs = ecsOf(L);
    checkSelf(L, "GetDataStore");
    requireServer(L);
    const std::string name = luaL_checkstring(L, 2);
    const std::string scope = luaL_optstring(L, 3, "global");
    if (const std::string error = datastore::checkName(name); !error.empty()) luaL_error(L, "%s", error.c_str());
    auto& handles = ecs.raw().ctx().emplace<DataStoreHandles>();
    const auto found = handles.byName.find({name, scope});
    InstanceRef ref = found != handles.byName.end() ? found->second : kNoInstance;
    if (ref == kNoInstance || !instances::isAlive(ecs, ref)) {
        ref = instances::createUnchecked(ecs, "DataStore");
        instances::setName(ecs, ref, name);
        handles.byName[{name, scope}] = ref;
        handles.byRef[ref] = DataStoreHandle{name, scope};
    }
    pushInstance(L, ref);
    return 1;
}

int mGetGlobalDataStore(lua_State* L) {
    lua_settop(L, 1);
    lua_pushstring(L, "GlobalDataStore");
    return mGetDataStore(L);
}

// The store behind `self`, after the shared checks; the key is argument 2.
const DataStoreHandle& checkStoreCall(lua_State* L, const char* method, std::string& key) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, method);
    requireServer(L);
    auto& handles = ecs.raw().ctx().emplace<DataStoreHandles>();
    const auto found = handles.byRef.find(self);
    if (found == handles.byRef.end()) luaL_error(L, "%s is not a valid member of %s", method, instances::className(ecs, self).c_str());
    key = luaL_checkstring(L, 2);
    if (const std::string error = datastore::checkKey(key); !error.empty()) luaL_error(L, "%s", error.c_str());
    return found->second;
}

std::string writeKey(const DataStoreHandle& store, const std::string& key) {
    return store.name + '\n' + store.scope + '\n' + key;
}

void storeValue(lua_State* L, const DataStoreHandle& store, const std::string& key, const nlohmann::json& value) {
    std::string text;
    std::string error;
    if (!datastore::encode(value, text, error)) luaL_error(L, "%s", error.c_str());
    datastore::set(ecsOf(L), store.name, store.scope, key, value);
}

nlohmann::json checkValue(lua_State* L, int index) {
    nlohmann::json value;
    std::string error;
    if (!datastore::toJson(L, index, value, error)) luaL_error(L, "%s", error.c_str());
    return value;
}

int mGetAsync(lua_State* L) {
    std::string key;
    const DataStoreHandle& store = checkStoreCall(L, "GetAsync", key);
    datastore::charge(ecsOf(L), datastore::Request::Get, {});
    nlohmann::json value;
    if (datastore::get(ecsOf(L), store.name, store.scope, key, value)) datastore::pushJson(L, value);
    else lua_pushnil(L);
    return 1;
}

int mSetAsync(lua_State* L) {
    std::string key;
    const DataStoreHandle& store = checkStoreCall(L, "SetAsync", key);
    const nlohmann::json value = checkValue(L, 3);
    if (value.is_null()) luaL_error(L, "Argument 2 missing or nil");
    datastore::charge(ecsOf(L), datastore::Request::Set, writeKey(store, key));
    storeValue(L, store, key, value);
    return 0;
}

int mUpdateAsync(lua_State* L) {
    std::string key;
    const DataStoreHandle& store = checkStoreCall(L, "UpdateAsync", key);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    ECS& ecs = ecsOf(L);
    datastore::charge(ecs, datastore::Request::Get, {});
    nlohmann::json old;
    const bool had = datastore::get(ecs, store.name, store.scope, key, old);
    lua_pushvalue(L, 3);
    if (had) datastore::pushJson(L, old);
    else lua_pushnil(L);
    // The transform can't yield, like in Roblox.
    if (lua_pcall(L, 1, 1, 0) != 0) lua_error(L);
    if (lua_isnil(L, -1)) return 1; // cancelled
    const nlohmann::json value = checkValue(L, -1);
    datastore::charge(ecs, datastore::Request::Set, writeKey(store, key));
    storeValue(L, store, key, value);
    datastore::pushJson(L, value);
    return 1;
}

int mIncrementAsync(lua_State* L) {
    std::string key;
    const DataStoreHandle& store = checkStoreCall(L, "IncrementAsync", key);
    const double delta = luaL_optnumber(L, 3, 1.0);
    if (delta != std::floor(delta)) luaL_error(L, "IncrementAsync delta must be an integer");
    ECS& ecs = ecsOf(L);
    datastore::charge(ecs, datastore::Request::Set, writeKey(store, key));
    nlohmann::json old;
    double current = 0.0;
    if (datastore::get(ecs, store.name, store.scope, key, old) && !old.is_null()) {
        if (!old.is_number()) luaL_error(L, "IncrementAsync can only increment a number, but the stored value is not one");
        current = old.get<double>();
    }
    const nlohmann::json value = std::floor(current) + delta;
    storeValue(L, store, key, value);
    lua_pushnumber(L, value.get<double>());
    return 1;
}

int mRemoveAsync(lua_State* L) {
    std::string key;
    const DataStoreHandle& store = checkStoreCall(L, "RemoveAsync", key);
    ECS& ecs = ecsOf(L);
    datastore::charge(ecs, datastore::Request::Set, writeKey(store, key));
    nlohmann::json old;
    if (datastore::remove(ecs, store.name, store.scope, key, old)) datastore::pushJson(L, old);
    else lua_pushnil(L);
    return 1;
}

// --- TweenService, Debris, Sound, CollectionService, Lighting -------------------

// An EnumItem's name (EasingStyle.Quad -> "Quad"), or `fallback` for nil.
std::string enumItemName(lua_State* L, int index, const char* enumType, const char* fallback) {
    if (lua_isnoneornil(L, index)) return fallback;
    if (datatypeOf(L, index) == "EnumItem") {
        lua_rawgetfield(L, index, "EnumType");
        const std::string type = luaL_tolstring(L, -1, nullptr);
        lua_pop(L, 2);
        if (type == enumType) {
            lua_rawgetfield(L, index, "Name");
            std::string item = lua_tostring(L, -1);
            lua_pop(L, 1);
            return item;
        }
    }
    luaL_error(L, "Unable to cast %s to Enum.%s", luauTypeName(L, index), enumType);
    return fallback;
}

int mCreate(lua_State* L) {
    ECS& ecs = ecsOf(L);
    checkSelf(L, "Create");
    const InstanceRef target = checkInstanceArg(L, 2, "Create");
    if (datatypeOf(L, 3) != "TweenInfo") luaL_error(L, "Unable to cast %s to TweenInfo", luauTypeName(L, 3));
    luaL_checktype(L, 4, LUA_TTABLE);
    services::TweenSettings settings;
    settings.time = rawNumber(L, 3, "Time");
    lua_rawgetfield(L, 3, "EasingStyle");
    settings.style = enumItemName(L, lua_gettop(L), "EasingStyle", "Quad");
    lua_rawgetfield(L, 3, "EasingDirection");
    settings.direction = enumItemName(L, lua_gettop(L), "EasingDirection", "Out");
    lua_pop(L, 2);
    settings.repeatCount = static_cast<int>(rawNumber(L, 3, "RepeatCount"));
    lua_rawgetfield(L, 3, "Reverses");
    settings.reverses = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    settings.delay = rawNumber(L, 3, "DelayTime");

    const std::string cls = instances::className(ecs, target);
    std::vector<std::pair<const PropertyDef*, InstanceValue>> goals;
    lua_pushnil(L);
    while (lua_next(L, 4) != 0) {
        const char* key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "?";
        const PropertyDef* property = instances::findProperty(cls, key);
        if (property == nullptr || property->readOnly || property->name == "Parent" || property->name == "Name") {
            luaL_error(L, "TweenService:Create no property named '%s' for object '%s'", key,
                       instances::name(ecs, target).c_str());
        }
        InstanceValue goal;
        if (!services::canTween(property->type) || !toPropertyValue(L, -1, *property, goal)) {
            luaL_error(L, "TweenService:Create property named '%s' cannot be tweened due to type mismatch "
                          "(property is a '%s', but given type is '%s')",
                       key, propertyTypeName(*property), luauTypeName(L, -1));
        }
        goals.emplace_back(property, std::move(goal));
        lua_pop(L, 1);
    }
    pushInstance(L, services::createTween(ecs, target, settings, std::move(goals)));
    return 1;
}

int mGetValue(lua_State* L) {
    checkSelf(L, "GetValue");
    const double alpha = luaL_checknumber(L, 2);
    const std::string style = enumItemName(L, 3, "EasingStyle", "Linear");
    const std::string direction = enumItemName(L, 4, "EasingDirection", "In");
    lua_pushnumber(L, services::ease(style, direction, alpha));
    return 1;
}

// Play, Pause, Stop and Resume are shared by Tweens and Sounds.
template <void (*TweenAction)(ECS&, InstanceRef), void (*SoundAction)(ECS&, InstanceRef)>
int playbackMethod(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "Play");
    const std::string cls = instances::className(ecs, self);
    if constexpr (TweenAction != nullptr) {
        if (instances::classIsA(cls, "TweenBase")) TweenAction(ecs, self);
    }
    if constexpr (SoundAction != nullptr) {
        if (cls == "Sound") SoundAction(ecs, self);
    }
    return 0;
}

int mAddItem(lua_State* L) {
    checkSelf(L, "AddItem");
    const InstanceRef item = checkInstanceArg(L, 2, "AddItem");
    services::addDebris(ecsOf(L), item, luaL_optnumber(L, 3, 10.0));
    return 0;
}

// Instance:AddTag(tag) or CollectionService:AddTag(instance, tag).
InstanceRef tagTarget(lua_State* L, const char* method, int& tagIndex) {
    const InstanceRef self = checkSelf(L, method);
    if (Proxy* other = toProxy(L, 2); other != nullptr && instances::className(ecsOf(L), self) == "CollectionService") {
        tagIndex = 3;
        return other->ref;
    }
    tagIndex = 2;
    return self;
}

int mAddTag(lua_State* L) {
    int tagIndex = 2;
    const InstanceRef target = tagTarget(L, "AddTag", tagIndex);
    instances::addTag(ecsOf(L), target, luaL_checkstring(L, tagIndex));
    return 0;
}

int mRemoveTag(lua_State* L) {
    int tagIndex = 2;
    const InstanceRef target = tagTarget(L, "RemoveTag", tagIndex);
    instances::removeTag(ecsOf(L), target, luaL_checkstring(L, tagIndex));
    return 0;
}

int mHasTag(lua_State* L) {
    int tagIndex = 2;
    const InstanceRef target = tagTarget(L, "HasTag", tagIndex);
    lua_pushboolean(L, instances::hasTag(ecsOf(L), target, luaL_checkstring(L, tagIndex)) ? 1 : 0);
    return 1;
}

void pushStrings(lua_State* L, const std::vector<std::string>& strings) {
    lua_createtable(L, static_cast<int>(strings.size()), 0);
    for (size_t i = 0; i < strings.size(); ++i) {
        lua_pushlstring(L, strings[i].data(), strings[i].size());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
}

int mGetTags(lua_State* L) {
    const InstanceRef self = checkSelf(L, "GetTags");
    Proxy* other = toProxy(L, 2);
    pushStrings(L, instances::tags(ecsOf(L), other != nullptr ? other->ref : self));
    return 1;
}

int mGetTagged(lua_State* L) {
    checkSelf(L, "GetTagged");
    pushList(L, instances::tagged(ecsOf(L), luaL_checkstring(L, 2)));
    return 1;
}

int mGetAllTags(lua_State* L) {
    ECS& ecs = ecsOf(L);
    checkSelf(L, "GetAllTags");
    std::vector<std::string> all;
    for (auto [e, info] : ecs.raw().view<InstanceInfo>().each()) {
        if (info.tags.empty() || instances::isDetached(ecs, e)) continue;
        for (const std::string& tag : info.tags) {
            if (std::find(all.begin(), all.end(), tag) == all.end()) all.push_back(tag);
        }
    }
    std::sort(all.begin(), all.end());
    pushStrings(L, all);
    return 1;
}

template <bool Added>
int tagSignal(lua_State* L) {
    const InstanceRef self = checkSelf(L, Added ? "GetInstanceAddedSignal" : "GetInstanceRemovedSignal");
    pushSignal(L, self, signals::tagEventName(luaL_checkstring(L, 2), Added));
    return 1;
}

int mGetMinutesAfterMidnight(lua_State* L) {
    ECS& ecs = ecsOf(L);
    InstanceValue clock;
    (void)instances::getProperty(ecs, checkSelf(L, "GetMinutesAfterMidnight"),
                                 *instances::findProperty("Lighting", "ClockTime"), clock);
    lua_pushnumber(L, clock.number * 60.0);
    return 1;
}

int mSetMinutesAfterMidnight(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "SetMinutesAfterMidnight");
    instances::setProperty(ecs, self, *instances::findProperty("Lighting", "ClockTime"),
                           InstanceValue::ofNumber(luaL_checknumber(L, 2) / 60.0));
    return 0;
}

// --- Players and Humanoid ----------------------------------------------------

int mGetPlayers(lua_State* L) {
    checkSelf(L, "GetPlayers");
    pushList(L, players::list(ecsOf(L)));
    return 1;
}

int mGetPlayerFromCharacter(lua_State* L) {
    checkSelf(L, "GetPlayerFromCharacter");
    Proxy* character = toProxy(L, 2);
    pushInstance(L, character != nullptr ? players::playerFromCharacter(ecsOf(L), character->ref) : kNoInstance);
    return 1;
}

int mGetPlayerByUserId(lua_State* L) {
    checkSelf(L, "GetPlayerByUserId");
    pushInstance(L, players::playerByUserId(ecsOf(L), static_cast<int64_t>(luaL_checknumber(L, 2))));
    return 1;
}

int mLoadCharacter(lua_State* L) {
    players::requestLoadCharacter(ecsOf(L), checkSelf(L, "LoadCharacter"));
    return 0;
}

EntityId checkHumanoid(lua_State* L, const char* method) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, method);
    const EntityId e = instances::entityOf(ecs, self);
    if (e == kNullEntity) luaL_error(L, "%s can't be called on a destroyed Humanoid", method);
    return e;
}

glm::vec3 checkVector3(lua_State* L, int index, const char* method) {
    InstanceValue value;
    if (!toAnyValue(L, index, value) || value.type != InstanceValue::Type::Vector3) {
        luaL_error(L, "%s expects a Vector3 as argument %d", method, index - 1);
    }
    return value.vec;
}

bool underAny(ECS& ecs, InstanceRef ref, const std::vector<InstanceRef>& roots) {
    for (int depth = 0; depth < 256 && ref != kNoInstance; ++depth) {
        if (std::find(roots.begin(), roots.end(), ref) != roots.end()) return true;
        ref = instances::parent(ecs, ref);
    }
    return false;
}

void pushRaycastResultMeta(lua_State* L) {
    if (luaL_newmetatable(L, "kronos.RaycastResult")) {
        lua_pushstring(L, "RaycastResult");
        lua_setfield(L, -2, "__type");
        lua_setreadonly(L, -1, true);
    }
}

// workspace:Raycast(origin, direction, params?)
int mRaycast(lua_State* L) {
    ECS& ecs = ecsOf(L);
    checkSelf(L, "Raycast");
    const glm::vec3 origin = checkVector3(L, 2, "Raycast");
    const glm::vec3 direction = checkVector3(L, 3, "Raycast");
    std::vector<InstanceRef> filter;
    bool include = false;
    bool respectCanCollide = false;
    if (!lua_isnoneornil(L, 4)) {
        if (datatypeOf(L, 4) != "RaycastParams") luaL_error(L, "Raycast expects RaycastParams as argument 3");
        lua_rawgetfield(L, 4, "FilterDescendantsInstances");
        if (lua_istable(L, -1)) {
            for (int i = 1;; ++i) {
                lua_rawgeti(L, -1, i);
                if (lua_isnil(L, -1)) {
                    lua_pop(L, 1);
                    break;
                }
                if (Proxy* proxy = toProxy(L, -1)) filter.push_back(proxy->ref);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        lua_rawgetfield(L, 4, "FilterType");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "Name");
            const char* name = lua_tostring(L, -1);
            include = name != nullptr && (std::strcmp(name, "Include") == 0 || std::strcmp(name, "Whitelist") == 0);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        lua_rawgetfield(L, 4, "RespectCanCollide");
        respectCanCollide = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }

    Physics* physics = partbodies::physicsOf(ecs);
    const float length = glm::length(direction);
    if (physics == nullptr || length < 1e-6f) {
        lua_pushnil(L);
        return 1;
    }
    const std::function<bool(EntityId)> accept = [&](EntityId e) {
        if (!ecs.raw().valid(e) || ecs.tryGetComponent<PlayerAvatarPart>(e) != nullptr) return false;
        const InstanceRef ref = instances::refOf(ecs, e);
        if (ref == kNoInstance || !instances::isInWorld(ecs, e)) return false;
        if (respectCanCollide && !instances::canCollide(ecs, e)) return false;
        return underAny(ecs, ref, filter) == include;
    };
    const Physics::RaycastHit hit = physics->raycast(origin, direction, length, accept);
    const InstanceRef ref = hit.hit ? instances::refOf(ecs, hit.entity) : kNoInstance;
    if (ref == kNoInstance) {
        lua_pushnil(L);
        return 1;
    }

    lua_createtable(L, 0, 5);
    pushInstance(L, ref);
    lua_setfield(L, -2, "Instance");
    pushValue(L, InstanceValue::ofVector3(hit.point));
    lua_setfield(L, -2, "Position");
    pushValue(L, InstanceValue::ofVector3(glm::normalize(hit.normal)));
    lua_setfield(L, -2, "Normal");
    lua_pushnumber(L, hit.distance);
    lua_setfield(L, -2, "Distance");
    InstanceValue material;
    if (const PropertyDef* def = instances::findProperty(instances::className(ecs, ref), "Material");
        def != nullptr && instances::getProperty(ecs, ref, *def, material)) {
        pushValue(L, material);
    } else {
        lua_getglobal(L, "Enum");
        lua_getfield(L, -1, "Material");
        lua_getfield(L, -1, "Plastic");
        lua_remove(L, -2);
        lua_remove(L, -2);
    }
    lua_setfield(L, -2, "Material");
    pushRaycastResultMeta(L);
    lua_setmetatable(L, -2);
    lua_setreadonly(L, -1, true);
    return 1;
}

int mTakeDamage(lua_State* L) {
    const EntityId humanoid = checkHumanoid(L, "TakeDamage");
    players::takeDamage(ecsOf(L), humanoid, luaL_checknumber(L, 2));
    return 0;
}

int mMoveTo(lua_State* L) {
    const EntityId humanoid = checkHumanoid(L, "MoveTo");
    const glm::vec3 target = checkVector3(L, 2, "MoveTo");
    Proxy* part = toProxy(L, 3);
    players::moveTo(ecsOf(L), humanoid, target, part != nullptr ? part->ref : kNoInstance);
    return 0;
}

int mMove(lua_State* L) {
    const EntityId humanoid = checkHumanoid(L, "Move");
    players::move(ecsOf(L), humanoid, checkVector3(L, 2, "Move"), lua_toboolean(L, 3) != 0);
    return 0;
}

int mGetState(lua_State* L) {
    const EntityId humanoid = checkHumanoid(L, "GetState");
    lua_getglobal(L, "Enum");
    lua_rawgetfield(L, -1, "HumanoidStateType");
    lua_rawgetfield(L, -1, players::state(ecsOf(L), humanoid).c_str());
    return 1;
}

int mChangeState(lua_State* L) {
    const EntityId humanoid = checkHumanoid(L, "ChangeState");
    InstanceValue value;
    if (!toAnyValue(L, 2, value) || value.type != InstanceValue::Type::Enum || value.enumType != "HumanoidStateType") {
        luaL_error(L, "ChangeState expects an Enum.HumanoidStateType item");
    }
    players::changeState(ecsOf(L), humanoid, value.text);
    return 0;
}

int instanceNew(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const std::string cls = luaL_checkstring(L, 1);
    std::string error;
    const InstanceRef created = instances::create(ecs, cls, error);
    if (created == kNoInstance) luaL_error(L, "%s", error.c_str());
    if (!lua_isnoneornil(L, 2)) {
        const InstanceRef parent = checkInstanceArg(L, 2, "Instance.new");
        if (!instances::setParent(ecs, created, parent, error)) luaL_error(L, "%s", error.c_str());
    }
    pushInstance(L, created);
    return 1;
}

const char* const kWaitForChildSource = R"LUAU(
local findFirstChild, getFullName, wait = ...
return function(self, name, timeout)
	local child = findFirstChild(self, name)
	if child ~= nil then return child end
	local waited, warned = 0, false
	repeat
		waited += (wait() or 0.016)
		child = findFirstChild(self, name)
		if child ~= nil then return child end
		if timeout == nil and not warned and waited >= 5 then
			warned = true
			print("Infinite yield possible on '" .. getFullName(self) .. ":WaitForChild(\"" .. tostring(name) .. "\")'")
		end
	until timeout ~= nil and waited >= timeout
	return nil
end
)LUAU";

const std::string& waitForChildBytecode() {
    static const std::string compiled = Luau::compile(kWaitForChildSource);
    return compiled;
}

// require(ModuleScript), Roblox's way: one result per module per VM, shared by
// every script in that VM; a module that yields makes other requirers wait.
const char* const kRequireSource = R"LUAU(
local stringRequire, loadModule, isInstance, wait = ...
local cache = {}
return function(target)
	if not isInstance(target) then
		if stringRequire == nil then error("Attempted to call require with invalid argument(s).", 2) end
		return stringRequire(target)
	end
	local entry = cache[target]
	if entry == nil then
		local body, compileError = loadModule(target)
		entry = {loading = true, thread = coroutine.running()}
		cache[target] = entry
		if body == nil then
			entry.loading, entry.failed = false, true
			error(compileError .. "\nRequested module experienced an error while loading", 2)
		end
		local results = table.pack(pcall(body))
		entry.loading = false
		if not results[1] then
			entry.failed = true
			error(tostring(results[2]) .. "\nRequested module experienced an error while loading", 2)
		end
		if results.n ~= 2 then
			entry.failed = true
			error("Module code did not return exactly one value", 2)
		end
		entry.value = results[2]
		return entry.value
	end
	if entry.loading then
		if entry.thread == coroutine.running() then error("Requested module was required recursively", 2) end
		while entry.loading do wait() end
	end
	if entry.failed then error("Requested module experienced an error while loading", 2) end
	return entry.value
end
)LUAU";

const std::string& invokeBytecode() {
    static const std::string compiled = Luau::compile(kInvokeSource);
    return compiled;
}

const std::string& trampolineBytecode() {
    static const std::string compiled = Luau::compile(kTrampolineSource);
    return compiled;
}

// Pushes the compiled helper chunk's function, or reports why it failed.
bool loadHelper(lua_State* L, const char* name, const std::string& bytecode) {
    if (luau_load(L, name, bytecode.data(), bytecode.size(), 0) == 0) return true;
    std::fprintf(stderr, "ScriptInstanceApi: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
}

const std::string& requireBytecode() {
    static const std::string compiled = Luau::compile(kRequireSource);
    return compiled;
}

int isInstanceValue(lua_State* L) {
    lua_pushboolean(L, toProxy(L, 1) != nullptr);
    return 1;
}

// The module's body as a function with its own environment (`script` is the
// module), or nil plus the compile error.
int loadModule(lua_State* L) {
    ECS& ecs = ecsOf(L);
    Proxy* proxy = toProxy(L, 1);
    const EntityId e = proxy != nullptr ? instances::entityOf(ecs, proxy->ref) : kNullEntity;
    const auto* module = e != kNullEntity && instances::className(ecs, proxy->ref) == "ModuleScript"
                             ? ecs.tryGetComponent<Script>(e)
                             : nullptr;
    if (module == nullptr) luaL_error(L, "Attempted to call require with invalid argument(s).");
    const std::string chunkName = "=" + instances::fullName(ecs, proxy->ref);
    const std::string bytecode = compileScriptSource(module->source);

    lua_newtable(L);
    lua_newtable(L);
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_setfield(L, -2, "__index");
    lua_setreadonly(L, -1, true);
    lua_setmetatable(L, -2);
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "script");
    lua_setsafeenv(L, -1, true);
    // Loaded on a thread whose globals are the module's: Luau resolves some
    // globals at load time through the loading thread, which would otherwise
    // be the requiring script's.
    lua_State* loader = lua_newthread(L);
    lua_pushvalue(L, -2);
    lua_xmove(L, loader, 1);
    lua_replace(loader, LUA_GLOBALSINDEX);
    const int status = luau_load(loader, chunkName.c_str(), bytecode.data(), bytecode.size(), 0);
    lua_xmove(loader, L, 1);
    if (status != 0) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

} // namespace

void registerInstanceApi(lua_State* L, ECS& ecs) {
    lua_pushlightuserdata(L, &ecs);
    lua_setfield(L, LUA_REGISTRYINDEX, kEcsKey);

    lua_getfield(L, LUA_REGISTRYINDEX, kGuardKey);
    const bool hasGuard = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!hasGuard) {
        std::shared_ptr<SignalHub> hub = signals::hubFor(ecs);
        lua_pushlightuserdata(L, hub.get());
        lua_setfield(L, LUA_REGISTRYINDEX, kHubKey);
        if (auto* scripting = static_cast<Scripting*>(lua_callbacks(L)->userdata)) scripting->addDeferredWork(hub);
        auto** guard = static_cast<VmGuard**>(lua_newuserdatadtor(L, sizeof(VmGuard*), [](void* data) {
            VmGuard* g = *static_cast<VmGuard**>(data);
            g->hub->forgetVm(g->vm);
            delete g;
        }));
        *guard = new VmGuard{std::move(hub), lua_mainthread(L)};
        lua_setfield(L, LUA_REGISTRYINDEX, kGuardKey);
    }

    lua_newtable(L);
    lua_pushstring(L, "RBXScriptSignal");
    lua_setfield(L, -2, "__type");
    lua_newtable(L);
    lua_pushcfunction(L, &sConnect, "Connect");
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "Connect");
    lua_setfield(L, -2, "ConnectParallel");
    lua_pushcfunction(L, &sOnce, "Once");
    lua_setfield(L, -2, "Once");
    lua_pushcfunction(L, &sWait, "Wait");
    lua_setfield(L, -2, "Wait");
    lua_setreadonly(L, -1, true);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, &signalToString, "Signal.__tostring");
    lua_setfield(L, -2, "__tostring");
    lua_pushstring(L, "The metatable is locked");
    lua_setfield(L, -2, "__metatable");
    lua_setreadonly(L, -1, true);
    lua_setfield(L, LUA_REGISTRYINDEX, kSignalMetaKey);

    lua_newtable(L);
    lua_pushstring(L, "RBXScriptConnection");
    lua_setfield(L, -2, "__type");
    lua_pushcfunction(L, &cDisconnect, "Disconnect");
    lua_pushcclosure(L, &connectionIndex, "Connection.__index", 1);
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, "Connection");
    lua_setfield(L, -2, "__name");
    lua_pushstring(L, "The metatable is locked");
    lua_setfield(L, -2, "__metatable");
    lua_setreadonly(L, -1, true);
    lua_setfield(L, LUA_REGISTRYINDEX, kConnectionMetaKey);

    auto pushClosure = [&](lua_CFunction fn, const char* debugName) {
        lua_pushlightuserdata(L, &ecs);
        lua_pushcclosure(L, fn, debugName, 1);
    };

    lua_newtable(L);
    lua_pushstring(L, "Instance");
    lua_setfield(L, -2, "__type");
    pushClosure(&instanceIndex, "Instance.__index");
    lua_setfield(L, -2, "__index");
    pushClosure(&instanceNewIndex, "Instance.__newindex");
    lua_setfield(L, -2, "__newindex");
    pushClosure(&instanceToString, "Instance.__tostring");
    lua_setfield(L, -2, "__tostring");
    lua_pushstring(L, "The metatable is locked");
    lua_setfield(L, -2, "__metatable");
    lua_setreadonly(L, -1, true);
    lua_setfield(L, LUA_REGISTRYINDEX, kMetaKey);

    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, kCacheKey);

    struct Method {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Method kMethods[] = {
        {"GetChildren", &mGetChildren},
        {"GetDescendants", &mGetDescendants},
        {"FindFirstChild", &mFindFirstChild},
        {"FindFirstChildOfClass", &mFindFirstChildOfClass},
        {"FindFirstChildWhichIsA", &mFindFirstChildWhichIsA},
        {"FindFirstAncestor", &mFindFirstAncestor},
        {"FindFirstAncestorOfClass", &mFindFirstAncestorOfClass},
        {"FindFirstAncestorWhichIsA", &mFindFirstAncestorWhichIsA},
        {"IsA", &mIsA},
        {"IsDescendantOf", &mIsDescendantOf},
        {"IsAncestorOf", &mIsAncestorOf},
        {"Clone", &mClone},
        {"Destroy", &mDestroy},
        {"ClearAllChildren", &mClearAllChildren},
        {"GetFullName", &mGetFullName},
        {"GetAttribute", &mGetAttribute},
        {"SetAttribute", &mSetAttribute},
        {"GetAttributes", &mGetAttributes},
        {"GetService", &mGetService},
        {"FindService", &mFindService},
        {"GetPropertyChangedSignal", &mGetPropertyChangedSignal},
        {"GetAttributeChangedSignal", &mGetAttributeChangedSignal},
        {"Fire", &mFire},
        {"IsServer", &runServiceQuery<&readServer>},
        {"IsClient", &runServiceQuery<&readClient>},
        {"IsStudio", &runServiceQuery<&readStudio>},
        {"IsRunning", &runServiceQuery<&readRunning>},
        {"IsRunMode", &runServiceQuery<&readRunMode>},
        {"IsEdit", &runServiceQuery<&readEdit>},
        {"GetPlayers", &mGetPlayers},
        {"GetPlayerFromCharacter", &mGetPlayerFromCharacter},
        {"GetPlayerByUserId", &mGetPlayerByUserId},
        {"LoadCharacter", &mLoadCharacter},
        {"TakeDamage", &mTakeDamage},
        {"MoveTo", &mMoveTo},
        {"Move", &mMove},
        {"GetState", &mGetState},
        {"ChangeState", &mChangeState},
        {"FireServer", &mFireServer},
        {"FireClient", &mFireClient},
        {"FireAllClients", &mFireAllClients},
        {"GetDataStore", &mGetDataStore},
        {"GetGlobalDataStore", &mGetGlobalDataStore},
        {"GetAsync", &mGetAsync},
        {"SetAsync", &mSetAsync},
        {"UpdateAsync", &mUpdateAsync},
        {"IncrementAsync", &mIncrementAsync},
        {"RemoveAsync", &mRemoveAsync},
        {"Create", &mCreate},
        {"GetValue", &mGetValue},
        {"Play", &playbackMethod<&services::playTween, &services::playSound>},
        {"Pause", &playbackMethod<&services::pauseTween, &services::pauseSound>},
        {"Cancel", &playbackMethod<&services::cancelTween, nullptr>},
        {"Stop", &playbackMethod<nullptr, &services::stopSound>},
        {"Resume", &playbackMethod<nullptr, &services::resumeSound>},
        {"AddItem", &mAddItem},
        {"Raycast", &mRaycast},
        {"AddTag", &mAddTag},
        {"RemoveTag", &mRemoveTag},
        {"HasTag", &mHasTag},
        {"GetTags", &mGetTags},
        {"GetTagged", &mGetTagged},
        {"GetAllTags", &mGetAllTags},
        {"GetInstanceAddedSignal", &tagSignal<true>},
        {"GetInstanceRemovedSignal", &tagSignal<false>},
        {"GetMinutesAfterMidnight", &mGetMinutesAfterMidnight},
        {"SetMinutesAfterMidnight", &mSetMinutesAfterMidnight},
    };
    lua_newtable(L);
    for (const Method& method : kMethods) {
        pushClosure(method.fn, method.name);
        lua_setfield(L, -2, method.name);
    }
    const std::string& code = waitForChildBytecode();
    if (luau_load(L, "=WaitForChild", code.data(), code.size(), 0) == 0) {
        lua_getfield(L, -2, "FindFirstChild");
        lua_getfield(L, -3, "GetFullName");
        lua_getglobal(L, "task");
        lua_getfield(L, -1, "wait");
        lua_remove(L, -2);
        if (lua_pcall(L, 3, 1, 0) == 0) {
            lua_setfield(L, -2, "WaitForChild");
        } else {
            std::fprintf(stderr, "ScriptInstanceApi: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        std::fprintf(stderr, "ScriptInstanceApi: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    if (loadHelper(L, "=Invoke", invokeBytecode())) {
        pushClosure(&invokeStart, "invokeStart");
        lua_pushcfunction(L, &invokeIsDone, "invokeIsDone");
        lua_pushcfunction(L, &invokeTake, "invokeTake");
        lua_getglobal(L, "task");
        lua_getfield(L, -1, "wait");
        lua_remove(L, -2);
        if (lua_pcall(L, 4, 3, 0) == 0) {
            lua_setfield(L, -4, "Invoke");
            lua_setfield(L, -3, "InvokeClient");
            lua_setfield(L, -2, "InvokeServer");
        } else {
            std::fprintf(stderr, "ScriptInstanceApi: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_setreadonly(L, -1, true);
    lua_setfield(L, LUA_REGISTRYINDEX, kMethodsKey);

    if (loadHelper(L, "=InvokeCallback", trampolineBytecode())) {
        lua_pushcfunction(L, &invokeFinish, "invokeFinish");
        if (lua_pcall(L, 1, 1, 0) == 0) {
            lua_pushinteger(L, lua_ref(L, -1));
            lua_setfield(L, LUA_REGISTRYINDEX, kInvokeTrampolineKey);
            lua_pop(L, 1);
        } else {
            std::fprintf(stderr, "ScriptInstanceApi: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    pushInstance(L, kGameInstance);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "game");
    lua_setglobal(L, "Game");
    pushInstance(L, kWorkspaceInstance);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "workspace");
    lua_setglobal(L, "Workspace");

    lua_getfield(L, LUA_REGISTRYINDEX, kStringRequireKey);
    const bool wrapped = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!wrapped) {
        lua_getglobal(L, "require");
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kStringRequireKey);
    } else {
        lua_getfield(L, LUA_REGISTRYINDEX, kStringRequireKey);
    }
    const std::string& requireCode = requireBytecode();
    if (luau_load(L, "=require", requireCode.data(), requireCode.size(), 0) == 0) {
        lua_insert(L, -2);
        lua_pushlightuserdata(L, &ecs);
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_pushcclosure(L, &loadModule, "loadModule", 2);
        lua_pushcfunction(L, &isInstanceValue, "isInstance");
        lua_getglobal(L, "task");
        lua_getfield(L, -1, "wait");
        lua_remove(L, -2);
        if (lua_pcall(L, 4, 1, 0) == 0) {
            lua_setglobal(L, "require");
        } else {
            std::fprintf(stderr, "ScriptInstanceApi: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        std::fprintf(stderr, "ScriptInstanceApi: %s\n", lua_tostring(L, -1));
        lua_pop(L, 2);
    }

    lua_newtable(L);
    pushClosure(&instanceNew, "Instance.new");
    lua_setfield(L, -2, "new");
    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Instance");
}

namespace {

SignalArg toSignalArgAt(lua_State* L, int index, int depth) {
    index = lua_absindex(L, index);
    SignalArg arg;
    if (toAnyValue(L, index, arg.value)) return arg;
    arg.value = InstanceValue{};
    if (lua_type(L, index) != LUA_TTABLE || depth >= 32) return arg; // functions and the like arrive as nil
    arg.isTable = true;
    lua_checkstack(L, 4);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        SignalArg key = toSignalArgAt(L, -2, depth + 1);
        SignalArg value = toSignalArgAt(L, -1, depth + 1);
        lua_pop(L, 1);
        if (key.isTable || key.value.type == InstanceValue::Type::Nil) continue;
        arg.keys.push_back(std::move(key));
        arg.values.push_back(std::move(value));
    }
    return arg;
}

} // namespace

SignalArg toSignalArg(lua_State* L, int index) { return toSignalArgAt(L, index, 0); }

void pushSignalArg(lua_State* L, const SignalArg& arg) {
    if (!arg.isTable) {
        pushValue(L, arg.value);
        return;
    }
    lua_checkstack(L, 4);
    lua_createtable(L, 0, static_cast<int>(arg.keys.size()));
    for (size_t i = 0; i < arg.keys.size(); ++i) {
        pushSignalArg(L, arg.keys[i]);
        pushSignalArg(L, arg.values[i]);
        lua_rawset(L, -3);
    }
}

bool pushScriptInstance(lua_State* L, uint32_t entity) {
    lua_getfield(L, LUA_REGISTRYINDEX, kEcsKey);
    auto* ecs = static_cast<ECS*>(lua_tolightuserdata(L, -1));
    lua_pop(L, 1);
    if (ecs == nullptr) return false;
    const InstanceRef ref = instances::refOf(*ecs, static_cast<EntityId>(entity));
    if (ref == kNoInstance) return false;
    pushInstance(L, ref);
    return true;
}

} // namespace engine::core
