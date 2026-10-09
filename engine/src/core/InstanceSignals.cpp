#include "core/InstanceSignals.hpp"

#include <algorithm>

#include <lua.h>

#include "core/ScriptInstanceApi.hpp"

namespace engine::core {
namespace {

bool isChangeEvent(const std::string& event) { return event == "Changed" || event.rfind("Changed:", 0) == 0; }

InstanceRef refOfSlot(uint64_t slot) { return static_cast<InstanceRef>(slot >> 32); }
uint32_t eventOfSlot(uint64_t slot) { return static_cast<uint32_t>(slot & 0xffffffffu); }

std::string handlerLabel(const std::string& event) {
    const size_t colon = event.find(':');
    if (colon == std::string::npos) return event + " handler";
    if (event.compare(0, colon, "Changed") == 0) return "GetPropertyChangedSignal(\"" + event.substr(colon + 1) + "\") handler";
    return "GetAttributeChangedSignal(\"" + event.substr(colon + 1) + "\") handler";
}

} // namespace

uint64_t SignalHub::slotOf(InstanceRef ref, const std::string& event) {
    const auto [it, inserted] = eventIds_.try_emplace(event, static_cast<uint32_t>(eventNames_.size()));
    if (inserted) eventNames_.push_back(event);
    return (static_cast<uint64_t>(ref) << 32) | it->second;
}

uint64_t SignalHub::add(std::shared_ptr<Connection> connection, InstanceRef ref, const std::string& event) {
    connection->id = nextId_++;
    connection->slot = slotOf(ref, event);
    connection->watchesChanges = isChangeEvent(event);
    if (connection->watchesChanges) ++changeWatchers_[ref];
    ++eventCounts_[eventOfSlot(connection->slot)];
    slots_[connection->slot].push_back(connection);
    byId_[connection->id] = connection;
    return connection->id;
}

uint64_t SignalHub::connect(lua_State* vm, InstanceRef ref, const std::string& event, int fnRef, bool once) {
    auto connection = std::make_shared<Connection>();
    connection->vm = vm;
    connection->fnRef = fnRef;
    connection->once = once;
    return add(std::move(connection), ref, event);
}

uint64_t SignalHub::connectWait(lua_State* thread, InstanceRef ref, const std::string& event) {
    auto connection = std::make_shared<Connection>();
    connection->vm = lua_mainthread(thread);
    connection->thread = thread;
    connection->once = true;
    return add(std::move(connection), ref, event);
}

void SignalHub::disconnect(uint64_t id) {
    const auto found = byId_.find(id);
    if (found == byId_.end()) return;
    std::shared_ptr<Connection> connection = found->second;
    byId_.erase(found);
    connection->connected = false;

    const auto slot = slots_.find(connection->slot);
    if (slot != slots_.end()) {
        auto& list = slot->second;
        list.erase(std::remove(list.begin(), list.end(), connection), list.end());
        if (list.empty()) slots_.erase(slot);
    }
    if (connection->watchesChanges) {
        const InstanceRef ref = refOfSlot(connection->slot);
        if (--changeWatchers_[ref] <= 0) changeWatchers_.erase(ref);
    }
    const uint32_t event = eventOfSlot(connection->slot);
    if (--eventCounts_[event] <= 0) eventCounts_.erase(event);
    retire(connection);
}

void SignalHub::retire(const std::shared_ptr<Connection>& connection) {
    if (connection->fnRef >= 0 && connection->vm != nullptr) retired_.push_back(connection);
}

void SignalHub::releaseRetired() {
    for (size_t i = 0; i < retired_.size();) {
        // Still referenced by a queued call: keep the function alive for it.
        if (retired_[i].use_count() > 1) {
            ++i;
            continue;
        }
        if (retired_[i]->vm != nullptr) lua_unref(retired_[i]->vm, retired_[i]->fnRef);
        retired_.erase(retired_.begin() + static_cast<long>(i));
    }
}

bool SignalHub::isConnected(uint64_t id) const { return byId_.count(id) != 0; }

bool SignalHub::hasConnections(InstanceRef ref, const std::string& event) const {
    const auto id = eventIds_.find(event);
    if (id == eventIds_.end()) return false;
    return slots_.count((static_cast<uint64_t>(ref) << 32) | id->second) != 0;
}

bool SignalHub::anyConnections(const std::string& event) const {
    const auto id = eventIds_.find(event);
    return id != eventIds_.end() && eventCounts_.count(id->second) != 0;
}

bool SignalHub::watchesChanges(InstanceRef ref) const { return changeWatchers_.count(ref) != 0; }

void SignalHub::fire(InstanceRef ref, const std::string& event, std::vector<SignalArg> args) {
    const auto id = eventIds_.find(event);
    if (id == eventIds_.end()) return;
    const auto slot = slots_.find((static_cast<uint64_t>(ref) << 32) | id->second);
    if (slot == slots_.end()) return;

    const std::vector<std::shared_ptr<Connection>> listeners = slot->second;
    if (depth_ + 1 > kMaxReentrancy) {
        if (auto* scripting = static_cast<Scripting*>(lua_callbacks(listeners.front()->vm)->userdata)) {
            scripting->reportError("runtime error: Maximum event re-entrancy depth exceeded for " + event);
        }
        return;
    }
    const std::string label = handlerLabel(event);
    for (const auto& connection : listeners) {
        Call call{connection, args, label, depth_ + 1, forceDelivery_};
        if (connection->once) {
            disconnect(connection->id);
            call.force = true;
        }
        queue_.push_back(std::move(call));
    }
}

void SignalHub::flush() {
    if (flushing_) return;
    flushing_ = true;
    std::deque<Call> held;
    // A runaway chain stops here; the rest waits for the next flush.
    size_t budget = 100000;
    while (!queue_.empty() && budget-- > 0) {
        Call call = std::move(queue_.front());
        queue_.pop_front();
        Connection& connection = *call.connection;
        if ((!connection.connected && !call.force) || connection.vm == nullptr) continue;
        auto* scripting = static_cast<Scripting*>(lua_callbacks(connection.vm)->userdata);
        if (scripting == nullptr) continue;
        if (scripting->debugPaused()) {
            held.push_back(std::move(call));
            continue;
        }
        depth_ = call.depth;
        const auto pushArgs = [&call](lua_State* L) {
            lua_checkstack(L, static_cast<int>(call.args.size()) + 8);
            for (const SignalArg& arg : call.args) pushSignalArg(L, arg);
            return static_cast<int>(call.args.size());
        };
        if (connection.thread != nullptr) {
            scripting->resumeWaiting(connection.thread, pushArgs);
        } else {
            scripting->runHandler(connection.vm, connection.fnRef, pushArgs, call.label);
        }
    }
    depth_ = 0;
    for (auto it = held.rbegin(); it != held.rend(); ++it) queue_.push_front(std::move(*it));
    flushing_ = false;
    releaseRetired();
}

void SignalHub::forgetVm(lua_State* vm) {
    std::vector<uint64_t> ids;
    for (const auto& [id, connection] : byId_) {
        if (connection->vm == vm) ids.push_back(id);
    }
    for (uint64_t id : ids) {
        const auto connection = byId_[id];
        connection->vm = nullptr; // the VM is closing; no unref
        disconnect(id);
    }
    for (Call& call : queue_) {
        if (call.connection->vm == vm) call.connection->vm = nullptr;
    }
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Call& c) { return c.connection->vm == nullptr; }),
                 queue_.end());
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [vm](const std::shared_ptr<Connection>& c) { return c->vm == vm || c->vm == nullptr; }),
                   retired_.end());
}

void SignalHub::forgetInstances(const std::vector<InstanceRef>& refs) {
    std::vector<uint64_t> ids;
    for (const auto& [id, connection] : byId_) {
        if (std::find(refs.begin(), refs.end(), refOfSlot(connection->slot)) != refs.end()) ids.push_back(id);
    }
    for (uint64_t id : ids) disconnect(id);
}

namespace signals {
namespace {

InstanceValue instanceArg(InstanceRef ref) { return InstanceValue::ofInstance(ref); }

bool canTouch(ECS& ecs, InstanceRef ref) {
    const PropertyDef* property = instances::findProperty(instances::className(ecs, ref), "CanTouch");
    InstanceValue value;
    return property == nullptr || !instances::getProperty(ecs, ref, *property, value) || value.boolean;
}

void fireReparent(ECS& ecs, SignalHub& hub, InstanceRef child, InstanceRef oldParent, InstanceRef newParent) {
    const SignalArg childArg = SignalArg::of(instanceArg(child));
    if (oldParent != kNoInstance) hub.fire(oldParent, "ChildRemoved", {childArg});
    if (newParent != kNoInstance) hub.fire(newParent, "ChildAdded", {childArg});

    const bool ancestry = hub.anyConnections("AncestryChanged");
    const bool removing = oldParent != kNoInstance && hub.anyConnections("DescendantRemoving");
    const bool adding = newParent != kNoInstance && hub.anyConnections("DescendantAdded");
    if (!ancestry && !removing && !adding) return;

    std::vector<InstanceRef> moved{child};
    for (InstanceRef d : instances::descendants(ecs, child)) moved.push_back(d);
    if (ancestry) {
        for (InstanceRef m : moved) hub.fire(m, "AncestryChanged", {childArg, SignalArg::of(instanceArg(newParent))});
    }
    auto eachAncestor = [&](InstanceRef start, const char* event) {
        int guard = 0;
        for (InstanceRef at = start; at != kNoInstance && guard < 1024; at = instances::parent(ecs, at), ++guard) {
            if (!hub.hasConnections(at, event)) continue;
            for (InstanceRef m : moved) hub.fire(at, event, {SignalArg::of(instanceArg(m))});
        }
    };
    if (removing) eachAncestor(oldParent, "DescendantRemoving");
    if (adding) eachAncestor(newParent, "DescendantAdded");
}

} // namespace

std::shared_ptr<SignalHub> hubFor(ECS& ecs) {
    auto& ctx = ecs.raw().ctx();
    if (auto* existing = ctx.find<std::shared_ptr<SignalHub>>()) return *existing;
    return ctx.emplace<std::shared_ptr<SignalHub>>(std::make_shared<SignalHub>());
}

SignalHub* findHub(ECS& ecs) {
    auto* hub = ecs.raw().ctx().find<std::shared_ptr<SignalHub>>();
    return hub != nullptr ? hub->get() : nullptr;
}

void flush(ECS& ecs) {
    if (SignalHub* hub = findHub(ecs)) hub->flush();
}

RunServiceState& runService(ECS& ecs) { return ecs.raw().ctx().emplace<RunServiceState>(); }

void propertyChanged(ECS& ecs, InstanceRef ref, const std::string& property, const InstanceValue& value) {
    SignalHub* hub = findHub(ecs);
    if (hub == nullptr || !hub->watchesChanges(ref)) return;
    // ValueBase objects fire Changed with the new Value, and only for Value.
    if (instances::classIsA(instances::className(ecs, ref), "ValueBase")) {
        if (property == "Value") hub->fire(ref, "Changed", {SignalArg::of(value)});
    } else {
        hub->fire(ref, "Changed", {SignalArg::of(InstanceValue::ofString(property))});
    }
    hub->fire(ref, "Changed:" + property);
}

void attributeChanged(ECS& ecs, InstanceRef ref, const std::string& attribute) {
    SignalHub* hub = findHub(ecs);
    if (hub == nullptr) return;
    hub->fire(ref, "AttributeChanged", {SignalArg::of(InstanceValue::ofString(attribute))});
    hub->fire(ref, "AttributeChanged:" + attribute);
}

void parentChanged(ECS& ecs, InstanceRef child, InstanceRef oldParent) {
    SignalHub* hub = findHub(ecs);
    if (hub == nullptr) return;
    const InstanceRef newParent = instances::parent(ecs, child);
    propertyChanged(ecs, child, "Parent", instanceArg(newParent));
    fireReparent(ecs, *hub, child, oldParent, newParent);
}

void destroying(ECS& ecs, InstanceRef ref) {
    SignalHub* hub = findHub(ecs);
    if (hub == nullptr) return;
    std::vector<InstanceRef> all{ref};
    for (InstanceRef d : instances::descendants(ecs, ref)) all.push_back(d);

    // Destroy disconnects everything on these objects, but what it fires
    // on the way out still reaches the handlers.
    hub->setForceDelivery(true);
    for (InstanceRef r : all) hub->fire(r, "Destroying");
    const InstanceRef oldParent = instances::parent(ecs, ref);
    if (oldParent != kNoInstance) {
        propertyChanged(ecs, ref, "Parent", InstanceValue{});
        fireReparent(ecs, *hub, ref, oldParent, kNoInstance);
    }
    hub->setForceDelivery(false);
    hub->forgetInstances(all);
}

void touch(ECS& ecs, EntityId a, EntityId b, bool began) {
    SignalHub* hub = findHub(ecs);
    const char* event = began ? "Touched" : "TouchEnded";
    if (hub == nullptr || !hub->anyConnections(event)) return;
    const InstanceRef refA = instances::refOf(ecs, a);
    const InstanceRef refB = instances::refOf(ecs, b);
    if (refA == kNoInstance || refB == kNoInstance || !canTouch(ecs, refA) || !canTouch(ecs, refB)) return;
    hub->fire(refA, event, {SignalArg::of(instanceArg(refB))});
    hub->fire(refB, event, {SignalArg::of(instanceArg(refA))});
}

void stepped(ECS& ecs, double dt) {
    RunServiceState& state = runService(ecs);
    state.time += dt;
    SignalHub* hub = findHub(ecs);
    if (hub == nullptr) return;
    hub->fire(kRunServiceInstance, "Stepped",
              {SignalArg::of(InstanceValue::ofNumber(state.time)), SignalArg::of(InstanceValue::ofNumber(dt))});
    hub->fire(kRunServiceInstance, "PreSimulation", {SignalArg::of(InstanceValue::ofNumber(dt))});
}

void heartbeat(ECS& ecs, double dt) {
    SignalHub* hub = findHub(ecs);
    if (hub == nullptr) return;
    hub->fire(kRunServiceInstance, "PostSimulation", {SignalArg::of(InstanceValue::ofNumber(dt))});
    hub->fire(kRunServiceInstance, "Heartbeat", {SignalArg::of(InstanceValue::ofNumber(dt))});
}

void renderStepped(ECS& ecs, double dt) {
    SignalHub* hub = findHub(ecs);
    if (hub == nullptr) return;
    hub->fire(kRunServiceInstance, "PreRender", {SignalArg::of(InstanceValue::ofNumber(dt))});
    hub->fire(kRunServiceInstance, "RenderStepped", {SignalArg::of(InstanceValue::ofNumber(dt))});
}

} // namespace signals
} // namespace engine::core
