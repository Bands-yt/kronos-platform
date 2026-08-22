#include "core/Scripting.hpp"

#include "core/ScriptSecurity.hpp"
#include "core/ScriptThreadContext.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <lua.h>
#include <lualib.h>
#include <Luau/Compiler.h>

namespace engine::core {

namespace {

using Clock = std::chrono::steady_clock;

// Registry key for each VM's own require() module cache. A registry
// key is deliberately used rather than a global: the registry is not
// reachable from Lua at all, so a script cannot inspect, poison, or
// clear another module's cached value.
constexpr const char* kModuleCacheRegistryKey = "kronos.modulecache";
// Modules whose top-level chunk is currently running, in require() order.
// Kept in the registry rather than a C++ member so it is per-VM (and so a
// coroutine yielding mid-require cannot corrupt another VM's view of it),
// exactly like the module cache above.
constexpr const char* kModuleLoadingRegistryKey = "kronos.moduleloading";

// Normalises a module path to the single spelling the cache and the
// resolver both key on.
//
// Real imported Luau projects spell the same module several ways --
// "Shared/Math", "./Shared/Math", "Shared//Math" -- and without this they
// each miss the cache and then fail in the resolver as "no such module",
// which reads to a creator as a broken import rather than a spelling
// difference. Collapsing them here also means the cycle detection and the
// cache cannot be fooled into treating one module as two.
//
// Deliberately does NOT resolve "..": that stays rejected outright by the
// traversal check in luaRequire(), and quietly resolving it here would
// turn a blocked path into an allowed one.
std::string normaliseModulePath(std::string path) {
    // Windows-style separators appear in projects authored on Windows.
    // Done first so the absolute-path check still sees a leading "\".
    for (char& c : path) {
        if (c == '\\') c = '/';
    }

    std::string result;
    result.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        // Collapse runs of '/'.
        if (path[i] == '/' && !result.empty() && result.back() == '/') continue;
        result.push_back(path[i]);
    }

    // Strip a leading "./" and any interior "/./".
    for (size_t pos = result.find("/./"); pos != std::string::npos; pos = result.find("/./")) {
        result.erase(pos, 2);
    }
    while (result.size() >= 2 && result[0] == '.' && result[1] == '/') result.erase(0, 2);

    // A trailing slash names the same module as without one.
    while (!result.empty() && result.back() == '/') result.pop_back();
    return result;
}

// Owns the memory budget for one script's lua_State. Passed as the `ud`
// argument to lua_newstate() -- see Scripting::budgetAllocator.
struct AllocatorState {
    size_t used = 0;
    size_t budget = 0;
};

} // namespace

// Kronos ("Luau Sandbox & Security Manager"): what used to be a local
// ScriptBudget (deadline only) is now core::ScriptThreadContext, which
// also carries the VM's immutable SecurityIdentity -- see
// core/ScriptThreadContext.hpp. Same lua_setthreaddata mechanism, same
// sharing between a script's owning state and its coroutines; the
// identity rides along so a coroutine can never run at a different
// privilege level than the script that created it.
using ScriptBudget = ScriptThreadContext;

Scripting::Scripting() = default;
// Real, deliberate asymmetry from shutdown() -- see that method's own
// doc comment for why the destructor must NOT go through it: this
// closes every VM directly, without firing events.onUnload() first.
Scripting::~Scripting() { closeAllScripts(); }

bool Scripting::initialize() {
    initialized_ = true;
    std::fprintf(stdout, "Scripting: Luau VM ready (per-script isolation, %.1f ms / %zu MB budgets)\n",
                 maxExecutionMillisPerTick_, maxMemoryBytesPerScript_ / (1024 * 1024));
    return true;
}

size_t Scripting::totalUsedMemoryBytes() const {
    size_t total = 0;
    for (const auto& script : scripts_) {
        if (!script.alive || script.allocatorState == nullptr) continue;
        total += static_cast<const AllocatorState*>(script.allocatorState)->used;
    }
    return total;
}

void Scripting::closeAllScripts() {
    for (auto& script : scripts_) {
        unload(static_cast<ScriptId>(&script - &scripts_[0]));
    }
    scripts_.clear();
    parked_.clear();
    deferredQueue_.clear();
    // Same "known gap, acceptable because everything is cleared
    // immediately after" reasoning as unload()'s own comment: these hold
    // `owner` pointers into lua_States just closed above, so they must be
    // cleared here too, not left for the next fireUpdate()/fireCollision()/
    // fireInteract() to dereference.
    onUpdateCallbacks_.clear();
    onCollisionCallbacks_.clear();
    onInteractCallbacks_.clear();
    onUnloadCallbacks_.clear();
    onSessionJoinCallbacks_.clear();
    onSessionLeaveCallbacks_.clear();
    onPlayerJoinCallbacks_.clear();
    onPlayerLeaveCallbacks_.clear();
}

void Scripting::shutdown() {
    // Real, deliberate ordering: fireUnload() first, while every VM this
    // instance owns (and whatever sibling members of *this instance's own
    // owner* a registered handler's C++ bindings might touch) is still
    // alive -- shutdown() is always called explicitly, by a caller that
    // still has its own full object alive at the call site (e.g.
    // studio::plugins::ScriptedPlugin::loadInternal()'s reload path).
    // The destructor deliberately does NOT go through this method (see
    // ~Scripting()'s own comment) -- caught live: an implicit
    // destructor-triggered shutdown() used to fire onUnload after C++ had
    // already destroyed sibling members in reverse-declaration order
    // (ScriptedPlugin::outputLog_, in this exact case), a real use-after-
    // destruction bug (a Lua print() -> appendLine() -> already-freed
    // std::vector::push_back()) masquerading as a double-free. Firing
    // onUnload is a real, meaningful thing to do while an owner is still
    // fully alive and reloading; it is not a safe thing to do while that
    // same owner is itself in the middle of being torn down.
    fireUnload();
    closeAllScripts();
    initialized_ = false;
}

void* Scripting::budgetAllocator(void* ud, void* ptr, size_t oldSize, size_t newSize) {
    auto* state = static_cast<AllocatorState*>(ud);

    if (newSize == 0) {
        if (ptr) {
            std::free(ptr);
            state->used -= oldSize;
        }
        return nullptr;
    }

    size_t effectiveOld = ptr ? oldSize : 0;
    if (newSize > effectiveOld) {
        size_t growth = newSize - effectiveOld;
        if (state->used + growth > state->budget) {
            // Hard ceiling hit -- ScriptContext.MaxMemory from
            // docs/ARCHITECTURE.md §6. Returning null on growth is exactly
            // how a lua_Alloc signals out-of-memory; Luau turns this into
            // a normal catchable LUA_ERRMEM, not a crash.
            return nullptr;
        }
    }

    void* newPtr = std::realloc(ptr, newSize);
    if (!newPtr) return nullptr;

    state->used = state->used - effectiveOld + newSize;
    return newPtr;
}

void Scripting::scriptInterrupt(lua_State* L, int gc) {
    if (gc >= 0) return; // only care about safepoints, not GC-phase callbacks

    auto* budget = static_cast<ScriptBudget*>(lua_getthreaddata(L));
    if (!budget) return;

    if (Clock::now() >= budget->deadline) {
        // ScriptContext.MaxExecutionTimePerFrame watchdog. Called at a
        // VM safepoint (loop back-edge / call / ret), which is exactly
        // where Luau documents it's safe to raise an error from.
        luaL_error(L, "script exceeded its per-tick execution budget");
    }
}

lua_State* Scripting::spawnThread(lua_State* owner, int& outRef) {
    lua_State* thread = lua_newthread(owner); // pushed onto owner's stack
    outRef = lua_ref(owner, -1);
    lua_pop(owner, 1);
    lua_setthreaddata(thread, lua_getthreaddata(owner));
    return thread;
}

int Scripting::luaPrint(lua_State* L) {
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len); // coerces any type, matches Lua's print()
        if (i > 1) line += '\t';
        line.append(s, len);
        lua_pop(L, 1); // pop the string luaL_tolstring pushed
    }
    std::fprintf(stdout, "[luau] %s\n", line.c_str());
    if (self->outputCallback_) self->outputCallback_(line);
    return 0;
}

int Scripting::luaEngineLog(lua_State* L) {
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    const char* level = luaL_checkstring(L, 1);
    const char* message = luaL_checkstring(L, 2);
    std::fprintf(stdout, "[engine:%s] %s\n", level, message);
    if (self->outputCallback_) self->outputCallback_(std::string("[") + level + "] " + message);
    // TODO(analytics, §13 / safety, §10): this is the seam where engine-
    // level log lines would also become TelemetryEvent / moderation
    // signal inputs instead of only going to stdout.
    return 0;
}

void Scripting::registerEventCallback(std::vector<EventCallback>& list, lua_State* L, int stackIndex) {
    lua_State* owner = lua_mainthread(L); // registration can happen from a spawned coroutine, not just top level
    lua_pushvalue(L, stackIndex);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    list.push_back(EventCallback{owner, ref});
}

void Scripting::invokeCallback(const EventCallback& callback, int argCount, const char* eventName) {
    lua_State* L = callback.owner;
    refreshDeadline(L);
    lua_getref(L, callback.ref);           // push the callback function
    // With argCount == 0 (events.onUnload, a new Phase 5 addition -- every
    // pre-existing caller here has argCount >= 1) there are no args
    // pushed yet to move the function below; the function is already
    // correctly on top, ready for a 0-arg pcall, and lua_insert(L, -1) is
    // a real no-op call worth skipping rather than relying on.
    if (argCount > 0) lua_insert(L, -(argCount + 1)); // move it below the already-pushed args, for lua_pcall's [fn, args...] order
    int status = lua_pcall(L, argCount, 0, 0);
    if (status != LUA_OK) {
        std::fprintf(stderr, "[luau] %s callback error: %s\n", eventName, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

int Scripting::luaEventsOnUpdate(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    self->registerEventCallback(self->onUpdateCallbacks_, L, 1);
    return 0;
}

int Scripting::luaEventsOnCollision(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    self->registerEventCallback(self->onCollisionCallbacks_, L, 1);
    return 0;
}

int Scripting::luaEventsOnInteract(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    self->registerEventCallback(self->onInteractCallbacks_, L, 1);
    return 0;
}

int Scripting::luaEventsOnUnload(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    self->registerEventCallback(self->onUnloadCallbacks_, L, 1);
    return 0;
}

int Scripting::luaEventsOnSessionJoin(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    self->registerEventCallback(self->onSessionJoinCallbacks_, L, 1);
    return 0;
}

int Scripting::luaEventsOnSessionLeave(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    self->registerEventCallback(self->onSessionLeaveCallbacks_, L, 1);
    return 0;
}

int Scripting::luaEventsOnPlayerJoin(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    self->registerEventCallback(self->onPlayerJoinCallbacks_, L, 1);
    return 0;
}

int Scripting::luaEventsOnPlayerLeave(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    self->registerEventCallback(self->onPlayerLeaveCallbacks_, L, 1);
    return 0;
}

void Scripting::fireUnload() {
    for (auto& callback : onUnloadCallbacks_) {
        invokeCallback(callback, 0, "events.onUnload");
    }
}

void Scripting::fireSessionJoin() {
    for (auto& callback : onSessionJoinCallbacks_) {
        invokeCallback(callback, 0, "events.onSessionJoin");
    }
}

void Scripting::fireSessionLeave() {
    for (auto& callback : onSessionLeaveCallbacks_) {
        invokeCallback(callback, 0, "events.onSessionLeave");
    }
}

void Scripting::firePlayerJoin(uint32_t playerId, const std::string& displayName) {
    for (auto& callback : onPlayerJoinCallbacks_) {
        lua_pushnumber(callback.owner, static_cast<double>(playerId));
        lua_pushstring(callback.owner, displayName.c_str());
        invokeCallback(callback, 2, "events.onPlayerJoin");
    }
}

void Scripting::firePlayerLeave(uint32_t playerId, const std::string& displayName) {
    for (auto& callback : onPlayerLeaveCallbacks_) {
        lua_pushnumber(callback.owner, static_cast<double>(playerId));
        lua_pushstring(callback.owner, displayName.c_str());
        invokeCallback(callback, 2, "events.onPlayerLeave");
    }
}

void Scripting::fireCollision(uint32_t entityA, uint32_t entityB) {
    for (auto& callback : onCollisionCallbacks_) {
        lua_pushnumber(callback.owner, static_cast<double>(entityA));
        lua_pushnumber(callback.owner, static_cast<double>(entityB));
        invokeCallback(callback, 2, "events.onCollision");
    }
}

void Scripting::fireInteract(uint32_t entity, uint32_t interactor) {
    for (auto& callback : onInteractCallbacks_) {
        lua_pushnumber(callback.owner, static_cast<double>(entity));
        lua_pushnumber(callback.owner, static_cast<double>(interactor));
        invokeCallback(callback, 2, "events.onInteract");
    }
}

int Scripting::luaTaskWait(lua_State* L) {
    double seconds = luaL_optnumber(L, 1, 0.0);
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));

    // Deliberately does NOT lua_ref the thread here: `L` (the yielding
    // thread) is already kept alive by whoever resumed it -- loadAndRun's
    // threadRef, or the ref spawnThread() made for a task.spawn/defer
    // thread. Re-refing it here would just leak a second, redundant ref.
    // Instead we record the requested wake time and let the resumer (see
    // consumeWait() below, called from loadAndRun/luaTaskSpawn/tick())
    // attach it to the ParkedThread entry it already owns.
    self->pendingWaits_.push_back(Scripting::PendingWait{L, self->clock_ + seconds, self->clock_});
    return lua_yield(L, 0);
}

void Scripting::refreshDeadline(lua_State* owner) {
    auto* budget = static_cast<ScriptBudget*>(lua_getthreaddata(owner));
    if (!budget) return;
    budget->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                           std::chrono::duration<double, std::milli>(maxExecutionMillisPerTick_));
}

bool Scripting::consumeWait(lua_State* thread, double& outWakeTime, double& outStartTime) {
    for (size_t i = 0; i < pendingWaits_.size(); ++i) {
        if (pendingWaits_[i].thread == thread) {
            outWakeTime = pendingWaits_[i].wakeTime;
            outStartTime = pendingWaits_[i].startTime;
            pendingWaits_.erase(pendingWaits_.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

int Scripting::luaTaskSpawn(lua_State* L) {
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    lua_State* owner = lua_mainthread(L);

    int ref = 0;
    lua_State* thread = self->spawnThread(owner, ref);
    lua_xmove(L, thread, lua_gettop(L)); // move fn + args from the calling stack onto the new thread

    int nargs = lua_gettop(thread) - 1;
    self->refreshDeadline(owner);
    int status = lua_resume(thread, L, nargs);
    if (status != LUA_OK && status != LUA_YIELD) {
        std::fprintf(stderr, "[luau] task.spawn error: %s\n", lua_tostring(thread, -1));
    }
    if (status == LUA_YIELD) {
        double wakeTime = self->clock_, startTime = self->clock_;
        // If it yielded via task.wait(), consumeWait() gives us the real
        // requested wake time; otherwise (a yield we don't have a wake
        // condition for yet -- e.g. a future task.wait(event)) it defaults
        // to "resume next tick", which is the only sane fallback here.
        self->consumeWait(thread, wakeTime, startTime);
        self->parked_.push_back(Scripting::ParkedThread{thread, ref, wakeTime, startTime});
    } else {
        lua_unref(owner, ref);
    }
    return 0;
}

int Scripting::luaTaskDefer(lua_State* L) {
    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    lua_State* owner = lua_mainthread(L);

    int ref = 0;
    lua_State* thread = self->spawnThread(owner, ref);
    lua_xmove(L, thread, lua_gettop(L));

    self->deferredQueue_.push_back(Scripting::ParkedThread{thread, ref, 0.0, self->clock_});
    return 0;
}

void Scripting::registerBindings(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaPrint, "print", 1);
    lua_setglobal(L, "print");

    lua_newtable(L); // engine table
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEngineLog, "engine.log", 1);
    lua_setfield(L, -2, "log");
    lua_setglobal(L, "engine");

    lua_newtable(L); // task table
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaTaskWait, "task.wait", 1);
    lua_setfield(L, -2, "wait");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaTaskSpawn, "task.spawn", 1);
    lua_setfield(L, -2, "spawn");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaTaskDefer, "task.defer", 1);
    lua_setfield(L, -2, "defer");
    lua_setglobal(L, "task");

    lua_newtable(L); // events table
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEventsOnUpdate, "events.onUpdate", 1);
    lua_setfield(L, -2, "onUpdate");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEventsOnCollision, "events.onCollision", 1);
    lua_setfield(L, -2, "onCollision");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEventsOnInteract, "events.onInteract", 1);
    lua_setfield(L, -2, "onInteract");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEventsOnUnload, "events.onUnload", 1);
    lua_setfield(L, -2, "onUnload");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEventsOnSessionJoin, "events.onSessionJoin", 1);
    lua_setfield(L, -2, "onSessionJoin");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEventsOnSessionLeave, "events.onSessionLeave", 1);
    lua_setfield(L, -2, "onSessionLeave");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEventsOnPlayerJoin, "events.onPlayerJoin", 1);
    lua_setfield(L, -2, "onPlayerJoin");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaEventsOnPlayerLeave, "events.onPlayerLeave", 1);
    lua_setfield(L, -2, "onPlayerLeave");
    lua_setglobal(L, "events");

    // TODO(§6): `game`, `workspace`, and every DataModel service attach
    // here once the Instance/ECS translation layer exists. Everything
    // above is the full C++-function-exposure surface Scripting provides
    // on its own; bindingsHook_ (below) is where a real entity/animation/
    // material/physics API attaches instead -- see setBindingsHook()'s
    // doc comment for why that's a separate seam from this TODO, not the
    // same one.
    if (bindingsHook_) bindingsHook_(L);
}

// Kronos ("replace require() with a Kronos-managed Virtual File System
// asset loader"). Every lookup goes through Scripting::moduleResolver_;
// there is deliberately NO filesystem access anywhere in this path, so
// even a misconfigured host cannot turn require() into a file read.
int Scripting::luaRequire(lua_State* L) {
    const char* rawPath = luaL_checkstring(L, 1);
    // The path as written, kept only for diagnostics: every error below
    // quotes what the author actually typed, not the normalised form they
    // never wrote.
    const std::string requestedPath = rawPath != nullptr ? rawPath : "";
    std::string modulePath = normaliseModulePath(requestedPath);

    auto* self = static_cast<Scripting*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    if (self == nullptr) {
        luaL_error(L, "require() is unavailable on this VM");
        return 0;
    }

    // Reject traversal and absolute paths before the resolver ever sees
    // them. A resolver author should not have to remember to do this,
    // and getting it wrong once would expose the whole host filesystem
    // if any resolver is ever backed by real files.
    if (modulePath.empty()) {
        luaL_error(L, "require() needs a non-empty module path");
        return 0;
    }
    if (modulePath.find("..") != std::string::npos) {
        luaL_error(L, "require(\"%s\"): path traversal is not allowed", requestedPath.c_str());
        return 0;
    }
    if (modulePath.front() == '/' || modulePath.front() == '\\' ||
        (modulePath.size() >= 2 && modulePath[1] == ':')) {
        luaL_error(L, "require(\"%s\"): absolute paths are not allowed", requestedPath.c_str());
        return 0;
    }
    // NUL-splicing: a path whose C string stops early could pass these
    // checks and still reach a resolver as something different.
    if (modulePath.find('\0') != std::string::npos) {
        luaL_error(L, "require(): embedded NUL in module path");
        return 0;
    }

    // Real module cache, one per VM (and therefore per script), keyed by
    // the requested path. Repeated require() of the same module returns
    // the identical value, matching every mainstream module system --
    // and, importantly, stops a script from re-running a module's side
    // effects in a loop as a cheap amplification attack.
    lua_getfield(L, LUA_REGISTRYINDEX, kModuleCacheRegistryKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kModuleCacheRegistryKey);
    }
    // Absolute, not relative: the in-progress table pushed below sits
    // above this one, so a relative index at the caching write would
    // address the wrong slot.
    const int cacheIndex = lua_gettop(L);
    lua_getfield(L, cacheIndex, modulePath.c_str());
    if (!lua_isnil(L, -1)) {
        return 1; // real cache hit -- the cached value is already on top
    }
    lua_pop(L, 1); // the nil

    // Cycle detection. A module is only cached AFTER its chunk finishes,
    // so a cycle (A requires B requires A) can never hit the cache above:
    // without this it re-enters luaRequire forever and dies with Luau's
    // "C stack overflow", which names neither module and tells a creator
    // migrating a project nothing at all.
    lua_getfield(L, LUA_REGISTRYINDEX, kModuleLoadingRegistryKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kModuleLoadingRegistryKey);
    }
    const int loadingIndex = lua_gettop(L);

    // Already in progress -> a real cycle. Report the whole chain, in
    // require order, so the author can see where to break it.
    lua_getfield(L, loadingIndex, modulePath.c_str());
    const bool alreadyLoading = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (alreadyLoading) {
        std::string chain;
        const int count = static_cast<int>(lua_objlen(L, loadingIndex));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, loadingIndex, i);
            const char* entry = lua_tostring(L, -1);
            if (entry != nullptr) {
                if (!chain.empty()) chain += " -> ";
                chain += entry;
            }
            lua_pop(L, 1);
        }
        if (!chain.empty()) chain += " -> ";
        chain += modulePath;
        luaL_error(L, "require(\"%s\"): circular dependency: %s", requestedPath.c_str(), chain.c_str());
        return 0;
    }

    if (!self->moduleResolver_) {
        luaL_error(L, "require(\"%s\"): no module resolver is configured on this Kronos build",
                   modulePath.c_str());
        return 0;
    }

    std::string moduleSource;
    std::string resolveError;
    SecurityIdentity requester = currentSecurityIdentity(L);
    if (!self->moduleResolver_(modulePath, requester, moduleSource, resolveError)) {
        luaL_error(L, "require(\"%s\"): %s", modulePath.c_str(),
                   resolveError.empty() ? "module not found" : resolveError.c_str());
        return 0;
    }

    Luau::CompileOptions compileOptions;
    compileOptions.optimizationLevel = 1;
    compileOptions.debugLevel = 1;
    std::string bytecode = Luau::compile(moduleSource, compileOptions);

    std::string chunkName = "=" + modulePath;
    if (luau_load(L, chunkName.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
        // luau_load left the real compile error on the stack.
        luaL_error(L, "require(\"%s\"): %s", modulePath.c_str(), lua_tostring(L, -1));
        return 0;
    }

    // Mark in-progress for the cycle check above, and record the order so
    // a cycle can name its chain.
    lua_pushboolean(L, 1);
    lua_setfield(L, loadingIndex, modulePath.c_str());
    lua_pushstring(L, modulePath.c_str());
    lua_rawseti(L, loadingIndex, static_cast<int>(lua_objlen(L, loadingIndex)) + 1);

    // The module runs on THIS thread, so it inherits this VM's identity
    // and its execution/memory budgets automatically -- a required
    // module is never a privilege boundary of its own.
    //
    // lua_pcall rather than lua_call specifically so the in-progress
    // marker below is cleared even when the module's own chunk throws.
    // lua_call propagates by longjmp, which would skip that cleanup and
    // leave the module permanently poisoned: every later require() of it
    // would be misreported as a circular dependency.
    const int callStatus = lua_pcall(L, 0, 1, 0);

    // Unmark, whether it succeeded or threw.
    lua_pushnil(L);
    lua_setfield(L, loadingIndex, modulePath.c_str());
    const int loadingCount = static_cast<int>(lua_objlen(L, loadingIndex));
    if (loadingCount > 0) {
        lua_pushnil(L);
        lua_rawseti(L, loadingIndex, loadingCount);
    }

    if (callStatus != 0) {
        // Re-raise the module's own error with the module named, so a
        // failure deep in a required chain says which module failed.
        const char* message = lua_tostring(L, -1);
        luaL_error(L, "require(\"%s\"): %s", requestedPath.c_str(),
                   message != nullptr ? message : "module raised an error");
        return 0;
    }

    // Cache before returning. A module that returns nothing caches
    // `true`, so a second require() is still a real cache hit rather
    // than silently re-running it.
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, cacheIndex, modulePath.c_str());
    return 1;
}

void Scripting::registerModuleLoader(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &Scripting::luaRequire, "require", 1);
    lua_setglobal(L, "require");
}

void Scripting::applySandbox(lua_State* L) {
    // Kronos ("Standard Library Lockdown"). Audited empirically against a
    // real VM rather than assumed -- what Luau upstream already denies,
    // confirmed absent from a live sandboxed state:
    //   io, package, require, load, loadstring, dofile, loadfile,
    //   collectgarbage  -- all ABSENT (no arbitrary code loading at all)
    //   os.*            -- only time/clock/date; execute/getenv/remove/
    //                      rename/exit/tmpname are ABSENT
    //   debug.*         -- only traceback; sethook/getinfo/getlocal/
    //                      setlocal/getupvalue/setupvalue/setmetatable
    //                      are ABSENT
    // Those are upstream design decisions, not something this function
    // has to (or can) redo.
    //
    // What the audit DID find still reachable, and what this function
    // therefore removes below: getfenv, setfenv and newproxy.
    static constexpr const char* kRemovedGlobals[] = {
        // getfenv/setfenv let a script read and REPLACE the environment
        // of a function -- including, given any reference to one, a
        // function belonging to a more privileged caller. That directly
        // undermines the whole point of SecurityIdentity, so they go
        // away for every identity, including StudioPlugin: nothing in
        // this codebase uses them (verified by search across all 27
        // shipped .lua files), so removing them costs nothing real.
        "getfenv",
        "setfenv",
        // newproxy creates userdata with a script-controlled metatable.
        // It has a long history as a sandbox-escape primitive and has no
        // legitimate use anywhere in Kronos today.
        "newproxy",
    };
    for (const char* name : kRemovedGlobals) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

    // luaL_sandbox() must come AFTER the removals above -- it freezes the
    // global table, and a frozen _G cannot then have entries niled out.
    // It also freezes the string metatable and the builtin tables
    // (math/table/string), so a script cannot repoint library functions
    // that C bindings might later rely on.
    luaL_sandbox(L);
}

ScriptId Scripting::loadAndRun(const std::string& chunkName, const std::string& source) {
    // Least privilege by default -- see the header's own comment.
    return loadAndRun(chunkName, source, SecurityIdentity::UserScript);
}

SecurityIdentity Scripting::identityOf(ScriptId id) const {
    if (id >= scripts_.size() || !scripts_[id].alive) return SecurityIdentity::UserScript;
    return scripts_[id].identity;
}

ScriptId Scripting::loadAndRun(const std::string& chunkName, const std::string& source, SecurityIdentity identity) {
    if (!initialized_) return kInvalidScript;

    auto* allocState = new AllocatorState{0, maxMemoryBytesPerScript_};
    lua_State* owner = lua_newstate(&Scripting::budgetAllocator, allocState);
    if (!owner) {
        std::fprintf(stderr, "Scripting: lua_newstate failed for \"%s\" (allocator budget too small?)\n", chunkName.c_str());
        delete allocState;
        return kInvalidScript;
    }

    luaL_openlibs(owner);
    registerBindings(owner);
    registerModuleLoader(owner);
    // Must run last: it freezes the global table, so every global this
    // VM will ever have has to be installed before this point.
    applySandbox(owner);

    auto* budget = new ScriptBudget{};
    budget->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                           std::chrono::duration<double, std::milli>(maxExecutionMillisPerTick_));
    // The VM's privilege level, fixed here and never mutated again.
    budget->identity = identity;
    lua_setthreaddata(owner, budget);
    lua_callbacks(owner)->interrupt = &Scripting::scriptInterrupt;

    Luau::CompileOptions compileOptions;
    compileOptions.optimizationLevel = 1;
    compileOptions.debugLevel = 1;
    std::string bytecode = Luau::compile(source, compileOptions);

    lua_State* thread = lua_newthread(owner);
    int threadRef = lua_ref(owner, -1);
    lua_pop(owner, 1);
    lua_setthreaddata(thread, budget);

    if (luau_load(thread, chunkName.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
        std::string message = std::string("compile error in \"") + chunkName + "\": " + lua_tostring(thread, -1);
        std::fprintf(stderr, "Scripting: %s\n", message.c_str());
        if (outputCallback_) outputCallback_(message);
        lua_unref(owner, threadRef);
        lua_close(owner);
        delete allocState;
        delete budget;
        return kInvalidScript;
    }

    refreshDeadline(owner);
    int status = lua_resume(thread, nullptr, 0);
    if (status != LUA_OK && status != LUA_YIELD) {
        std::string message = std::string("runtime error in \"") + chunkName + "\": " + lua_tostring(thread, -1);
        std::fprintf(stderr, "Scripting: %s\n", message.c_str());
        if (outputCallback_) outputCallback_(message);
    }

    ScriptId id = static_cast<ScriptId>(scripts_.size());
    scripts_.push_back(LoadedScript{chunkName, owner, allocState, budget, identity, /*alive=*/true});

    if (status == LUA_YIELD) {
        double wakeTime = clock_, startTime = clock_;
        consumeWait(thread, wakeTime, startTime);
        parked_.push_back(ParkedThread{thread, threadRef, wakeTime, startTime});
    } else {
        lua_unref(owner, threadRef);
    }
    return id;
}

void Scripting::unload(ScriptId id) {
    if (id >= scripts_.size() || !scripts_[id].alive) return;
    auto& script = scripts_[id];

    // Real bug found while auditing hot-reload stability (Alpha Completion
    // Checklist, "Lua Creator Experience"): core::Application's own
    // hot-reload path (see Application.cpp's "Real hot-reload path")
    // calls this on a live, shared Scripting instance's single script,
    // NOT during a full shutdown() -- so the previous "not built out
    // since nothing yet calls unload() outside of shutdown" reasoning was
    // already stale. Any parked task.wait()/task.defer() coroutine, or
    // any events.onUpdate/onCollision/onInteract/onUnload handler,
    // registered by THIS script and still live in one of the five lists
    // below would otherwise dangle the moment lua_close() below frees the
    // VM they belong to -- and the very next tick()/fire*() call
    // (Application.cpp runs scripting_.tick() right after this same hot-
    // reload scan, same frame) would resume/invoke a freed lua_State.
    // Every real gameplay-script example that uses events.onUpdate (e.g.
    // examples/lua/moving_platform.lua) hits this path on every reload.
    // Purging by owner here -- not unref'ing, since the whole VM these
    // refs live in is about to be destroyed anyway -- closes the gap.
    // Deliberately does NOT fire this script's own onUnload handler (see
    // docs/LUA_API.md's own onUnload entry: that hook is scoped to a real
    // Scripting::shutdown() call, not a single script's unload()).
    auto belongsToThisScript = [owner = script.owner](const ParkedThread& entry) {
        return lua_mainthread(entry.thread) == owner;
    };
    parked_.erase(std::remove_if(parked_.begin(), parked_.end(), belongsToThisScript), parked_.end());
    deferredQueue_.erase(std::remove_if(deferredQueue_.begin(), deferredQueue_.end(), belongsToThisScript),
                          deferredQueue_.end());
    auto belongsToThisScriptCallback = [owner = script.owner](const EventCallback& callback) {
        return callback.owner == owner;
    };
    onUpdateCallbacks_.erase(std::remove_if(onUpdateCallbacks_.begin(), onUpdateCallbacks_.end(), belongsToThisScriptCallback),
                             onUpdateCallbacks_.end());
    onCollisionCallbacks_.erase(
        std::remove_if(onCollisionCallbacks_.begin(), onCollisionCallbacks_.end(), belongsToThisScriptCallback),
        onCollisionCallbacks_.end());
    onInteractCallbacks_.erase(
        std::remove_if(onInteractCallbacks_.begin(), onInteractCallbacks_.end(), belongsToThisScriptCallback),
        onInteractCallbacks_.end());
    onUnloadCallbacks_.erase(
        std::remove_if(onUnloadCallbacks_.begin(), onUnloadCallbacks_.end(), belongsToThisScriptCallback),
        onUnloadCallbacks_.end());
    // Kronos ("Active Joining UI" -- Scripting event hooks): the four new
    // vectors go through the exact same real purge filter as the
    // pre-existing four above -- skipping this is the single easiest way
    // to regress the real use-after-free bug this filter exists to fix
    // (see this function's own comment above).
    onSessionJoinCallbacks_.erase(
        std::remove_if(onSessionJoinCallbacks_.begin(), onSessionJoinCallbacks_.end(), belongsToThisScriptCallback),
        onSessionJoinCallbacks_.end());
    onSessionLeaveCallbacks_.erase(
        std::remove_if(onSessionLeaveCallbacks_.begin(), onSessionLeaveCallbacks_.end(), belongsToThisScriptCallback),
        onSessionLeaveCallbacks_.end());
    onPlayerJoinCallbacks_.erase(
        std::remove_if(onPlayerJoinCallbacks_.begin(), onPlayerJoinCallbacks_.end(), belongsToThisScriptCallback),
        onPlayerJoinCallbacks_.end());
    onPlayerLeaveCallbacks_.erase(
        std::remove_if(onPlayerLeaveCallbacks_.begin(), onPlayerLeaveCallbacks_.end(), belongsToThisScriptCallback),
        onPlayerLeaveCallbacks_.end());

    lua_close(script.owner);
    delete static_cast<AllocatorState*>(script.allocatorState);
    delete static_cast<ScriptBudget*>(script.budgetState);
    script.owner = nullptr;
    script.alive = false;
}

void Scripting::tick(float dt) {
    if (!initialized_) return;
    clock_ += dt;

    // 1. Run anything task.defer()'d during the previous tick, per
    //    Roblox's "runs after this resumption cycle" ordering.
    std::vector<ParkedThread> deferredNow;
    deferredNow.swap(deferredQueue_);
    for (auto& entry : deferredNow) {
        lua_State* owner = lua_mainthread(entry.thread);
        int nargs = lua_gettop(entry.thread) - 1; // stack is still exactly [fn, args...] from task.defer()
        refreshDeadline(owner);
        int status = lua_resume(entry.thread, nullptr, nargs);
        if (status != LUA_OK && status != LUA_YIELD) {
            std::fprintf(stderr, "[luau] task.defer error: %s\n", lua_tostring(entry.thread, -1));
        }
        if (status == LUA_YIELD) {
            double wakeTime = clock_, startTime = clock_;
            consumeWait(entry.thread, wakeTime, startTime);
            parked_.push_back(ParkedThread{entry.thread, entry.ref, wakeTime, startTime});
        } else {
            lua_unref(owner, entry.ref);
        }
    }

    // 2. Resume every task.wait() whose deadline has elapsed. Threads not
    //    yet due stay parked for a later tick.
    std::vector<ParkedThread> stillParked;
    for (auto& entry : parked_) {
        if (entry.wakeTime > clock_) {
            stillParked.push_back(entry);
            continue;
        }
        lua_State* owner = lua_mainthread(entry.thread);
        lua_pushnumber(entry.thread, clock_ - entry.startTime); // task.wait() returns elapsed time
        refreshDeadline(owner);
        int status = lua_resume(entry.thread, nullptr, 1);
        if (status != LUA_OK && status != LUA_YIELD) {
            std::fprintf(stderr, "[luau] task.wait resume error: %s\n", lua_tostring(entry.thread, -1));
        }
        if (status == LUA_YIELD) {
            // Yielded again (e.g. the script called task.wait() a second
            // time) -- fetch the *new* wake time rather than reusing the
            // one that just elapsed.
            double wakeTime = clock_, startTime = clock_;
            consumeWait(entry.thread, wakeTime, startTime);
            stillParked.push_back(ParkedThread{entry.thread, entry.ref, wakeTime, startTime});
        } else {
            lua_unref(owner, entry.ref);
        }
    }
    parked_.swap(stillParked);

    // Note: deadlines are refreshed immediately before each lua_resume()
    // above via refreshDeadline(), not in bulk here -- see that function's
    // doc comment for why "once per GameLoop tick" measures the wrong
    // thing (wall-clock time including physics/render/vsync, not the
    // script's own execution time).

    // 3. events.onUpdate(dt) -- the real RunService.Heartbeat-equivalent
    //    this codebase has today. Full parity (Stepped/Heartbeat/
    //    RenderStepped as three distinct signals with different ordering
    //    guarantees, §6's table) still needs the Instance/DataModel event
    //    layer; this is the one, simple "runs every tick" signal that
    //    layer doesn't gate.
    for (auto& callback : onUpdateCallbacks_) {
        lua_pushnumber(callback.owner, static_cast<double>(dt));
        invokeCallback(callback, 1, "events.onUpdate");
    }
}

} // namespace engine::core
