#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"
#include "core/Scripting.hpp"

struct lua_State;

// Roblox events (RBXScriptSignal) on the Instance tree. Signals are deferred
// like Roblox's SignalBehavior.Deferred: firing only queues the handlers, and
// they run when the current script resume ends (see docs/ROBLOX_BRIDGE.md).
namespace engine::core {

// A handler argument. Tables passed to BindableEvent:Fire are copied.
struct SignalArg {
    InstanceValue value;
    bool isTable = false;
    std::vector<SignalArg> keys;
    std::vector<SignalArg> values;

    static SignalArg of(InstanceValue v) {
        SignalArg arg;
        arg.value = std::move(v);
        return arg;
    }
};

// One per ECS, in the registry context. Shared with every script VM that
// uses it, so it outlives whichever of the ECS or the VMs goes first.
class SignalHub final : public DeferredWork {
public:
    uint64_t connect(lua_State* vm, InstanceRef ref, const std::string& event, int fnRef, bool once,
                     void* owner = nullptr);
    // Resumes `thread` (parked with Scripting::suspendCurrent) on the next fire.
    uint64_t connectWait(lua_State* thread, InstanceRef ref, const std::string& event);
    void disconnect(uint64_t id);
    [[nodiscard]] bool isConnected(uint64_t id) const;
    [[nodiscard]] bool hasConnections(InstanceRef ref, const std::string& event) const;
    // Any instance has a handler for this event.
    [[nodiscard]] bool anyConnections(const std::string& event) const;
    // Changed or GetPropertyChangedSignal is connected on ref.
    [[nodiscard]] bool watchesChanges(InstanceRef ref) const;

    void fire(InstanceRef ref, const std::string& event, std::vector<SignalArg> args = {});
    void flush() override;
    void forgetOwner(const void* owner) override;

    // A VM is closing: drop its connections and queued calls.
    void forgetVm(lua_State* vm);
    // Instances were destroyed: disconnect everything on them.
    void forgetInstances(const std::vector<InstanceRef>& refs);
    // Calls queued from now on run even if their connection is gone (Destroying).
    void setForceDelivery(bool force) { forceDelivery_ = force; }

    // Callbacks (OnServerInvoke, OnClientInvoke, OnInvoke): one function per
    // instance and name. fnRef -1 removes it.
    void setCallback(lua_State* vm, InstanceRef ref, const std::string& name, int fnRef, void* owner);
    // Calls the callback, or waits until one is set. Poll invokeDone, then takeInvoke.
    uint64_t invoke(InstanceRef ref, const std::string& name, std::vector<SignalArg> args);
    struct InvokeResult {
        bool done = false;
        bool ok = false;
        std::vector<SignalArg> values;
        std::string error;
    };
    void finishInvoke(uint64_t id, bool ok, std::vector<SignalArg> values, std::string error);
    [[nodiscard]] bool invokeDone(uint64_t id) const;
    InvokeResult takeInvoke(uint64_t id);

    // Roblox stops a chain of events re-firing each other at this depth.
    static constexpr int kMaxReentrancy = 10;

private:
    struct Connection {
        uint64_t id = 0;
        uint64_t slot = 0;
        lua_State* vm = nullptr;     // main thread of the owning VM
        lua_State* thread = nullptr; // set for Wait()
        void* owner = nullptr;       // thread data of the connecting script
        int fnRef = -1;
        bool once = false;
        bool connected = true;
        bool watchesChanges = false;
    };
    struct Call {
        std::shared_ptr<Connection> connection;
        std::vector<SignalArg> args;
        std::string label;
        int depth = 0;
        bool force = false;
        uint64_t invokeId = 0;
    };

    uint64_t slotOf(InstanceRef ref, const std::string& event);
    uint64_t add(std::shared_ptr<Connection> connection, InstanceRef ref, const std::string& event);
    void retire(const std::shared_ptr<Connection>& connection);
    void releaseRetired();

    std::unordered_map<std::string, uint32_t> eventIds_;
    std::vector<std::string> eventNames_;
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<Connection>>> slots_;
    std::unordered_map<uint64_t, std::shared_ptr<Connection>> byId_;
    std::unordered_map<uint32_t, int> eventCounts_;
    std::unordered_map<InstanceRef, int> changeWatchers_;
    std::deque<Call> queue_;
    std::unordered_map<uint64_t, uint64_t> callbackIds_; // slot -> connection id
    std::unordered_map<uint64_t, std::vector<std::pair<uint64_t, std::vector<SignalArg>>>> waitingInvokes_;
    std::unordered_map<uint64_t, InvokeResult> invokes_;
    uint64_t nextInvokeId_ = 1;
    std::vector<std::shared_ptr<Connection>> retired_;
    uint64_t nextId_ = 1;
    int depth_ = 0;
    bool flushing_ = false;
    bool forceDelivery_ = false;
};

// Registry field holding each VM's ref to the function that runs a callback
// and reports its result (set by registerInstanceApi).
inline constexpr const char* kInvokeTrampolineKey = "kronos.invoke.trampoline";

// Host-side RunService state, in the registry context.
struct RunServiceState {
    bool server = true;
    bool client = true;
    bool studio = false;
    bool running = true;
    double time = 0.0;
};

namespace signals {

// The ECS's hub, made on first use.
std::shared_ptr<SignalHub> hubFor(ECS& ecs);
// Null when no script has used signals on this ECS.
SignalHub* findHub(ECS& ecs);
void flush(ECS& ecs);
RunServiceState& runService(ECS& ecs);

// Called by core/InstanceTree.cpp when the tree changes.
void propertyChanged(ECS& ecs, InstanceRef ref, const std::string& property, const InstanceValue& value);
void attributeChanged(ECS& ecs, InstanceRef ref, const std::string& attribute);
void parentChanged(ECS& ecs, InstanceRef child, InstanceRef oldParent);
// Before the instance and its descendants are destroyed.
void destroying(ECS& ecs, InstanceRef ref);

// Host loops: physics contacts and RunService. Stepped and PreSimulation run
// before physics, PostSimulation and Heartbeat after it, RenderStepped and
// PreRender before drawing.
void touch(ECS& ecs, EntityId a, EntityId b, bool began);
void stepped(ECS& ecs, double dt);
void heartbeat(ECS& ecs, double dt);
void renderStepped(ECS& ecs, double dt);

} // namespace signals
} // namespace engine::core
