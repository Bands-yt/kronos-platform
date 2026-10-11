#include "core/RollbackScript.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <lua.h>
#include <lualib.h>
#include <Luau/Ast.h>
#include <Luau/Compiler.h>
#include <Luau/Parser.h>

#include "core/Components.hpp"
#include "core/DeterministicMath.hpp"

namespace engine::core {

namespace {

constexpr int kMaxDepth = 32;
constexpr size_t kMaxStateBytes = 1u << 20;
constexpr size_t kMemoryLimit = 64u << 20;
constexpr size_t kHeaderBytes = 16; // source hash + random state

// The C library's versions of these can differ in the last bit between systems, and the
// compiler would otherwise call them directly as builtins.
const char* const kDisabledBuiltins[] = {"math.sin",  "math.cos",  "math.tan",    "math.asin", "math.acos",
                                         "math.atan", "math.atan2", "math.exp",   "math.log",  "math.log10",
                                         "math.pow",  "math.sinh", "math.cosh",   "math.tanh", "math.random",
                                         "math.randomseed", nullptr};

enum Tag : uint8_t { False = 1, True, Number, String, Vector, Table };

uint64_t fnv1a(const std::string& text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) hash = (hash ^ c) * 1099511628211ull;
    return hash;
}

uint64_t nextRandom(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

template <typename T>
void put(std::vector<uint8_t>& out, T value) {
    const auto* raw = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), raw, raw + sizeof(T));
}

template <typename T>
bool get(const uint8_t* data, size_t size, size_t& at, T& value) {
    if (size - at < sizeof(T)) return false;
    std::memcpy(&value, data + at, sizeof(T));
    at += sizeof(T);
    return true;
}

struct Key {
    bool isString = false;
    double number = 0.0;
    std::string text;
    bool operator<(const Key& other) const {
        if (isString != other.isString) return !isString;
        return isString ? text < other.text : number < other.number;
    }
};

bool encodeValue(lua_State* L, int index, std::vector<uint8_t>& out, std::string& error, int depth) {
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
        case LUA_TBOOLEAN:
            out.push_back(lua_toboolean(L, index) ? True : False);
            return true;
        case LUA_TNUMBER:
            out.push_back(Number);
            put(out, static_cast<double>(lua_tonumber(L, index)));
            return true;
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(L, index, &length);
            out.push_back(String);
            put(out, static_cast<uint32_t>(length));
            out.insert(out.end(), text, text + length);
            return true;
        }
        case LUA_TVECTOR: {
            const float* v = lua_tovector(L, index);
            out.push_back(Vector);
            for (int i = 0; i < 3; ++i) put(out, v[i]);
            return true;
        }
        case LUA_TTABLE:
            break;
        default:
            error = std::string("state can't hold a ") + luaL_typename(L, index);
            return false;
    }
    if (depth >= kMaxDepth) {
        error = "state is nested too deeply (or a table contains itself)";
        return false;
    }
    if (lua_getmetatable(L, index)) {
        lua_pop(L, 1);
        error = "state can't hold tables with metatables";
        return false;
    }
    std::vector<Key> keys;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        lua_pop(L, 1);
        Key key;
        if (lua_type(L, -1) == LUA_TNUMBER) {
            key.number = lua_tonumber(L, -1);
        } else if (lua_type(L, -1) == LUA_TSTRING) {
            size_t length = 0;
            const char* text = lua_tolstring(L, -1, &length);
            key.isString = true;
            key.text.assign(text, length);
        } else {
            lua_pop(L, 1);
            error = std::string("state keys must be numbers or strings, not ") + luaL_typename(L, -1);
            return false;
        }
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    out.push_back(Table);
    put(out, static_cast<uint32_t>(keys.size()));
    for (const Key& key : keys) {
        if (key.isString) {
            lua_pushlstring(L, key.text.data(), key.text.size());
        } else {
            lua_pushnumber(L, key.number);
        }
        if (!encodeValue(L, -1, out, error, depth + 1)) {
            lua_pop(L, 1);
            return false;
        }
        lua_rawget(L, index);
        const bool ok = encodeValue(L, -1, out, error, depth + 1);
        lua_pop(L, 1);
        if (!ok) return false;
        if (out.size() > kMaxStateBytes) {
            error = "state is larger than 1 MB";
            return false;
        }
    }
    return true;
}

bool decodeValue(lua_State* L, const uint8_t* data, size_t size, size_t& at, int depth) {
    uint8_t tag = 0;
    if (!get(data, size, at, tag)) return false;
    switch (tag) {
        case False:
        case True:
            lua_pushboolean(L, tag == True);
            return true;
        case Number: {
            double value = 0.0;
            if (!get(data, size, at, value)) return false;
            lua_pushnumber(L, value);
            return true;
        }
        case String: {
            uint32_t length = 0;
            if (!get(data, size, at, length) || size - at < length) return false;
            lua_pushlstring(L, reinterpret_cast<const char*>(data + at), length);
            at += length;
            return true;
        }
        case Vector: {
            float v[3];
            for (float& component : v) {
                if (!get(data, size, at, component)) return false;
            }
            lua_pushvector(L, v[0], v[1], v[2]);
            return true;
        }
        case Table: {
            uint32_t count = 0;
            if (depth >= kMaxDepth || !get(data, size, at, count) || count > size - at) return false;
            lua_checkstack(L, 4);
            lua_createtable(L, 0, static_cast<int>(count));
            for (uint32_t i = 0; i < count; ++i) {
                if (!decodeValue(L, data, size, at, depth + 1)) {
                    lua_pop(L, 1);
                    return false;
                }
                if (lua_type(L, -1) != LUA_TNUMBER && lua_type(L, -1) != LUA_TSTRING) {
                    lua_pop(L, 2);
                    return false;
                }
                if (!decodeValue(L, data, size, at, depth + 1)) {
                    lua_pop(L, 2);
                    return false;
                }
                lua_rawset(L, -3);
            }
            return true;
        }
        default:
            return false;
    }
}

void* limitedAlloc(void* ud, void* ptr, size_t oldSize, size_t newSize) {
    auto* used = static_cast<size_t*>(ud);
    if (newSize == 0) {
        std::free(ptr);
        *used -= oldSize;
        return nullptr;
    }
    if (newSize > oldSize && *used + (newSize - oldSize) > kMemoryLimit) return nullptr;
    void* result = std::realloc(ptr, newSize);
    if (result) *used = *used - oldSize + newSize;
    return result;
}

// Assigning to a top-level local from inside a function would keep state outside `state`.
struct UpvalueWriteFinder : Luau::AstVisitor {
    std::string error;
    void check(Luau::AstExpr* target) {
        auto* local = target->as<Luau::AstExprLocal>();
        if (!error.empty() || local == nullptr || !local->upvalue || local->local->functionDepth != 0) return;
        error = "line " + std::to_string(target->location.begin.line + 1) + ": '" + local->local->name.value +
                "' is a top-level local changed inside a function; keep match data in `state` so it rolls back";
    }
    bool visit(Luau::AstStatAssign* node) override {
        for (Luau::AstExpr* var : node->vars) check(var);
        return true;
    }
    bool visit(Luau::AstStatCompoundAssign* node) override {
        check(node->var);
        return true;
    }
};

// Freezes every table reachable from the value at `index`, through tables and function upvalues.
void deepFreeze(lua_State* L, int index, int depth) {
    index = lua_absindex(L, index);
    if (depth > 64 || !lua_checkstack(L, 4)) return;
    if (lua_istable(L, index)) {
        if (lua_getreadonly(L, index)) return;
        lua_setreadonly(L, index, true);
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            deepFreeze(L, -1, depth + 1);
            lua_pop(L, 1);
            deepFreeze(L, -1, depth + 1);
        }
        if (lua_getmetatable(L, index)) {
            deepFreeze(L, -1, depth + 1);
            lua_pop(L, 1);
        }
    } else if (lua_isLfunction(L, index)) {
        for (int n = 1; lua_getupvalue(L, index, n) != nullptr; ++n) {
            deepFreeze(L, -1, depth + 1);
            lua_pop(L, 1);
        }
    }
}

EntityId toEntity(lua_State* L, int index) {
    const double value = luaL_checknumber(L, index);
    if (!(value >= 0.0 && value < 4294967295.0)) luaL_error(L, "not an entity id");
    return static_cast<EntityId>(static_cast<uint32_t>(value));
}

void pushVec3(lua_State* L, const glm::vec3& v) { lua_pushvector(L, v.x, v.y, v.z); }

glm::vec3 checkVec3(lua_State* L, int index) {
    const float* v = luaL_checkvector(L, index);
    return {v[0], v[1], v[2]};
}

} // namespace

namespace rollback_state {
bool encode(lua_State* L, int index, std::vector<uint8_t>& out, std::string& error) {
    return encodeValue(L, index, out, error, 0);
}
bool decode(lua_State* L, const uint8_t* data, size_t size, size_t& at) { return decodeValue(L, data, size, at, 0); }
} // namespace rollback_state

RollbackScript::~RollbackScript() {
    if (L_) {
        void* ud = nullptr;
        lua_getallocf(L_, &ud);
        lua_close(L_);
        delete static_cast<size_t*>(ud);
    }
}

RollbackScript& RollbackScript::self(lua_State* L) { return *static_cast<RollbackScript*>(lua_callbacks(L)->userdata); }

void RollbackScript::interrupt(lua_State* L, int gc) {
    if (gc >= 0) return;
    RollbackScript& script = self(L);
    if (script.budgetLeft_ == 0) return;
    if (--script.budgetLeft_ == 0) luaL_error(L, "match script ran too long in one frame (endless loop?)");
}

bool RollbackScript::load(const std::string& chunkName, const std::string& source, std::string& error) {
    if (L_) {
        error = "a match script is already loaded";
        return false;
    }
    {
        Luau::Allocator allocator;
        Luau::AstNameTable names(allocator);
        Luau::ParseOptions options;
        Luau::ParseResult parsed = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);
        if (!parsed.errors.empty()) {
            const auto& first = parsed.errors.front();
            error = "line " + std::to_string(first.getLocation().begin.line + 1) + ": " + first.getMessage();
            return false;
        }
        UpvalueWriteFinder finder;
        parsed.root->visit(&finder);
        if (!finder.error.empty()) {
            error = finder.error;
            return false;
        }
    }

    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 1;
    options.disabledBuiltins = kDisabledBuiltins;
    const std::string bytecode = Luau::compile(source, options);

    auto* used = new size_t(0);
    L_ = lua_newstate(limitedAlloc, used);
    if (!L_) {
        delete used;
        error = "out of memory";
        return false;
    }
    lua_callbacks(L_)->userdata = this;
    luaL_openlibs(L_);
    for (const char* name : {"os", "debug", "getfenv", "setfenv", "loadstring", "newproxy", "collectgarbage", "gcinfo"}) {
        lua_pushnil(L_);
        lua_setglobal(L_, name);
    }

    lua_getglobal(L_, "math");
    const auto setMath = [&](const char* name, lua_CFunction fn) {
        lua_pushcfunction(L_, fn, name);
        lua_setfield(L_, -2, name);
    };
    setMath("sin", [](lua_State* L) {
        lua_pushnumber(L, det::sin(static_cast<float>(luaL_checknumber(L, 1))));
        return 1;
    });
    setMath("cos", [](lua_State* L) {
        lua_pushnumber(L, det::cos(static_cast<float>(luaL_checknumber(L, 1))));
        return 1;
    });
    setMath("tan", [](lua_State* L) {
        const float x = static_cast<float>(luaL_checknumber(L, 1));
        lua_pushnumber(L, det::sin(x) / det::cos(x));
        return 1;
    });
    setMath("atan2", [](lua_State* L) {
        lua_pushnumber(L, det::atan2(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))));
        return 1;
    });
    setMath("atan", [](lua_State* L) {
        const float y = static_cast<float>(luaL_checknumber(L, 1));
        const float x = static_cast<float>(luaL_optnumber(L, 2, 1.0));
        lua_pushnumber(L, det::atan2(y, x));
        return 1;
    });
    setMath("asin", [](lua_State* L) {
        const float x = std::clamp(static_cast<float>(luaL_checknumber(L, 1)), -1.0f, 1.0f);
        lua_pushnumber(L, det::atan2(x, std::sqrt(1.0f - x * x)));
        return 1;
    });
    setMath("acos", [](lua_State* L) {
        const float x = std::clamp(static_cast<float>(luaL_checknumber(L, 1)), -1.0f, 1.0f);
        lua_pushnumber(L, det::atan2(std::sqrt(1.0f - x * x), x));
        return 1;
    });
    for (const char* name : {"exp", "log", "log10", "pow", "sinh", "cosh", "tanh"}) {
        lua_pushnil(L_);
        lua_setfield(L_, -2, name);
    }
    setMath("random", &RollbackScript::mathRandom);
    setMath("randomseed", &RollbackScript::mathRandomSeed);
    lua_pop(L_, 1);

    // ctx functions live in a frozen table that each frame's ctx reads through __index.
    lua_newtable(L_);
    const luaL_Reg functions[] = {{"input", &RollbackScript::ctxInput},
                                  {"player", &RollbackScript::ctxPlayer},
                                  {"find", &RollbackScript::ctxFind},
                                  {"position", &RollbackScript::ctxPosition},
                                  {"velocity", &RollbackScript::ctxVelocity},
                                  {"setPosition", &RollbackScript::ctxSetPosition},
                                  {"setVelocity", &RollbackScript::ctxSetVelocity},
                                  {"impulse", &RollbackScript::ctxImpulse},
                                  {"moveKinematic", &RollbackScript::ctxMoveKinematic},
                                  {"contacts", &RollbackScript::ctxContacts},
                                  {nullptr, nullptr}};
    for (const luaL_Reg* fn = functions; fn->name; ++fn) {
        lua_pushcfunction(L_, fn->func, fn->name);
        lua_setfield(L_, -2, fn->name);
    }
    lua_setreadonly(L_, -1, true);
    lua_newtable(L_);
    lua_pushvalue(L_, -2);
    lua_setfield(L_, -2, "__index");
    lua_setreadonly(L_, -1, true);
    contextRef_ = lua_ref(L_, -1);
    lua_pop(L_, 2);

    luaL_sandbox(L_);
    thread_ = lua_newthread(L_);
    threadRef_ = lua_ref(L_, -1);
    lua_pop(L_, 1);
    luaL_sandboxthread(thread_);
    lua_callbacks(L_)->interrupt = &RollbackScript::interrupt;

    const std::string name = "=" + chunkName;
    if (luau_load(thread_, name.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
        error = lua_tostring(thread_, -1);
        lua_settop(thread_, 0);
        return false;
    }
    if (!call(0, 0, error)) return false;

    lua_getglobal(thread_, "step");
    if (!lua_isfunction(thread_, -1)) {
        lua_settop(thread_, 0);
        error = "a match script needs a global function step(state, ctx)";
        return false;
    }
    stepRef_ = lua_ref(thread_, -1);
    lua_pop(thread_, 1);
    lua_getglobal(thread_, "init");
    if (lua_isfunction(thread_, -1)) initRef_ = lua_ref(thread_, -1);
    lua_pop(thread_, 1);

    // Anything outside `state` is read-only from here on, so it can't drift between players.
    lua_pushvalue(thread_, LUA_GLOBALSINDEX);
    deepFreeze(thread_, -1, 0);
    lua_pop(thread_, 1);
    sourceHash_ = fnv1a(source);
    return true;
}

bool RollbackScript::call(int args, int results, std::string& error) {
    budgetLeft_ = kStepBudget;
    const int status = lua_pcall(thread_, args, results, 0);
    budgetLeft_ = 0;
    if (status == LUA_OK) return true;
    error = lua_isstring(thread_, -1) ? lua_tostring(thread_, -1) : "error";
    lua_pop(thread_, 1);
    return false;
}

bool RollbackScript::initialState(uint32_t players, std::vector<uint8_t>& out, std::string& error) {
    if (!L_) {
        error = "no match script loaded";
        return false;
    }
    const int top = lua_gettop(thread_);
    if (initRef_ >= 0) {
        lua_getref(thread_, initRef_);
        lua_pushnumber(thread_, players);
        if (!call(1, 1, error)) {
            lua_settop(thread_, top);
            return false;
        }
        if (lua_isnil(thread_, -1)) {
            lua_pop(thread_, 1);
            lua_newtable(thread_);
        } else if (!lua_istable(thread_, -1)) {
            lua_settop(thread_, top);
            error = "init must return a table";
            return false;
        }
    } else {
        lua_newtable(thread_);
    }
    out.clear();
    put(out, sourceHash_);
    put(out, uint64_t{0x5EED0000ull + players});
    const bool ok = rollback_state::encode(thread_, -1, out, error);
    lua_settop(thread_, top);
    return ok;
}

void RollbackScript::fail(const std::string& message) {
    lastError_ = message;
    ++errorCount_;
    // Resimulated frames repeat errors; report each one once.
    if (reported_.size() < 16 && std::find(reported_.begin(), reported_.end(), message) == reported_.end()) {
        reported_.push_back(message);
        std::fprintf(stderr, "RollbackScript: %s\n", message.c_str());
    }
}

void RollbackScript::pushContext() {
    lua_createtable(thread_, 0, 3);
    lua_pushnumber(thread_, frame_->frame);
    lua_setfield(thread_, -2, "frame");
    lua_pushnumber(thread_, frame_->dt);
    lua_setfield(thread_, -2, "dt");
    lua_pushnumber(thread_, frame_->playerCount);
    lua_setfield(thread_, -2, "players");
    lua_getref(thread_, contextRef_);
    lua_setmetatable(thread_, -2);
    lua_setreadonly(thread_, -1, true);
}

void RollbackScript::step(RollbackFrameContext& frame, size_t offset, const std::vector<EntityId>& playerBodies) {
    if (!L_) return;
    std::vector<uint8_t>& bytes = frame.gameState;
    uint64_t hash = 0;
    size_t at = offset;
    if (!get(bytes.data(), bytes.size(), at, hash) || !get(bytes.data(), bytes.size(), at, rng_) || hash != sourceHash_) {
        fail("the match state doesn't belong to this script");
        return;
    }
    const int top = lua_gettop(thread_);
    lua_getref(thread_, stepRef_);
    if (!rollback_state::decode(thread_, bytes.data(), bytes.size(), at) || !lua_istable(thread_, -1)) {
        lua_settop(thread_, top);
        fail("the match state is damaged");
        return;
    }
    lua_pushvalue(thread_, -1);
    lua_insert(thread_, top + 1); // keep the state below the call
    frame_ = &frame;
    playerBodies_ = &playerBodies;
    pushContext();
    std::string error;
    const bool ok = call(2, 0, error);
    frame_ = nullptr;
    playerBodies_ = nullptr;
    if (!ok) {
        lua_settop(thread_, top);
        fail(error);
        return;
    }
    std::vector<uint8_t> encoded;
    put(encoded, sourceHash_);
    put(encoded, rng_);
    if (!rollback_state::encode(thread_, top + 1, encoded, error)) {
        lua_settop(thread_, top);
        fail(error);
        return;
    }
    lua_settop(thread_, top);
    bytes.resize(offset);
    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
}

int RollbackScript::mathRandom(lua_State* L) {
    RollbackScript& script = self(L);
    const uint64_t bits = nextRandom(script.rng_);
    const int args = lua_gettop(L);
    if (args == 0) {
        lua_pushnumber(L, static_cast<double>(bits >> 11) * 0x1.0p-53);
        return 1;
    }
    const double low = args >= 2 ? luaL_checknumber(L, 1) : 1.0;
    const double high = args >= 2 ? luaL_checknumber(L, 2) : luaL_checknumber(L, 1);
    const int64_t a = static_cast<int64_t>(std::floor(low));
    const int64_t b = static_cast<int64_t>(std::floor(high));
    luaL_argcheck(L, a <= b, args >= 2 ? 2 : 1, "interval is empty");
    const uint64_t span = static_cast<uint64_t>(b - a) + 1;
    lua_pushnumber(L, static_cast<double>(a + static_cast<int64_t>(span == 0 ? bits : bits % span)));
    return 1;
}

int RollbackScript::mathRandomSeed(lua_State* L) {
    self(L).rng_ = static_cast<uint64_t>(static_cast<int64_t>(luaL_checknumber(L, 1)));
    return 0;
}

RollbackFrameContext& RollbackScript::frameIn(lua_State* L) {
    RollbackFrameContext* frame = self(L).frame_;
    if (!frame) luaL_error(L, "ctx can only be used inside step");
    return *frame;
}

namespace {
EntityId checkBody(lua_State* L, RollbackFrameContext& frame, int index) {
    const EntityId entity = toEntity(L, index);
    if (!frame.ecs.raw().valid(entity) || !frame.ecs.tryGetComponent<RigidBody>(entity)) {
        luaL_error(L, "entity %u has no physics body", static_cast<unsigned>(entt::to_integral(entity)));
    }
    return entity;
}
} // namespace

int RollbackScript::ctxInput(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    const int player = luaL_checkinteger(L, 1);
    luaL_argcheck(L, player >= 1 && static_cast<uint32_t>(player) <= frame.playerCount, 1, "no such player");
    const RollbackInput& input = frame.inputs[player - 1];
    lua_createtable(L, 0, 9);
    const glm::vec2 move = input.move();
    lua_pushnumber(L, move.x);
    lua_setfield(L, -2, "moveX");
    lua_pushnumber(L, move.y);
    lua_setfield(L, -2, "moveZ");
    lua_pushnumber(L, input.yawRadians());
    lua_setfield(L, -2, "yaw");
    lua_pushnumber(L, input.pitchRadians());
    lua_setfield(L, -2, "pitch");
    const std::pair<const char*, RollbackInput::Button> buttons[] = {{"jump", RollbackInput::Jump},
                                                                     {"primary", RollbackInput::Primary},
                                                                     {"secondary", RollbackInput::Secondary},
                                                                     {"sprint", RollbackInput::Sprint},
                                                                     {"crouch", RollbackInput::Crouch}};
    for (const auto& [name, button] : buttons) {
        lua_pushboolean(L, input.held(button));
        lua_setfield(L, -2, name);
    }
    return 1;
}

int RollbackScript::ctxPlayer(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    const int player = luaL_checkinteger(L, 1);
    const auto* bodies = self(L).playerBodies_;
    if (player < 1 || static_cast<uint32_t>(player) > frame.playerCount || !bodies ||
        static_cast<size_t>(player) > bodies->size()) {
        lua_pushnil(L);
    } else {
        lua_pushnumber(L, entt::to_integral((*bodies)[player - 1]));
    }
    return 1;
}

int RollbackScript::ctxFind(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    const std::string name = luaL_checkstring(L, 1);
    EntityId found = kNullEntity;
    for (auto [entity, entityName] : frame.ecs.raw().view<Name>().each()) {
        if (entityName.value == name && (found == kNullEntity || entt::to_integral(entity) < entt::to_integral(found))) {
            found = entity;
        }
    }
    if (found == kNullEntity) {
        lua_pushnil(L);
    } else {
        lua_pushnumber(L, entt::to_integral(found));
    }
    return 1;
}

int RollbackScript::ctxPosition(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    const EntityId entity = toEntity(L, 1);
    const auto* transform = frame.ecs.raw().valid(entity) ? frame.ecs.tryGetComponent<Transform>(entity) : nullptr;
    if (!transform) luaL_error(L, "entity %u has no position", static_cast<unsigned>(entt::to_integral(entity)));
    pushVec3(L, transform->position);
    return 1;
}

int RollbackScript::ctxVelocity(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    pushVec3(L, frame.physics.getLinearVelocity(checkBody(L, frame, 1), frame.ecs));
    return 1;
}

int RollbackScript::ctxSetPosition(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    frame.physics.setPosition(checkBody(L, frame, 1), frame.ecs, checkVec3(L, 2));
    return 0;
}

int RollbackScript::ctxSetVelocity(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    const EntityId entity = checkBody(L, frame, 1);
    const glm::vec3 velocity = checkVec3(L, 2);
    frame.physics.setHorizontalVelocity(entity, frame.ecs, glm::vec2(velocity.x, velocity.z));
    frame.physics.setVerticalVelocity(entity, frame.ecs, velocity.y);
    return 0;
}

int RollbackScript::ctxImpulse(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    frame.physics.applyImpulse(checkBody(L, frame, 1), frame.ecs, checkVec3(L, 2));
    return 0;
}

int RollbackScript::ctxMoveKinematic(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    const EntityId entity = checkBody(L, frame, 1);
    const auto* transform = frame.ecs.tryGetComponent<Transform>(entity);
    const glm::quat rotation = transform ? transform->rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    frame.physics.moveKinematic(entity, frame.ecs, checkVec3(L, 2), rotation, frame.dt);
    return 0;
}

int RollbackScript::ctxContacts(lua_State* L) {
    RollbackFrameContext& frame = frameIn(L);
    lua_createtable(L, static_cast<int>(frame.contacts.size()), 0);
    int index = 1;
    for (const Physics::CollisionEvent& contact : frame.contacts) {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, entt::to_integral(contact.first));
        lua_setfield(L, -2, "a");
        lua_pushnumber(L, entt::to_integral(contact.second));
        lua_setfield(L, -2, "b");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

} // namespace engine::core
