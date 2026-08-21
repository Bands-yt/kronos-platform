#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/ScriptSecurity.hpp"

struct lua_State;

namespace engine::core {

using ScriptId = uint32_t;
inline constexpr ScriptId kInvalidScript = ~0u;

// Embeds the real Luau VM -- per docs/ARCHITECTURE.md Principle 1, this is
// deliberately *not* a "Lua 5.1-compatible" reimplementation. Every script
// gets its own lua_State (its own global table, its own custom allocator
// with a hard memory ceiling) rather than sharing one VM with coroutines,
// which is what makes the per-script memory/time budgets in §6 an actual
// property of the state object instead of a convention scripts have to
// cooperate with.
//
// What this class does NOT do yet, on purpose: there is no Instance/
// DataModel translation layer here (§6's "Instance is a view over the ECS"
// promise). That's a real, sizeable module of its own -- registering every
// service, every datatype metatable, every RBXScriptSignal -- and building
// a partial version of it would be more misleading than leaving the seam
// visible. registerBindings() is exactly that seam: it's where `game`,
// `workspace`, and friends attach once that layer exists.
class Scripting {
public:
    Scripting();
    ~Scripting();

    Scripting(const Scripting&) = delete;
    Scripting& operator=(const Scripting&) = delete;

    [[nodiscard]] bool initialize();
    void shutdown();

    // Compiles `source` with Luau's real compiler (not a hand-rolled
    // parser) and runs its top-level chunk on a fresh coroutine so it can
    // legally call task.wait() from the outermost scope, exactly like a
    // Roblox Script's top level can. Returns kInvalidScript and logs the
    // compile/runtime error on failure.
    // Kronos ("Luau Sandbox & Security Manager"): the identity-less
    // overload is kept for every existing call site and runs at
    // SecurityIdentity::UserScript -- the LEAST privileged level. That
    // default is deliberate: forgetting to pass an identity must produce
    // an under-privileged script, never an over-privileged one.
    ScriptId loadAndRun(const std::string& chunkName, const std::string& source);
    ScriptId loadAndRun(const std::string& chunkName, const std::string& source, SecurityIdentity identity);
    void unload(ScriptId id);

    // The real privilege level a loaded script is running at, for
    // diagnostics and tests. Returns UserScript for an unknown id --
    // failing closed, same rule as currentSecurityIdentity().
    [[nodiscard]] SecurityIdentity identityOf(ScriptId id) const;

    // Kronos ("replace require() with a Kronos-managed Virtual File
    // System asset loader"): installs the real resolver `require(path)`
    // calls. Kronos owns every lookup -- there is no filesystem fallback
    // anywhere, so a script cannot reach the host disk even if the
    // resolver is never installed (in that case require() raises a real,
    // honest "no module resolver is configured" error rather than
    // quietly reading a file).
    //
    // The resolver is handed the requesting script's real identity so it
    // can refuse to serve, say, an engine-internal module to a Level 0
    // user script. Return false and set `outError` to reject a request.
    using ModuleResolver = std::function<bool(const std::string& modulePath, SecurityIdentity requester,
                                               std::string& outSource, std::string& outError)>;
    void setModuleResolver(ModuleResolver resolver) { moduleResolver_ = std::move(resolver); }

    // Advances the scheduler by dt: drains anything task.defer()'d last
    // tick, resumes any task.wait() that has elapsed, matching the
    // Stepped -> physics -> Heartbeat -> RenderStepped ordering GameLoop
    // enforces (docs/ARCHITECTURE.md §6 table) by being called once per
    // phase from GameLoop::tick(), not by owning that ordering itself.
    void tick(float dt);

    // Sets the interrupt budget used by the sandboxing watchdog (see
    // Scripting.cpp's scriptInterrupt) -- ScriptContext.MaxExecutionTimePerFrame
    // from docs/ARCHITECTURE.md §6.
    void setMaxExecutionMillisPerTick(double ms) { maxExecutionMillisPerTick_ = ms; }
    void setMaxMemoryBytesPerScript(size_t bytes) { maxMemoryBytesPerScript_ = bytes; }

    // Kronos ("Developer Velocity Sprint" -- "Real-Time Visual
    // Performance Profiler" -- "Lua runtime memory allocations"): real,
    // live sum of every currently-alive script's own allocator
    // bookkeeping (see Scripting.cpp's AllocatorState -- already tracked
    // internally for the per-script memory ceiling, just never exposed
    // publicly before this). 0 for an instance with no scripts loaded --
    // a real, honest "nothing running" value, not an error.
    [[nodiscard]] size_t totalUsedMemoryBytes() const;

    // Called once at the end of registerBindings() for every new script
    // VM, after print/engine/task/events are already set up -- the seam a
    // higher layer (Application, which owns the ECS/Renderer/Physics/
    // Animation systems Scripting itself deliberately stays decoupled
    // from -- see class comment) uses to attach a real entity/animation/
    // material/physics API (core/ScriptWorldApi.hpp) without Scripting
    // depending on any of those systems directly. Not called at all if
    // never set -- a script VM with no hook attached just gets
    // print/engine/task/events and nothing else, same as before this
    // existed.
    void setBindingsHook(std::function<void(lua_State*)> hook) { bindingsHook_ = std::move(hook); }

    // events.onUpdate/.onCollision/.onInteract's firing half (the
    // registration half is luaEventsOn* below, wired into every VM's
    // `events` table by registerBindings()). fireUpdate() is called
    // automatically once per tick() (see its body) -- fireCollision()/
    // fireInteract() are public because their triggers live outside
    // Scripting (Physics' real ContactListener, and whatever interaction-
    // trigger system calls fireInteract -- see Application.cpp). Every
    // registered callback across every loaded script fires on each call
    // (a broadcast event bus, not a per-entity-connection signal system
    // like Roblox's Instance:GetPropertyChangedSignal-style API) -- the
    // right-sized model for a Luau surface this early, not a design claim
    // about the final API shape.
    void fireCollision(uint32_t entityA, uint32_t entityB);
    void fireInteract(uint32_t entity, uint32_t interactor);

    // Kronos ("Active Joining UI" -- Scripting event hooks): the real,
    // mechanical extension of the exact same broadcast-event-bus pattern
    // fireCollision()/fireInteract()/fireUnload() already establish --
    // every registered handler across every loaded script fires on each
    // call. Their triggers live outside Scripting (net::NetworkSession's
    // own setOnSessionJoined()/setOnSessionLeft()/setOnPlayerJoin()/
    // setOnPlayerLeave() callbacks, wired from core::Application -- see
    // its own startNetworking()), same "Scripting has zero net::
    // dependency" split fireCollision()/fireInteract() already model for
    // core::Physics. fireSessionJoin()/fireSessionLeave() take no
    // arguments (a script only needs to know its own session state
    // changed, matching fireUnload()'s own 0-arg shape); firePlayerJoin()/
    // firePlayerLeave() pass the real player id and real display name
    // (matching Roblox's own Players.PlayerAdded/PlayerRemoving shape,
    // which is the closest real precedent this API deliberately mirrors).
    void fireSessionJoin();
    void fireSessionLeave();
    void firePlayerJoin(uint32_t playerId, const std::string& displayName);
    void firePlayerLeave(uint32_t playerId, const std::string& displayName);

    // Kronos (Alpha Roadmap Phase 5, "Plugin System Expansion" -- "Plugin
    // lifecycle events (onLoad, onUnload, onUpdate)"): events.onUpdate
    // already covers onUpdate, and a script's own top-level chunk running
    // once on load already covers onLoad -- onUnload was the real, missing
    // one. Fired automatically at the very start of shutdown() (before any
    // VM actually closes below, so every registered handler still has a
    // real, live lua_State to run against) -- covers both a full
    // Scripting teardown AND studio::plugins::ScriptedPlugin's reload()
    // path, which is exactly shutdown()+initialize() again. Same real
    // "broadcast event bus" model onUpdate/onCollision/onInteract already
    // use (every registered handler across every loaded script fires),
    // not scoped to just one script.
    void fireUnload();

    // Every print()/engine.log() call, and loadAndRun()'s compile/runtime
    // error messages, are forwarded here in addition to stdout/stderr (not
    // instead of -- this never suppresses the existing terminal output).
    // studio::panels::DebugConsolePanel is the one caller today: without
    // this, a script run from Studio's console could only have its output
    // seen by whoever's watching the process's stdout, which defeats the
    // point of an in-Studio console.
    void setOutputCallback(std::function<void(const std::string&)> callback) { outputCallback_ = std::move(callback); }

    // Kronos (Alpha Roadmap Phase 4, "Networking Upgrade"): a real,
    // public wrapper around refreshDeadline() -- for an external caller
    // (core::ScriptNetworkApi) that invokes a registered Lua callback via
    // lua_pcall() asynchronously (triggered by a real network event
    // arriving during NetworkSession::tick(), not from inside this
    // class's own tick()-driven resume path) and needs to reset that VM's
    // watchdog deadline first, the same requirement every internal
    // resume/call site already has -- see refreshDeadline()'s own doc
    // comment. Does not otherwise couple Scripting to net:: at all (no
    // net:: type appears in this class's own interface).
    void refreshWatchdogDeadline(lua_State* owner) { refreshDeadline(owner); }

private:
    struct ParkedThread {
        lua_State* thread = nullptr;
        int ref = -1;         // lua_ref() id keeping the thread alive while parked
        double wakeTime = 0.0;
        double startTime = 0.0;
    };

    // Bookkeeping written by luaTaskWait (which only ever sees the raw
    // lua_State* of the yielding thread, not our ref for it) and consumed
    // by whichever call site resumed that thread, so the ParkedThread it
    // re-parks carries the real requested wake time instead of "resume
    // next tick". See consumeWait() in Scripting.cpp.
    struct PendingWait {
        lua_State* thread = nullptr;
        double wakeTime = 0.0;
        double startTime = 0.0;
    };

    struct LoadedScript {
        std::string name;
        lua_State* owner = nullptr; // the VM instance; owns the allocator budget
        void* allocatorState = nullptr; // AllocatorState*, see Scripting.cpp
        void* budgetState = nullptr;    // ScriptThreadContext*, see Scripting.cpp
        SecurityIdentity identity = SecurityIdentity::UserScript;
        bool alive = false;
    };

    void registerBindings(lua_State* L);
    void applySandbox(lua_State* L);
    // Real, shared teardown -- closes every VM, clears every queue/
    // callback list. Used by both shutdown() (after firing onUnload) and
    // ~Scripting() (which deliberately skips firing onUnload) -- see
    // shutdown()'s own doc comment for why those two are not the same
    // call.
    void closeAllScripts();
    lua_State* spawnThread(lua_State* owner, int& outRef);

    // If `thread` just yielded via task.wait(), returns true and fills in
    // the wake/start time it recorded (removing the bookkeeping entry).
    // Otherwise returns false and the caller re-parks with a "resume next
    // tick" default -- the correct behavior for a yield we don't have a
    // wake condition for yet (see the note in luaTaskSpawn).
    bool consumeWait(lua_State* thread, double& outWakeTime, double& outStartTime);

    // Resets the watchdog deadline on `owner`'s shared ScriptBudget to
    // "now + maxExecutionMillisPerTick_". Must be called immediately
    // before every lua_resume() -- not once per GameLoop tick -- so the
    // budget measures the script's own execution time between safepoints,
    // not wall-clock time that happens to include physics/render/vsync
    // work from everything *else* GameLoop::tick() does between resumes.
    void refreshDeadline(lua_State* owner);

    static void* budgetAllocator(void* ud, void* ptr, size_t oldSize, size_t newSize);
    static void scriptInterrupt(lua_State* L, int gc);

    static int luaPrint(lua_State* L);
    static int luaTaskWait(lua_State* L);
    static int luaTaskSpawn(lua_State* L);
    static int luaTaskDefer(lua_State* L);
    static int luaEngineLog(lua_State* L);
    static int luaEventsOnUpdate(lua_State* L);
    static int luaEventsOnCollision(lua_State* L);
    static int luaEventsOnInteract(lua_State* L);
    static int luaEventsOnUnload(lua_State* L);
    static int luaEventsOnSessionJoin(lua_State* L);
    static int luaEventsOnSessionLeave(lua_State* L);
    static int luaEventsOnPlayerJoin(lua_State* L);
    static int luaEventsOnPlayerLeave(lua_State* L);

    // One registered events.onX(fn) callback: `owner` is the VM's main
    // thread (lua_mainthread() of whatever thread called events.onX --
    // registration can happen from a spawned coroutine, same reasoning as
    // ParkedThread), `ref` is a lua_ref() into that VM's registry, valid
    // on any thread of the same state (the registry is shared VM-wide).
    struct EventCallback {
        lua_State* owner = nullptr;
        int ref = -1;
    };

    // Shared by fireCollision()/fireInteract()/fireUpdate(): looks up
    // `callback.ref`, pushes `args` (already on the stack, `argCount` of
    // them), and lua_pcall()s it on `callback.owner`, refreshing that VM's
    // watchdog deadline first (same requirement as every other resume/call
    // site in this class -- see refreshDeadline()'s doc comment).
    void invokeCallback(const EventCallback& callback, int argCount, const char* eventName);

    void registerEventCallback(std::vector<EventCallback>& list, lua_State* L, int stackIndex);

    std::vector<LoadedScript> scripts_;
    std::vector<ParkedThread> parked_;
    std::vector<ParkedThread> deferredQueue_;
    std::vector<PendingWait> pendingWaits_;
    std::vector<EventCallback> onUpdateCallbacks_;
    std::vector<EventCallback> onCollisionCallbacks_;
    std::vector<EventCallback> onInteractCallbacks_;
    std::vector<EventCallback> onUnloadCallbacks_;
    std::vector<EventCallback> onSessionJoinCallbacks_;
    std::vector<EventCallback> onSessionLeaveCallbacks_;
    std::vector<EventCallback> onPlayerJoinCallbacks_;
    std::vector<EventCallback> onPlayerLeaveCallbacks_;
    std::function<void(lua_State*)> bindingsHook_;
    std::function<void(const std::string&)> outputCallback_;
    ModuleResolver moduleResolver_;

    // Kronos ("Standard Library Lockdown"): installs the real,
    // VFS-backed require() into a freshly created VM. Called before
    // applySandbox() freezes the global table.
    void registerModuleLoader(lua_State* L);
    static int luaRequire(lua_State* L);

    double clock_ = 0.0;
    double maxExecutionMillisPerTick_ = 8.0;   // ScriptContext.MaxExecutionTimePerFrame default, §6
    size_t maxMemoryBytesPerScript_ = 256u * 1024u * 1024u; // ScriptContext.MaxMemory default, §6
    bool initialized_ = false;
};

} // namespace engine::core
