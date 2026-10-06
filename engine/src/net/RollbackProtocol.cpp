#include "net/RollbackProtocol.hpp"

#include <algorithm>
#include <cstring>

namespace engine::net {

namespace {

template <typename T>
void put(std::vector<uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
bool get(const uint8_t* data, size_t size, size_t& at, T& out) {
    if (at + sizeof(T) > size) return false;
    std::memcpy(&out, data + at, sizeof(T));
    at += sizeof(T);
    return true;
}

constexpr uint32_t kWaitInterval = 3; // wait at most once per this many frames, so slowing down stays smooth

} // namespace

std::vector<uint8_t> encodeRollbackPacket(const RollbackPacket& packet) {
    std::vector<uint8_t> out;
    const auto count = static_cast<uint8_t>(std::min(packet.inputs.size(), RollbackPacket::kMaxInputs));
    const uint8_t players = std::min<uint8_t>(packet.players, kMaxRollbackPlayers);
    out.reserve(16 + count * sizeof(core::RollbackInput) + players * 4 + 12);
    put(out, RollbackPacket::kType);
    put(out, packet.player);
    put(out, players);
    put(out, packet.advantage);
    put(out, packet.startFrame);
    put(out, count);
    for (uint8_t i = 0; i < count; ++i) put(out, packet.inputs[i]);
    for (uint8_t p = 0; p < players; ++p) put(out, packet.received[p]);
    put(out, packet.checksumFrame);
    put(out, packet.checksum);
    return out;
}

bool decodeRollbackPacket(const uint8_t* data, size_t size, RollbackPacket& out) {
    size_t at = 0;
    uint8_t type = 0;
    uint8_t count = 0;
    RollbackPacket packet;
    if (!get(data, size, at, type) || type != RollbackPacket::kType) return false;
    if (!get(data, size, at, packet.player) || !get(data, size, at, packet.players) ||
        !get(data, size, at, packet.advantage) || !get(data, size, at, packet.startFrame) ||
        !get(data, size, at, count)) {
        return false;
    }
    if (packet.players == 0 || packet.players > kMaxRollbackPlayers || packet.player >= packet.players ||
        count > RollbackPacket::kMaxInputs) {
        return false;
    }
    packet.inputs.resize(count);
    for (auto& input : packet.inputs) {
        if (!get(data, size, at, input)) return false;
    }
    for (uint8_t p = 0; p < packet.players; ++p) {
        if (!get(data, size, at, packet.received[p])) return false;
    }
    if (!get(data, size, at, packet.checksumFrame) || !get(data, size, at, packet.checksum) || at != size) return false;
    out = std::move(packet);
    return true;
}

RollbackPeer::RollbackPeer(core::PhysicsRollback& rollback)
    : rollback_(rollback), players_(rollback.settings().players), local_(rollback.settings().localPlayer) {
    remoteFrame_.fill(-1);
}

int RollbackPeer::advantage() const {
    int64_t slowest = -1;
    for (uint32_t p = 0; p < players_; ++p) {
        if (p == local_ || remoteFrame_[p] < 0) continue;
        slowest = slowest < 0 ? remoteFrame_[p] : std::min(slowest, remoteFrame_[p]);
    }
    if (slowest < 0) return 0;
    return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(rollback_.currentFrame()) - slowest, -127, 127));
}

bool RollbackPeer::tick(const core::RollbackInput& localInput) {
    ++sinceWait_;
    int ahead = 0;
    for (uint32_t p = 0; p < players_; ++p) {
        if (p == local_ || remoteFrame_[p] < 0) continue;
        const auto local = static_cast<int>(static_cast<int64_t>(rollback_.currentFrame()) - remoteFrame_[p]);
        ahead = std::max(ahead, (local - remoteAdvantage_[p]) / 2);
    }
    if (!rollback_.canAdvance() || (ahead >= 1 && sinceWait_ >= kWaitInterval)) {
        ++waitedFrames_;
        sinceWait_ = 0;
        return false;
    }
    if (!rollback_.advance(localInput)) {
        ++waitedFrames_;
        return false;
    }
    localInputs_.push_back(localInput);
    received_[local_] = rollback_.currentFrame();

    uint32_t acked = received_[local_];
    for (uint32_t p = 0; p < players_; ++p) {
        if (p != local_) acked = std::min(acked, ackedByPeer_[p]);
    }
    if (acked > localBase_) {
        const uint32_t drop = std::min<uint32_t>(acked - localBase_, static_cast<uint32_t>(localInputs_.size()));
        localInputs_.erase(localInputs_.begin(), localInputs_.begin() + drop);
        localBase_ += drop;
    }
    return true;
}

std::vector<uint8_t> RollbackPeer::buildPacket() const {
    RollbackPacket packet;
    packet.player = static_cast<uint8_t>(local_);
    packet.players = static_cast<uint8_t>(players_);
    packet.advantage = static_cast<int8_t>(advantage());
    // Our oldest input some peer still lacks; the window keeps it within kMaxInputs.
    packet.startFrame = localBase_;
    const size_t count = std::min(localInputs_.size(), RollbackPacket::kMaxInputs);
    packet.inputs.assign(localInputs_.begin(), localInputs_.begin() + static_cast<std::ptrdiff_t>(count));
    packet.received = received_;
    if (const auto latest = rollback_.latestChecksum()) {
        packet.checksumFrame = latest->first;
        packet.checksum = latest->second;
    }
    return encodeRollbackPacket(packet);
}

bool RollbackPeer::receive(const uint8_t* data, size_t size) {
    RollbackPacket packet;
    if (!decodeRollbackPacket(data, size, packet)) return false;
    if (packet.players != players_ || packet.player == local_) return false;
    const uint32_t p = packet.player;

    const int64_t end = static_cast<int64_t>(packet.startFrame) + static_cast<int64_t>(packet.inputs.size());
    remoteFrame_[p] = std::max(remoteFrame_[p], end);
    remoteAdvantage_[p] = packet.advantage;
    ackedByPeer_[p] = std::max(ackedByPeer_[p], packet.received[local_]);

    for (size_t i = 0; i < packet.inputs.size(); ++i) {
        const uint32_t frame = packet.startFrame + static_cast<uint32_t>(i);
        if (frame < received_[p]) continue;
        if (frame > received_[p]) break; // a gap; a later packet repeats it
        if (!rollback_.addRemoteInput(p, frame, packet.inputs[i]) && frame >= rollback_.currentFrame()) break;
        received_[p] = frame + 1;
    }
    if (packet.checksumFrame > 0) rollback_.addRemoteChecksum(packet.checksumFrame, packet.checksum);
    return true;
}

} // namespace engine::net
