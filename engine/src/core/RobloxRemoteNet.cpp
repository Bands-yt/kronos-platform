#include "core/RobloxRemoteNet.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "core/Logger.hpp"
#include "core/RobloxWire.hpp"

namespace engine::core::remotenet {
namespace {

enum class Kind : uint8_t { Fire = 1, Invoke = 2, Result = 3 };

enum class Tag : uint8_t {
    Nil = 0,
    Bool,
    Number,
    String,
    Vector3,
    CFrame,
    Color3,
    BrickColor,
    Enum,
    Instance,
    Player,
    Table,
    Vector2,
    UDim,
    UDim2,
};

constexpr int kMaxDepth = 32;

struct Outgoing {
    uint64_t hubId = 0;
    uint32_t target = 0;
};

struct Incoming {
    uint64_t hubId = 0;
    uint32_t sender = 0;
    uint32_t callId = 0;
};

struct LinkState {
    bool attached = false;
    Side side = Side::Server;
    SendFn send;
    std::unordered_map<uint32_t, InstanceRef> players;
    std::unordered_map<InstanceRef, uint32_t> netIds;
    std::unordered_map<uint32_t, Outgoing> outgoing;
    std::vector<Incoming> incoming;
    std::unordered_map<uint32_t, double> budget;
    uint32_t nextCallId = 1;
    RawReceiver rawReceiver;
    std::function<void()> beforeServerSend;
    bool inBeforeSend = false;
};

LinkState& link(ECS& ecs) { return ecs.raw().ctx().emplace<LinkState>(); }

using wire::Reader;
using wire::Writer;

// A script's remote message. On the server, object changes go out first, so
// an instance made just before FireClient exists when the event arrives.
void deliver(LinkState& state, uint32_t target, std::vector<uint8_t> bytes, bool reliable) {
    if (state.side == Side::Server && state.beforeServerSend && !state.inBeforeSend) {
        state.inBeforeSend = true;
        state.beforeServerSend();
        state.inBeforeSend = false;
    }
    state.send(target, std::move(bytes), reliable);
}

// Names from game down, each with its index among same-named siblings.
bool pathOf(ECS& ecs, InstanceRef ref, std::vector<std::pair<std::string, uint16_t>>& path) {
    std::vector<InstanceRef> chain;
    InstanceRef at = ref;
    while (at != kGameInstance) {
        if (at == kNoInstance || !instances::isAlive(ecs, at) || chain.size() > 256) return false;
        chain.push_back(at);
        at = instances::parent(ecs, at);
    }
    path.clear();
    InstanceRef parent = kGameInstance;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const std::string name = instances::name(ecs, *it);
        uint16_t index = 0;
        for (InstanceRef sibling : instances::children(ecs, parent)) {
            if (sibling == *it) break;
            if (instances::name(ecs, sibling) == name) ++index;
        }
        path.emplace_back(name, index);
        parent = *it;
    }
    return true;
}

InstanceRef resolvePath(ECS& ecs, const std::vector<std::pair<std::string, uint16_t>>& path) {
    InstanceRef at = kGameInstance;
    for (const auto& [name, index] : path) {
        InstanceRef found = kNoInstance;
        uint16_t seen = 0;
        for (InstanceRef child : instances::children(ecs, at)) {
            if (instances::name(ecs, child) != name) continue;
            if (seen++ == index) {
                found = child;
                break;
            }
        }
        if (found == kNoInstance) return kNoInstance;
        at = found;
    }
    return at;
}

void writeInstance(ECS& ecs, Writer& w, InstanceRef ref) {
    if (const uint32_t netId = netIdFor(ecs, ref); netId != 0) {
        w.u8(static_cast<uint8_t>(Tag::Player));
        w.u32(netId);
        return;
    }
    std::vector<std::pair<std::string, uint16_t>> path;
    if (!pathOf(ecs, ref, path)) {
        w.u8(static_cast<uint8_t>(Tag::Nil));
        return;
    }
    w.u8(static_cast<uint8_t>(Tag::Instance));
    w.u16(static_cast<uint16_t>(path.size()));
    for (const auto& [name, index] : path) {
        w.str(name);
        w.u16(index);
    }
}

void writeArg(ECS& ecs, Writer& w, const SignalArg& arg, int depth) {
    if (arg.isTable) {
        if (depth >= kMaxDepth) {
            w.u8(static_cast<uint8_t>(Tag::Nil));
            return;
        }
        w.u8(static_cast<uint8_t>(Tag::Table));
        w.u32(static_cast<uint32_t>(arg.keys.size()));
        for (size_t i = 0; i < arg.keys.size(); ++i) {
            writeArg(ecs, w, arg.keys[i], depth + 1);
            writeArg(ecs, w, arg.values[i], depth + 1);
        }
        return;
    }
    const InstanceValue& v = arg.value;
    using T = InstanceValue::Type;
    switch (v.type) {
    case T::Nil:
        w.u8(static_cast<uint8_t>(Tag::Nil));
        break;
    case T::Bool:
        w.u8(static_cast<uint8_t>(Tag::Bool));
        w.u8(v.boolean ? 1 : 0);
        break;
    case T::Number:
        w.u8(static_cast<uint8_t>(Tag::Number));
        w.f64(v.number);
        break;
    case T::String:
        w.u8(static_cast<uint8_t>(Tag::String));
        w.str(v.text);
        break;
    case T::Vector3:
        w.u8(static_cast<uint8_t>(Tag::Vector3));
        w.vec3(v.vec);
        break;
    case T::CFrame:
        w.u8(static_cast<uint8_t>(Tag::CFrame));
        w.vec3(v.vec);
        w.f32(v.rot.w);
        w.f32(v.rot.x);
        w.f32(v.rot.y);
        w.f32(v.rot.z);
        break;
    case T::Color3:
    case T::BrickColor:
        w.u8(static_cast<uint8_t>(v.type == T::Color3 ? Tag::Color3 : Tag::BrickColor));
        w.vec3(v.vec);
        break;
    case T::Enum:
        w.u8(static_cast<uint8_t>(Tag::Enum));
        w.str(v.enumType);
        w.str(v.text);
        w.f64(v.number);
        break;
    case T::Instance:
        writeInstance(ecs, w, v.ref);
        break;
    case T::Vector2:
    case T::UDim:
    case T::UDim2:
        w.u8(static_cast<uint8_t>(v.type == T::Vector2 ? Tag::Vector2 : v.type == T::UDim ? Tag::UDim : Tag::UDim2));
        w.vec3(v.vec);
        w.f64(v.number);
        break;
    }
}

bool readArg(ECS& ecs, Reader& r, SignalArg& out, int depth) {
    if (depth > kMaxDepth) return false;
    const auto tag = static_cast<Tag>(r.u8());
    out = SignalArg{};
    switch (tag) {
    case Tag::Nil:
        break;
    case Tag::Bool:
        out.value = InstanceValue::ofBool(r.u8() != 0);
        break;
    case Tag::Number:
        out.value = InstanceValue::ofNumber(r.f64());
        break;
    case Tag::String:
        out.value = InstanceValue::ofString(r.str());
        break;
    case Tag::Vector3:
        out.value = InstanceValue::ofVector3(r.vec3());
        break;
    case Tag::CFrame: {
        const glm::vec3 position = r.vec3();
        const float w = r.f32();
        const float x = r.f32();
        const float y = r.f32();
        const float z = r.f32();
        out.value = InstanceValue::ofCFrame(position, glm::quat(w, x, y, z));
        break;
    }
    case Tag::Color3:
        out.value = InstanceValue::ofColor3(r.vec3());
        break;
    case Tag::BrickColor:
        out.value = InstanceValue::ofBrickColor(r.vec3());
        break;
    case Tag::Enum: {
        std::string enumType = r.str();
        std::string item = r.str();
        out.value = InstanceValue::ofEnum(std::move(enumType), std::move(item), static_cast<int>(r.f64()));
        break;
    }
    case Tag::Instance: {
        const uint16_t count = r.u16();
        std::vector<std::pair<std::string, uint16_t>> path;
        for (uint16_t i = 0; i < count && r.ok(); ++i) {
            std::string name = r.str();
            const uint16_t index = r.u16();
            path.emplace_back(std::move(name), index);
        }
        if (r.ok()) out.value = InstanceValue::ofInstance(resolvePath(ecs, path));
        break;
    }
    case Tag::Player:
        out.value = InstanceValue::ofInstance(playerFor(ecs, r.u32()));
        break;
    case Tag::Vector2:
    case Tag::UDim:
    case Tag::UDim2: {
        const glm::vec3 v = r.vec3();
        const double last = r.f64();
        if (tag == Tag::Vector2) out.value = InstanceValue::ofVector2(v.x, v.y);
        if (tag == Tag::UDim) out.value = InstanceValue::ofUDim(v.x, v.y);
        if (tag == Tag::UDim2) out.value = InstanceValue::ofUDim2(v.x, v.y, v.z, static_cast<float>(last));
        break;
    }
    case Tag::Table: {
        const uint32_t count = r.u32();
        if (count > r.left() / 2) return false;
        out.isTable = true;
        for (uint32_t i = 0; i < count; ++i) {
            SignalArg key;
            SignalArg value;
            if (!readArg(ecs, r, key, depth + 1) || !readArg(ecs, r, value, depth + 1)) return false;
            // A key that didn't arrive (an unknown instance) drops its entry.
            if (key.isTable || key.value.type == InstanceValue::Type::Nil) continue;
            out.keys.push_back(std::move(key));
            out.values.push_back(std::move(value));
        }
        break;
    }
    default:
        return false;
    }
    return r.ok();
}

void writeArgs(ECS& ecs, Writer& w, const std::vector<SignalArg>& args) {
    w.u16(static_cast<uint16_t>(std::min<size_t>(args.size(), 0xFFFF)));
    for (size_t i = 0; i < args.size() && i < 0xFFFF; ++i) writeArg(ecs, w, args[i], 0);
}

bool readArgs(ECS& ecs, Reader& r, std::vector<SignalArg>& out) {
    const uint16_t count = r.u16();
    if (!r.ok() || count > r.left()) return false;
    out.clear();
    out.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        SignalArg arg;
        if (!readArg(ecs, r, arg, 0)) return false;
        out.push_back(std::move(arg));
    }
    return true;
}

std::vector<uint8_t> callMessage(ECS& ecs, Kind kind, uint32_t callId, InstanceRef remote,
                                 const std::vector<SignalArg>& args) {
    std::vector<uint8_t> bytes;
    Writer w(bytes);
    w.u8(static_cast<uint8_t>(kind));
    if (kind == Kind::Invoke) w.u32(callId);
    writeInstance(ecs, w, remote);
    writeArgs(ecs, w, args);
    return bytes;
}

std::vector<uint8_t> resultMessage(ECS& ecs, uint32_t callId, bool ok, const std::vector<SignalArg>& values,
                                   const std::string& error) {
    std::vector<uint8_t> bytes;
    Writer w(bytes);
    w.u8(static_cast<uint8_t>(Kind::Result));
    w.u32(callId);
    w.u8(ok ? 1 : 0);
    if (ok) {
        writeArgs(ecs, w, values);
    } else {
        w.str(error);
    }
    return bytes;
}

bool tooLarge(const std::vector<uint8_t>& bytes, std::string& error) {
    if (bytes.size() <= kMaxPayloadBytes) return false;
    error = "Remote payload is too large (" + std::to_string(bytes.size()) + " bytes, the limit is " +
            std::to_string(kMaxPayloadBytes) + ")";
    return true;
}

bool ready(ECS& ecs, Side side, const char* method, std::string& error) {
    LinkState& state = link(ecs);
    if (!state.attached || state.side != side || !state.send) {
        error = std::string(method) + ": not connected";
        return false;
    }
    return true;
}

bool sendFire(ECS& ecs, uint32_t target, InstanceRef remote, const std::vector<SignalArg>& args, std::string& error) {
    std::vector<uint8_t> bytes = callMessage(ecs, Kind::Fire, 0, remote, args);
    if (tooLarge(bytes, error)) return false;
    const bool unreliable = instances::className(ecs, remote) == "UnreliableRemoteEvent";
    if (unreliable && bytes.size() > kMaxUnreliableBytes) {
        logWarn("Remotes", "UnreliableRemoteEvent %s: %zu bytes is over the %zu byte limit; dropped",
                instances::name(ecs, remote).c_str(), bytes.size(), kMaxUnreliableBytes);
        return true;
    }
    deliver(link(ecs), target, std::move(bytes), !unreliable);
    return true;
}

uint64_t sendInvoke(ECS& ecs, uint32_t target, InstanceRef remote, const std::vector<SignalArg>& args,
                    std::string& error) {
    LinkState& state = link(ecs);
    const uint32_t callId = state.nextCallId++;
    std::vector<uint8_t> bytes = callMessage(ecs, Kind::Invoke, callId, remote, args);
    if (tooLarge(bytes, error)) return 0;
    const uint64_t hubId = signals::hubFor(ecs)->beginInvoke();
    state.outgoing[callId] = Outgoing{hubId, target};
    deliver(state, target, std::move(bytes), true);
    return hubId;
}

void receiveCall(ECS& ecs, LinkState& state, Kind kind, uint32_t sender, Reader& r) {
    const uint32_t callId = kind == Kind::Invoke ? r.u32() : 0;
    SignalArg remoteArg;
    std::vector<SignalArg> args;
    if (!readArg(ecs, r, remoteArg, 0) || !readArgs(ecs, r, args)) return;
    const bool server = state.side == Side::Server;
    InstanceRef player = kNoInstance;
    if (server) {
        player = playerFor(ecs, sender);
        if (player == kNoInstance) return;
        args.insert(args.begin(), SignalArg::of(InstanceValue::ofInstance(player)));
    }
    const InstanceRef remote = remoteArg.value.type == InstanceValue::Type::Instance ? remoteArg.value.ref : kNoInstance;
    const std::string cls = remote == kNoInstance ? std::string() : instances::className(ecs, remote);
    auto hub = signals::hubFor(ecs);
    if (kind == Kind::Fire) {
        if (!instances::classIsA(cls, "BaseRemoteEvent")) {
            logWarn("Remotes", "a remote event from the %s wasn't found here", server ? "client" : "server");
            return;
        }
        hub->fire(remote, server ? "OnServerEvent" : "OnClientEvent", std::move(args));
        return;
    }
    if (!instances::classIsA(cls, "RemoteFunction")) {
        deliver(state, sender, resultMessage(ecs, callId, false, {}, "RemoteFunction not found"), true);
        return;
    }
    const uint64_t hubId = hub->invoke(remote, server ? "OnServerInvoke" : "OnClientInvoke", std::move(args));
    state.incoming.push_back(Incoming{hubId, sender, callId});
}

void receiveResult(ECS& ecs, LinkState& state, uint32_t sender, Reader& r) {
    const uint32_t callId = r.u32();
    const bool ok = r.u8() != 0;
    std::vector<SignalArg> values;
    std::string error;
    if (ok ? !readArgs(ecs, r, values) : (error = r.str(), !r.ok())) return;
    const auto found = state.outgoing.find(callId);
    // Only the client that was asked may answer.
    if (found == state.outgoing.end() || (state.side == Side::Server && found->second.target != sender)) return;
    signals::hubFor(ecs)->finishInvoke(found->second.hubId, ok, std::move(values), std::move(error));
    state.outgoing.erase(found);
}

} // namespace

void attach(ECS& ecs, Side side, SendFn send) {
    detach(ecs);
    LinkState& state = link(ecs);
    state.attached = true;
    state.side = side;
    state.send = std::move(send);
}

void detach(ECS& ecs) {
    LinkState& state = link(ecs);
    if (!state.outgoing.empty()) {
        auto hub = signals::hubFor(ecs);
        for (const auto& [callId, call] : state.outgoing) {
            hub->finishInvoke(call.hubId, false, {}, "The connection was lost");
        }
    }
    state = LinkState{};
}

bool attached(ECS& ecs) { return link(ecs).attached; }

void setRawReceiver(ECS& ecs, RawReceiver receiver) { link(ecs).rawReceiver = std::move(receiver); }

void setBeforeServerSend(ECS& ecs, std::function<void()> hook) { link(ecs).beforeServerSend = std::move(hook); }

void sendRaw(ECS& ecs, uint32_t target, std::vector<uint8_t> bytes, bool reliable) {
    LinkState& state = link(ecs);
    if (state.attached && state.send) state.send(target, std::move(bytes), reliable);
}

bool isServer(ECS& ecs) {
    const LinkState& state = link(ecs);
    return state.attached && state.side == Side::Server;
}

void addPlayer(ECS& ecs, uint32_t netId, InstanceRef player) {
    if (netId == 0 || player == kNoInstance) return;
    LinkState& state = link(ecs);
    state.players[netId] = player;
    state.netIds[player] = netId;
    state.budget[netId] = kServerReceiveBurst;
}

void removePlayer(ECS& ecs, uint32_t netId) {
    LinkState& state = link(ecs);
    if (const auto found = state.players.find(netId); found != state.players.end()) {
        state.netIds.erase(found->second);
        state.players.erase(found);
    }
    state.budget.erase(netId);
    for (auto it = state.outgoing.begin(); it != state.outgoing.end();) {
        if (it->second.target != netId) {
            ++it;
            continue;
        }
        signals::hubFor(ecs)->finishInvoke(it->second.hubId, false, {}, "Player has left the game");
        it = state.outgoing.erase(it);
    }
}

InstanceRef playerFor(ECS& ecs, uint32_t netId) {
    const LinkState& state = link(ecs);
    const auto found = state.players.find(netId);
    return found == state.players.end() || !instances::isAlive(ecs, found->second) ? kNoInstance : found->second;
}

uint32_t netIdFor(ECS& ecs, InstanceRef player) {
    const LinkState& state = link(ecs);
    const auto found = state.netIds.find(player);
    return found == state.netIds.end() ? 0 : found->second;
}

void receive(ECS& ecs, uint32_t sender, const uint8_t* data, size_t size) {
    LinkState& state = link(ecs);
    if (!state.attached || data == nullptr || size == 0 || size > kMaxPayloadBytes + 64) return;
    if (state.side == Side::Server) {
        auto budget = state.budget.find(sender);
        if (budget == state.budget.end()) return;
        if (budget->second < 1.0) {
            logWarn("Remotes", "player %u is sending remote calls too fast; dropped one", sender);
            return;
        }
        budget->second -= 1.0;
    } else {
        sender = 0;
    }
    if (data[0] >= kFirstRawKind) {
        if (state.side == Side::Client && state.rawReceiver) state.rawReceiver(sender, data, size);
        return;
    }
    Reader r(data, size);
    const auto kind = static_cast<Kind>(r.u8());
    if (kind == Kind::Fire || kind == Kind::Invoke) {
        receiveCall(ecs, state, kind, sender, r);
    } else if (kind == Kind::Result) {
        receiveResult(ecs, state, sender, r);
    }
}

void tick(ECS& ecs, double dt) {
    LinkState& state = link(ecs);
    if (!state.attached) return;
    for (auto& [netId, tokens] : state.budget) {
        tokens = std::min(kServerReceiveBurst, tokens + kServerReceivePerSecond * dt);
    }
    if (state.incoming.empty()) return;
    auto hub = signals::hubFor(ecs);
    for (auto it = state.incoming.begin(); it != state.incoming.end();) {
        if (!hub->invokeDone(it->hubId)) {
            ++it;
            continue;
        }
        SignalHub::InvokeResult result = hub->takeInvoke(it->hubId);
        const bool senderGone = state.side == Side::Server && state.players.count(it->sender) == 0;
        if (!senderGone) {
            std::vector<uint8_t> bytes = resultMessage(ecs, it->callId, result.ok, result.values, result.error);
            std::string error;
            if (tooLarge(bytes, error)) bytes = resultMessage(ecs, it->callId, false, {}, error);
            deliver(state, it->sender, std::move(bytes), true);
        }
        it = state.incoming.erase(it);
    }
}

// Like Roblox, events fired with no connection are dropped, not errors.
bool fireServer(ECS& ecs, InstanceRef remote, const std::vector<SignalArg>& args, std::string& error) {
    if (!ready(ecs, Side::Client, "FireServer", error)) return true;
    return sendFire(ecs, kEveryone, remote, args, error);
}

bool fireClient(ECS& ecs, InstanceRef remote, InstanceRef player, const std::vector<SignalArg>& args,
                std::string& error) {
    if (!ready(ecs, Side::Server, "FireClient", error)) return true;
    const uint32_t netId = netIdFor(ecs, player);
    if (netId == 0) {
        error = "FireClient: " + instances::name(ecs, player) + " isn't connected";
        return false;
    }
    return sendFire(ecs, netId, remote, args, error);
}

bool fireAllClients(ECS& ecs, InstanceRef remote, const std::vector<SignalArg>& args, std::string& error) {
    if (!ready(ecs, Side::Server, "FireAllClients", error)) return true;
    return sendFire(ecs, kEveryone, remote, args, error);
}

uint64_t invokeServer(ECS& ecs, InstanceRef remote, const std::vector<SignalArg>& args, std::string& error) {
    if (!ready(ecs, Side::Client, "InvokeServer", error)) return 0;
    return sendInvoke(ecs, kEveryone, remote, args, error);
}

uint64_t invokeClient(ECS& ecs, InstanceRef remote, InstanceRef player, const std::vector<SignalArg>& args,
                      std::string& error) {
    if (!ready(ecs, Side::Server, "InvokeClient", error)) return 0;
    const uint32_t netId = netIdFor(ecs, player);
    if (netId == 0) {
        error = "InvokeClient: " + instances::name(ecs, player) + " isn't connected";
        return 0;
    }
    return sendInvoke(ecs, netId, remote, args, error);
}

std::vector<uint8_t> encodeArgs(ECS& ecs, const std::vector<SignalArg>& args) {
    std::vector<uint8_t> bytes;
    Writer w(bytes);
    writeArgs(ecs, w, args);
    return bytes;
}

bool decodeArgs(ECS& ecs, const uint8_t* data, size_t size, std::vector<SignalArg>& out) {
    Reader r(data, size);
    return readArgs(ecs, r, out);
}

} // namespace engine::core::remotenet
