#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/PhysicsRollback.hpp"

namespace engine::net {

using core::kMaxRollbackPlayers;

// One peer's inputs for a run of frames plus what it has heard from
// everyone else. Each packet repeats every input some peer has not
// acknowledged yet, so packets can go unreliable and still never lose one.
struct RollbackPacket {
    uint8_t player = 0;
    uint8_t players = 0;
    int8_t advantage = 0;  // sender's frames ahead of the slowest peer it has heard from
    uint32_t startFrame = 0;
    std::vector<core::RollbackInput> inputs;
    std::array<uint32_t, kMaxRollbackPlayers> received{}; // per player: frames of input the sender holds, from 0
    uint32_t checksumFrame = 0; // 0 when there is no checksum yet
    uint64_t checksum = 0;

    static constexpr uint8_t kType = 0xB7;
    static constexpr size_t kMaxInputs = 64;
};

[[nodiscard]] std::vector<uint8_t> encodeRollbackPacket(const RollbackPacket& packet);
[[nodiscard]] bool decodeRollbackPacket(const uint8_t* data, size_t size, RollbackPacket& out);

// Runs the input exchange for one PhysicsRollback peer over any transport:
// call tick() once per frame and send the packet it returns to the other
// peers (directly or through a relay), and hand every packet received to
// receive().
class RollbackPeer {
public:
    explicit RollbackPeer(core::PhysicsRollback& rollback);

    // Advances one frame unless that would run too far ahead (of the
    // rollback window, or of slower peers by more than a frame, which
    // keeps every peer at about the same frame). Returns whether it did.
    bool tick(const core::RollbackInput& localInput);
    [[nodiscard]] std::vector<uint8_t> buildPacket() const;
    // Returns false for packets that aren't for this session.
    bool receive(const uint8_t* data, size_t size);

    [[nodiscard]] int advantage() const;
    [[nodiscard]] uint32_t waitedFrames() const { return waitedFrames_; }
    [[nodiscard]] uint32_t received(uint32_t player) const { return received_[player]; }
    [[nodiscard]] core::PhysicsRollback& rollback() { return rollback_; }

private:
    core::PhysicsRollback& rollback_;
    uint32_t players_;
    uint32_t local_;
    std::vector<core::RollbackInput> localInputs_; // from frame localBase_
    uint32_t localBase_ = 0;
    std::array<uint32_t, kMaxRollbackPlayers> received_{};
    std::array<uint32_t, kMaxRollbackPlayers> ackedByPeer_{}; // how much of our input each peer has
    std::array<int64_t, kMaxRollbackPlayers> remoteFrame_{};
    std::array<int, kMaxRollbackPlayers> remoteAdvantage_{};
    uint32_t waitedFrames_ = 0;
    uint32_t sinceWait_ = 0;
};

} // namespace engine::net
