#include "core/RobloxReplication.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/InstanceSignals.hpp"
#include "core/Logger.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/RobloxRemoteNet.hpp"
#include "core/RobloxWire.hpp"

namespace engine::core::replication {
namespace {

using wire::Reader;
using wire::Writer;

enum class Kind : uint8_t { Records = remotenet::kFirstRawKind, Destroy, SnapshotDone };

constexpr uint32_t kGameId = 1;
constexpr uint32_t kWorkspaceId = 2;
constexpr uint32_t kFirstId = 16;
constexpr size_t kChunkBytes = 32 * 1024;
constexpr double kPendingGiveUpSeconds = 10.0;

constexpr const char* kReplicatedServices[] = {"ReplicatedStorage", "ReplicatedFirst", "Lighting", "Players",
                                               "StarterGui",        "StarterPack",     "StarterPlayer", "Teams",
                                               "SoundService"};
// Each client makes its own from the Starter folders.
constexpr const char* kClientOwnedClasses[] = {"Backpack", "PlayerGui", "PlayerScripts"};

struct Property {
    std::string name;
    InstanceValue value;
    uint32_t refId = 0; // Instance values
};

struct Record {
    uint32_t id = 0;
    uint32_t parentId = 0;
    std::string className;
    std::string name;
    uint32_t playerNetId = 0;
    std::vector<Property> properties;
    std::vector<std::pair<std::string, InstanceValue>> attributes;
    std::vector<std::string> tags;
};

struct PendingRecord {
    Record record;
    double age = 0.0;
};

struct State {
    bool active = false;
    bool server = false;
    std::unordered_map<InstanceRef, uint32_t> ids;
    std::unordered_map<uint32_t, InstanceRef> refs;

    // Server
    uint32_t nextId = kFirstId;
    double sinceSend = 0.0;
    std::unordered_map<uint32_t, std::vector<uint8_t>> sent;
    std::vector<uint32_t> clients;
    std::vector<uint32_t> newClients;

    // Client
    bool snapshotDone = false;
    bool candidatesTaken = false;
    double waited = 0.0;
    std::vector<InstanceRef> candidates;
    std::vector<PendingRecord> pending;
};

State& stateOf(ECS& ecs) { return ecs.raw().ctx().emplace<State>(); }

bool same(const InstanceValue& a, const InstanceValue& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case InstanceValue::Type::Nil: return true;
        case InstanceValue::Type::Bool: return a.boolean == b.boolean;
        case InstanceValue::Type::Number: return a.number == b.number;
        case InstanceValue::Type::String: return a.text == b.text;
        case InstanceValue::Type::Enum: return a.enumType == b.enumType && a.text == b.text;
        case InstanceValue::Type::Instance: return a.ref == b.ref;
        case InstanceValue::Type::CFrame: return a.vec == b.vec && a.rot == b.rot;
        case InstanceValue::Type::UDim2: return a.vec == b.vec && a.number == b.number;
        default: return a.vec == b.vec;
    }
}

bool isClientOwned(const std::string& className) {
    return std::any_of(std::begin(kClientOwnedClasses), std::end(kClientOwnedClasses),
                       [&](const char* c) { return className == c; });
}

// Engine-only entities (avatar rigs, cameras, lights without InstanceInfo)
// and characters stay local to each side.
bool replicates(ECS& ecs, InstanceRef ref) {
    const EntityId e = instances::entityOf(ecs, ref);
    if (e == kNullEntity || ecs.tryGetComponent<InstanceInfo>(e) == nullptr) return false;
    if (isClientOwned(instances::className(ecs, ref))) return false;
    return players::playerFromCharacter(ecs, ref) == kNoInstance;
}

void collect(ECS& ecs, InstanceRef at, int depth, std::vector<std::pair<InstanceRef, InstanceRef>>& out) {
    if (depth > 512) return;
    for (InstanceRef child : instances::children(ecs, at)) {
        if (!replicates(ecs, child)) continue;
        out.emplace_back(child, at);
        collect(ecs, child, depth + 1, out);
    }
}

// Everything that replicates, each with its parent, parents first.
std::vector<std::pair<InstanceRef, InstanceRef>> replicatedTree(ECS& ecs) {
    std::vector<std::pair<InstanceRef, InstanceRef>> out;
    collect(ecs, kWorkspaceInstance, 0, out);
    for (const char* name : kReplicatedServices) {
        const InstanceRef service = instances::findService(ecs, name);
        if (service == kNoInstance || service == kWorkspaceInstance) continue;
        out.emplace_back(service, kGameInstance);
        collect(ecs, service, 0, out);
    }
    return out;
}

const std::vector<const PropertyDef*>& replicatedProperties(const std::string& className) {
    static std::unordered_map<std::string, std::vector<const PropertyDef*>> cache;
    if (const auto found = cache.find(className); found != cache.end()) return found->second;
    std::vector<const PropertyDef*> list;
    for (const ClassDef* c = instances::findClass(className); c != nullptr;
         c = c->superclass.empty() ? nullptr : instances::findClass(c->superclass)) {
        for (const PropertyDef& p : c->properties) {
            if (!p.replicated || !p.serialized || p.context == PropertyContext::ServerOnly) continue;
            if (p.name == "Name" || p.name == "Parent" || p.name == "LocalPlayer") continue;
            const bool seen = std::any_of(list.begin(), list.end(), [&](const PropertyDef* d) { return d->name == p.name; });
            if (!seen) list.push_back(&p);
        }
    }
    return cache.emplace(className, std::move(list)).first->second;
}

void writeValue(Writer& w, const InstanceValue& v, uint32_t refId) {
    w.u8(static_cast<uint8_t>(v.type));
    switch (v.type) {
        case InstanceValue::Type::Nil: break;
        case InstanceValue::Type::Bool: w.u8(v.boolean ? 1 : 0); break;
        case InstanceValue::Type::Number: w.f64(v.number); break;
        case InstanceValue::Type::String: w.str(v.text); break;
        case InstanceValue::Type::Enum:
            w.str(v.enumType);
            w.str(v.text);
            w.f64(v.number);
            break;
        case InstanceValue::Type::Instance: w.u32(refId); break;
        case InstanceValue::Type::CFrame:
            w.vec3(v.vec);
            w.f32(v.rot.w);
            w.f32(v.rot.x);
            w.f32(v.rot.y);
            w.f32(v.rot.z);
            break;
        case InstanceValue::Type::UDim2:
            w.vec3(v.vec);
            w.f64(v.number);
            break;
        default: w.vec3(v.vec); break;
    }
}

bool readValue(Reader& r, InstanceValue& v, uint32_t& refId) {
    const uint8_t type = r.u8();
    if (type > static_cast<uint8_t>(InstanceValue::Type::UDim2)) return false;
    v = InstanceValue{};
    v.type = static_cast<InstanceValue::Type>(type);
    switch (v.type) {
        case InstanceValue::Type::Nil: break;
        case InstanceValue::Type::Bool: v.boolean = r.u8() != 0; break;
        case InstanceValue::Type::Number: v.number = r.f64(); break;
        case InstanceValue::Type::String: v.text = r.str(); break;
        case InstanceValue::Type::Enum:
            v.enumType = r.str();
            v.text = r.str();
            v.number = r.f64();
            break;
        case InstanceValue::Type::Instance: refId = r.u32(); break;
        case InstanceValue::Type::CFrame:
            v.vec = r.vec3();
            v.rot.w = r.f32();
            v.rot.x = r.f32();
            v.rot.y = r.f32();
            v.rot.z = r.f32();
            break;
        case InstanceValue::Type::UDim2:
            v.vec = r.vec3();
            v.number = r.f64();
            break;
        default: v.vec = r.vec3(); break;
    }
    return r.ok();
}

// ---- Server

uint32_t assignId(State& s, InstanceRef ref) {
    if (ref == kGameInstance) return kGameId;
    if (ref == kWorkspaceInstance) return kWorkspaceId;
    const auto [it, added] = s.ids.try_emplace(ref, s.nextId);
    if (added) {
        s.refs[s.nextId] = ref;
        ++s.nextId;
    }
    return it->second;
}

std::vector<uint8_t> encodeRecord(ECS& ecs, State& s, InstanceRef ref, InstanceRef parent,
                                  const std::unordered_set<InstanceRef>& inTree) {
    std::vector<uint8_t> bytes;
    Writer w(bytes);
    const std::string cls = instances::className(ecs, ref);
    w.u32(assignId(s, ref));
    w.u32(assignId(s, parent));
    w.str(cls);
    w.str(instances::name(ecs, ref));
    w.u32(cls == "Player" ? remotenet::netIdFor(ecs, ref) : 0);

    std::vector<uint8_t> props;
    Writer pw(props);
    uint16_t count = 0;
    for (const PropertyDef* def : replicatedProperties(cls)) {
        InstanceValue value;
        if (!instances::getProperty(ecs, ref, *def, value)) continue;
        uint32_t refId = 0;
        if (value.type == InstanceValue::Type::Instance) {
            // Pointing at something the client can't see: leave it out.
            if (inTree.count(value.ref) == 0) continue;
            refId = assignId(s, value.ref);
        }
        pw.str(def->name);
        writeValue(pw, value, refId);
        ++count;
    }
    w.u16(count);
    bytes.insert(bytes.end(), props.begin(), props.end());

    const auto attrs = instances::attributes(ecs, ref);
    w.u16(static_cast<uint16_t>(attrs.size()));
    for (const auto& [name, value] : attrs) {
        w.str(name);
        writeValue(w, value, 0);
    }
    const auto tags = instances::tags(ecs, ref);
    w.u16(static_cast<uint16_t>(tags.size()));
    for (const std::string& tag : tags) w.str(tag);
    return bytes;
}

void sendRecords(ECS& ecs, uint32_t target, const std::vector<const std::vector<uint8_t>*>& records) {
    std::vector<uint8_t> message;
    uint32_t count = 0;
    auto flush = [&] {
        if (count == 0) return;
        std::memcpy(message.data() + 1, &count, sizeof count);
        remotenet::sendRaw(ecs, target, std::move(message), true);
        message.clear();
        count = 0;
    };
    for (const auto* record : records) {
        if (count > 0 && message.size() + record->size() > kChunkBytes) flush();
        if (message.empty()) message = {static_cast<uint8_t>(Kind::Records), 0, 0, 0, 0};
        message.insert(message.end(), record->begin(), record->end());
        ++count;
    }
    flush();
}

void sendIds(ECS& ecs, uint32_t target, Kind kind, const std::vector<uint32_t>& ids) {
    std::vector<uint8_t> message;
    Writer w(message);
    w.u8(static_cast<uint8_t>(kind));
    w.u32(static_cast<uint32_t>(ids.size()));
    for (uint32_t id : ids) w.u32(id);
    remotenet::sendRaw(ecs, target, std::move(message), true);
}

void serverTick(ECS& ecs, State& s, double dt) {
    s.sinceSend += dt;
    if (s.clients.empty() && s.newClients.empty()) {
        s.sent.clear();
        return;
    }
    if (s.sinceSend < 1.0 / kSendRate && s.newClients.empty()) return;
    s.sinceSend = 0.0;

    const auto tree = replicatedTree(ecs);
    std::unordered_set<InstanceRef> inTree{kGameInstance, kWorkspaceInstance};
    for (const auto& [ref, parent] : tree) inTree.insert(ref);

    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> current;
    current.reserve(tree.size());
    for (const auto& [ref, parent] : tree) {
        current.emplace_back(assignId(s, ref), encodeRecord(ecs, s, ref, parent, inTree));
    }

    if (!s.clients.empty()) {
        std::vector<const std::vector<uint8_t>*> changed;
        std::unordered_set<uint32_t> live;
        for (const auto& [id, bytes] : current) {
            live.insert(id);
            const auto found = s.sent.find(id);
            if (found == s.sent.end() || found->second != bytes) changed.push_back(&bytes);
        }
        std::vector<uint32_t> gone;
        for (const auto& [id, bytes] : s.sent) {
            if (live.count(id) == 0) gone.push_back(id);
        }
        for (uint32_t client : s.clients) {
            sendRecords(ecs, client, changed);
            if (!gone.empty()) sendIds(ecs, client, Kind::Destroy, gone);
        }
    }
    if (!s.newClients.empty()) {
        std::vector<const std::vector<uint8_t>*> all;
        all.reserve(current.size());
        for (const auto& [id, bytes] : current) all.push_back(&bytes);
        for (uint32_t client : s.newClients) {
            sendRecords(ecs, client, all);
            sendIds(ecs, client, Kind::SnapshotDone, {});
            s.clients.push_back(client);
        }
        s.newClients.clear();
    }

    s.sent.clear();
    for (auto& [id, bytes] : current) s.sent.emplace(id, std::move(bytes));
    for (auto it = s.ids.begin(); it != s.ids.end();) {
        if (instances::isAlive(ecs, it->first)) {
            ++it;
            continue;
        }
        s.refs.erase(it->second);
        it = s.ids.erase(it);
    }
}

// ---- Client

bool readRecord(Reader& r, Record& out) {
    out = Record{};
    out.id = r.u32();
    out.parentId = r.u32();
    out.className = r.str();
    out.name = r.str();
    out.playerNetId = r.u32();
    const uint16_t propCount = r.u16();
    for (uint16_t i = 0; i < propCount && r.ok(); ++i) {
        Property p;
        p.name = r.str();
        if (!readValue(r, p.value, p.refId)) return false;
        out.properties.push_back(std::move(p));
    }
    const uint16_t attrCount = r.u16();
    for (uint16_t i = 0; i < attrCount && r.ok(); ++i) {
        std::string name = r.str();
        InstanceValue value;
        uint32_t unused = 0;
        if (!readValue(r, value, unused)) return false;
        out.attributes.emplace_back(std::move(name), std::move(value));
    }
    const uint16_t tagCount = r.u16();
    for (uint16_t i = 0; i < tagCount && r.ok(); ++i) out.tags.push_back(r.str());
    return r.ok() && out.id >= kFirstId;
}

InstanceRef lookup(ECS& ecs, State& s, uint32_t id) {
    if (id == kGameId) return kGameInstance;
    if (id == kWorkspaceId) return kWorkspaceInstance;
    const auto found = s.refs.find(id);
    if (found == s.refs.end() || !instances::isAlive(ecs, found->second)) return kNoInstance;
    return found->second;
}

void bind(State& s, uint32_t id, InstanceRef ref) {
    s.refs[id] = ref;
    s.ids[ref] = id;
}

// What the client loaded from the place file. Whatever of it the server
// doesn't send in its first copy was removed on the server, so it goes.
void takeCandidates(ECS& ecs, State& s) {
    s.candidatesTaken = true;
    const InstanceRef playersService = instances::findService(ecs, "Players");
    for (const auto& [ref, parent] : replicatedTree(ecs)) {
        if (parent == kGameInstance) continue;
        if (playersService != kNoInstance && instances::isDescendantOf(ecs, ref, playersService)) continue;
        s.candidates.push_back(ref);
    }
}

InstanceRef findMatch(ECS& ecs, State& s, InstanceRef parent, const Record& record) {
    if (record.playerNetId != 0) return remotenet::playerFor(ecs, record.playerNetId);
    if (parent == kGameInstance) return instances::findService(ecs, record.className);
    for (InstanceRef child : instances::children(ecs, parent)) {
        if (s.ids.count(child) != 0 || !replicates(ecs, child)) continue;
        if (instances::className(ecs, child) == record.className && instances::name(ecs, child) == record.name) {
            return child;
        }
    }
    return kNoInstance;
}

// False when it has to wait (its parent or Player hasn't arrived yet).
bool apply(ECS& ecs, State& s, const Record& record) {
    const InstanceRef parent = lookup(ecs, s, record.parentId);
    if (parent == kNoInstance) return false;
    InstanceRef ref = lookup(ecs, s, record.id);
    if (ref == kNoInstance) {
        ref = findMatch(ecs, s, parent, record);
        if (ref == kNoInstance) {
            if (record.playerNetId != 0) return false;
            std::string error;
            ref = parent == kGameInstance ? instances::getService(ecs, record.className, error)
                                          : instances::createUnchecked(ecs, record.className);
            if (ref == kNoInstance) {
                logWarn("Replication", "can't make a %s here; skipped", record.className.c_str());
                return true;
            }
        }
        bind(s, record.id, ref);
    }

    if (instances::name(ecs, ref) != record.name) instances::setName(ecs, ref, record.name);
    if (parent != kGameInstance && instances::parent(ecs, ref) != parent) {
        std::string error;
        if (!instances::setParent(ecs, ref, parent, error)) {
            logWarn("Replication", "couldn't parent %s: %s", record.name.c_str(), error.c_str());
        }
    }
    for (const Property& p : record.properties) {
        const PropertyDef* def = instances::findProperty(record.className, p.name);
        if (def == nullptr) continue;
        InstanceValue value = p.value;
        if (value.type == InstanceValue::Type::Instance) {
            const InstanceRef target = lookup(ecs, s, p.refId);
            if (target == kNoInstance) continue;
            value = InstanceValue::ofInstance(target);
        }
        InstanceValue current;
        if (instances::getProperty(ecs, ref, *def, current) && same(current, value)) continue;
        instances::setProperty(ecs, ref, *def, value);
    }
    for (const auto& [name, value] : record.attributes) {
        const InstanceValue* current = instances::attribute(ecs, ref, name);
        if (current == nullptr || !same(*current, value)) instances::setAttribute(ecs, ref, name, value);
    }
    for (const auto& [name, value] : instances::attributes(ecs, ref)) {
        const bool kept = std::any_of(record.attributes.begin(), record.attributes.end(),
                                      [&](const auto& a) { return a.first == name; });
        if (!kept) instances::setAttribute(ecs, ref, name, InstanceValue{});
    }
    for (const std::string& tag : instances::tags(ecs, ref)) {
        if (std::find(record.tags.begin(), record.tags.end(), tag) == record.tags.end()) instances::removeTag(ecs, ref, tag);
    }
    for (const std::string& tag : record.tags) instances::addTag(ecs, ref, tag);
    return true;
}

void retryPending(ECS& ecs, State& s) {
    bool progress = true;
    while (progress && !s.pending.empty()) {
        progress = false;
        for (auto it = s.pending.begin(); it != s.pending.end();) {
            if (apply(ecs, s, it->record)) {
                it = s.pending.erase(it);
                progress = true;
            } else {
                ++it;
            }
        }
    }
}

void queue(State& s, Record record) {
    for (PendingRecord& p : s.pending) {
        if (p.record.id == record.id) {
            p.record = std::move(record);
            return;
        }
    }
    s.pending.push_back(PendingRecord{std::move(record), 0.0});
}

void finishSnapshot(ECS& ecs, State& s) {
    retryPending(ecs, s);
    for (InstanceRef ref : s.candidates) {
        if (s.ids.count(ref) == 0 && instances::isAlive(ecs, ref)) instances::destroy(ecs, ref);
    }
    s.candidates.clear();
    s.snapshotDone = true;
    signals::runService(ecs).awaitingReplication = false;
}

void clientReceive(ECS& ecs, const uint8_t* data, size_t size) {
    State& s = stateOf(ecs);
    if (!s.active || s.server) return;
    Reader r(data, size);
    const auto kind = static_cast<Kind>(r.u8());
    const uint32_t count = r.u32();
    if (!r.ok()) return;
    if (kind == Kind::Records) {
        if (!s.candidatesTaken) takeCandidates(ecs, s);
        for (uint32_t i = 0; i < count; ++i) {
            Record record;
            if (!readRecord(r, record)) {
                logWarn("Replication", "dropped a damaged update");
                return;
            }
            if (!apply(ecs, s, record)) queue(s, std::move(record));
        }
        retryPending(ecs, s);
    } else if (kind == Kind::Destroy) {
        for (uint32_t i = 0; i < count && r.ok(); ++i) {
            const uint32_t id = r.u32();
            s.pending.erase(std::remove_if(s.pending.begin(), s.pending.end(),
                                           [&](const PendingRecord& p) { return p.record.id == id; }),
                            s.pending.end());
            const InstanceRef ref = lookup(ecs, s, id);
            s.refs.erase(id);
            if (ref == kNoInstance) continue;
            s.ids.erase(ref);
            instances::destroy(ecs, ref);
        }
    } else if (kind == Kind::SnapshotDone) {
        if (!s.candidatesTaken) takeCandidates(ecs, s);
        finishSnapshot(ecs, s);
    }
}

void clientTick(ECS& ecs, State& s, double dt) {
    if (!s.snapshotDone) {
        s.waited += dt;
        if (s.waited > kClientWaitSeconds && signals::runService(ecs).awaitingReplication) {
            logWarn("Replication", "no copy of the game arrived from the server; starting LocalScripts anyway");
            signals::runService(ecs).awaitingReplication = false;
        }
    }
    if (s.pending.empty()) return;
    retryPending(ecs, s);
    for (auto it = s.pending.begin(); it != s.pending.end();) {
        it->age += dt;
        if (it->age < kPendingGiveUpSeconds) {
            ++it;
            continue;
        }
        logWarn("Replication", "gave up on %s (its parent never arrived)", it->record.name.c_str());
        it = s.pending.erase(it);
    }
}

} // namespace

void startServer(ECS& ecs) {
    State& s = stateOf(ecs);
    s = State{};
    s.active = true;
    s.server = true;
    remotenet::setBeforeServerSend(ecs, [&ecs] {
        State& state = stateOf(ecs);
        if (!state.active || !state.server) return;
        state.sinceSend = 1.0;
        serverTick(ecs, state, 0.0);
    });
}

void startClient(ECS& ecs) {
    State& s = stateOf(ecs);
    s = State{};
    s.active = true;
    signals::runService(ecs).awaitingReplication = true;
    remotenet::setRawReceiver(ecs, [&ecs](uint32_t, const uint8_t* data, size_t size) { clientReceive(ecs, data, size); });
}

void stop(ECS& ecs) {
    stateOf(ecs) = State{};
    signals::runService(ecs).awaitingReplication = false;
    remotenet::setRawReceiver(ecs, nullptr);
    remotenet::setBeforeServerSend(ecs, nullptr);
}

bool active(ECS& ecs) { return stateOf(ecs).active; }

void addClient(ECS& ecs, uint32_t netId) {
    State& s = stateOf(ecs);
    if (!s.active || !s.server || netId == 0) return;
    if (std::find(s.clients.begin(), s.clients.end(), netId) != s.clients.end()) return;
    if (std::find(s.newClients.begin(), s.newClients.end(), netId) != s.newClients.end()) return;
    s.newClients.push_back(netId);
}

void removeClient(ECS& ecs, uint32_t netId) {
    State& s = stateOf(ecs);
    s.clients.erase(std::remove(s.clients.begin(), s.clients.end(), netId), s.clients.end());
    s.newClients.erase(std::remove(s.newClients.begin(), s.newClients.end(), netId), s.newClients.end());
}

void tick(ECS& ecs, double dt) {
    State& s = stateOf(ecs);
    if (!s.active) return;
    if (s.server) serverTick(ecs, s, dt);
    else clientTick(ecs, s, dt);
}

bool snapshotApplied(ECS& ecs) { return stateOf(ecs).snapshotDone; }

uint32_t idOf(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance) return kGameId;
    if (ref == kWorkspaceInstance) return kWorkspaceId;
    const State& s = stateOf(ecs);
    const auto found = s.ids.find(ref);
    return found == s.ids.end() ? 0 : found->second;
}

InstanceRef instanceFor(ECS& ecs, uint32_t id) { return lookup(ecs, stateOf(ecs), id); }

} // namespace engine::core::replication
