#include "core/ScriptInstanceApi.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"

namespace engine::core {
namespace {

constexpr int kInstanceTag = 60;
constexpr const char* kMetaKey = "kronos.Instance.mt";
constexpr const char* kCacheKey = "kronos.Instance.cache";
constexpr const char* kMethodsKey = "kronos.Instance.methods";
constexpr const char* kEcsKey = "kronos.Instance.ecs";

struct Proxy {
    InstanceRef ref;
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
    }
    return "value";
}

// Members that exist in Roblox and arrive in a later bridge step.
bool isPlannedMember(const char* key) {
    static const char* const kPlanned[] = {
        "Touched", "TouchEnded", "Changed", "ChildAdded", "ChildRemoved", "DescendantAdded", "DescendantRemoving",
        "AncestryChanged", "AttributeChanged", "Destroying", "GetPropertyChangedSignal",
        "GetAttributeChangedSignal", "PlayerAdded", "PlayerRemoving", "CharacterAdded", "CharacterRemoving",
        "LocalPlayer", "GetPlayers", "GetPlayerFromCharacter", "Event", "Fire", "Invoke", "OnInvoke",
        "OnServerEvent", "OnClientEvent", "OnServerInvoke", "OnClientInvoke", "FireServer", "FireClient",
        "FireAllClients", "InvokeServer", "InvokeClient", "Play", "Stop", "Pause", "Resume", "GetPivot",
        "PivotTo", "MoveTo", "SetPrimaryPartCFrame", "GetPrimaryPartCFrame", "GetMass", "ApplyImpulse",
        "AssemblyLinearVelocity", "Velocity", "SoundId", "Volume", "Looped", "Playing", "MouseButton1Click",
        "MouseButton1Down", "MouseButton1Up", "Activated", "MouseEnter", "MouseLeave", "Heartbeat", "Stepped",
        "RenderStepped", "Text", "Visible"};
    for (const char* planned : kPlanned) {
        if (std::strcmp(key, planned) == 0) return true;
    }
    return false;
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

    if (const PropertyDef* property = instances::findProperty(cls, key)) {
        InstanceValue value;
        if (alive && instances::getProperty(ecs, ref, *property, value)) {
            pushValue(L, value);
        } else {
            lua_pushnil(L);
        }
        return 1;
    }

    lua_getfield(L, LUA_REGISTRYINDEX, kMethodsKey);
    lua_rawgetfield(L, -1, key);
    if (!lua_isnil(L, -1)) {
        const bool dataModelOnly = std::strcmp(key, "GetService") == 0 || std::strcmp(key, "FindService") == 0;
        if (!dataModelOnly || ref == kGameInstance) return 1;
    }
    lua_pop(L, 2);

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

    for (InstanceRef child : instances::children(ecs, ref)) {
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
    if ((ref == kGameInstance || ref == kWorkspaceInstance) && property->name == "Name") {
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
    pushList(L, instances::children(ecsOf(L), checkSelf(L, "GetChildren")));
    return 1;
}

int mGetDescendants(lua_State* L) {
    pushList(L, instances::descendants(ecsOf(L), checkSelf(L, "GetDescendants")));
    return 1;
}

template <typename Match>
InstanceRef findChild(ECS& ecs, InstanceRef ref, bool recursive, const Match& match) {
    const std::vector<InstanceRef> list = recursive ? instances::descendants(ecs, ref) : instances::children(ecs, ref);
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
    pushInstance(L, findChild(ecs, self, recursive, [&](InstanceRef c) { return instances::name(ecs, c) == wanted; }));
    return 1;
}

int mFindFirstChildOfClass(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "FindFirstChildOfClass");
    const std::string wanted = luaL_checkstring(L, 2);
    pushInstance(L, findChild(ecs, self, false, [&](InstanceRef c) { return instances::className(ecs, c) == wanted; }));
    return 1;
}

int mFindFirstChildWhichIsA(lua_State* L) {
    ECS& ecs = ecsOf(L);
    const InstanceRef self = checkSelf(L, "FindFirstChildWhichIsA");
    const std::string wanted = luaL_checkstring(L, 2);
    const bool recursive = lua_toboolean(L, 3) != 0;
    pushInstance(L, findChild(ecs, self, recursive,
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
    if (self == kGameInstance || self == kWorkspaceInstance) {
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

} // namespace

void registerInstanceApi(lua_State* L, ECS& ecs) {
    lua_pushlightuserdata(L, &ecs);
    lua_setfield(L, LUA_REGISTRYINDEX, kEcsKey);

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
    lua_setreadonly(L, -1, true);
    lua_setfield(L, LUA_REGISTRYINDEX, kMethodsKey);

    pushInstance(L, kGameInstance);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "game");
    lua_setglobal(L, "Game");
    pushInstance(L, kWorkspaceInstance);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "workspace");
    lua_setglobal(L, "Workspace");

    lua_newtable(L);
    pushClosure(&instanceNew, "Instance.new");
    lua_setfield(L, -2, "new");
    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Instance");
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
