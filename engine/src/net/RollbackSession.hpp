#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace engine::net {

// GGPO-style rollback for deterministic peer-to-peer simulations. Every
// peer runs the full simulation; remote inputs that have not arrived yet
// are predicted by repeating that player's last known input. When a real
// input arrives and differs from the prediction, the session restores the
// snapshot taken before that frame and resimulates up to the present.
//
// State must be copyable and the step function deterministic. Input must be
// equality-comparable and default-constructible (the default is "no input").
template <typename State, typename Input, uint32_t MaxPlayers = 4>
class RollbackSession {
public:
    using Inputs = std::array<Input, MaxPlayers>;
    using StepFn = std::function<void(State&, const Inputs&, uint32_t frame)>;

    struct Stats {
        uint64_t rollbacks = 0;
        uint64_t resimulatedFrames = 0;
        uint64_t mispredictions = 0;
    };

    RollbackSession(const State& initial, uint32_t playerCount, uint32_t localPlayer, StepFn step,
                    uint32_t maxRollbackFrames = 8)
        : playerCount_(std::clamp<uint32_t>(playerCount, 1, MaxPlayers)),
          localPlayer_(localPlayer),
          window_(std::max<uint32_t>(maxRollbackFrames, 1)),
          step_(std::move(step)),
          state_(initial),
          snapshots_(window_ + 2),
          inputs_(playerCount_, std::vector<Slot>((window_ + 2) * 2)) {
        snapshotAt(0) = {0, initial};
        lastConfirmedInput_.fill(Input{});
        for (uint32_t p = 0; p < playerCount_; ++p) contiguousConfirmed_[p] = -1;
    }

    [[nodiscard]] uint32_t currentFrame() const { return frame_; }
    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const Stats& stats() const { return stats_; }
    [[nodiscard]] uint32_t playerCount() const { return playerCount_; }

    // Highest frame for which every player's real input is known, or -1.
    [[nodiscard]] int64_t confirmedFrame() const {
        int64_t confirmed = contiguousConfirmed_[0];
        for (uint32_t p = 1; p < playerCount_; ++p) confirmed = std::min(confirmed, contiguousConfirmed_[p]);
        return confirmed;
    }

    // False when advancing would predict further ahead than the rollback
    // window allows; the caller should stall (render the same frame) until
    // more remote input arrives.
    [[nodiscard]] bool canAdvance() const {
        return static_cast<int64_t>(frame_) - confirmedFrame() <= static_cast<int64_t>(window_);
    }

    // Records the local player's input for the current frame and advances.
    bool advance(const Input& localInput) {
        if (!canAdvance()) return false;
        setInput(localPlayer_, frame_, localInput);
        resolvePendingRollback();
        simulateFrame(frame_);
        ++frame_;
        snapshotAt(frame_) = {frame_, state_};
        return true;
    }

    // Delivers a remote player's real input for `frame`. Inputs outside the
    // window (too old, or absurdly far ahead) are ignored and return false.
    bool addRemoteInput(uint32_t player, uint32_t frame, const Input& input) {
        if (player >= playerCount_ || player == localPlayer_) return false;
        const int64_t oldest = static_cast<int64_t>(frame_) - static_cast<int64_t>(window_) - 1;
        if (static_cast<int64_t>(frame) < oldest || frame > frame_ + window_) return false;

        if (frame < frame_) {
            const Slot& used = slotAt(player, frame);
            if (used.frame != static_cast<int64_t>(frame) || !(used.input == input)) {
                ++stats_.mispredictions;
                rollbackTo_ = std::min(rollbackTo_, frame);
            }
        }
        setInput(player, frame, input);
        return true;
    }

    // Applies any rollback queued by late inputs without advancing, so
    // state() reflects every input received so far.
    void synchronize() { resolvePendingRollback(); }

    // The state after `frame` frames while it is still in the window, or
    // null. Stale until synchronize() if late inputs are pending.
    [[nodiscard]] const State* stateAt(uint32_t frame) const {
        const Snapshot& snapshot = snapshots_[frame % snapshots_.size()];
        return frame <= frame_ && snapshot.frame == frame ? &snapshot.state : nullptr;
    }

    // Every player's confirmed input for `frame`, if all are known and still in the window.
    [[nodiscard]] bool confirmedInputs(uint32_t frame, Inputs& out) const {
        if (static_cast<int64_t>(frame) > confirmedFrame()) return false;
        out = Inputs{};
        for (uint32_t p = 0; p < playerCount_; ++p) {
            const Slot& slot = inputs_[p][frame % inputs_[p].size()];
            if (slot.frame != static_cast<int64_t>(frame) || !slot.confirmed) return false;
            out[p] = slot.input;
        }
        return true;
    }

private:
    // Holds the confirmed input for a frame, or the prediction it was last
    // simulated with, so late inputs are compared against what was used.
    struct Slot {
        int64_t frame = -1;
        Input input{};
        bool confirmed = false;
    };
    struct Snapshot {
        uint32_t frame = 0;
        State state{};
    };

    Snapshot& snapshotAt(uint32_t frame) { return snapshots_[frame % snapshots_.size()]; }
    Slot& slotAt(uint32_t player, uint32_t frame) { return inputs_[player][frame % inputs_[player].size()]; }

    void setInput(uint32_t player, uint32_t frame, const Input& input) {
        Slot& slot = slotAt(player, frame);
        slot = {frame, input, true};
        int64_t next = contiguousConfirmed_[player] + 1;
        while (slotAt(player, static_cast<uint32_t>(next)).frame == next &&
               slotAt(player, static_cast<uint32_t>(next)).confirmed) {
            lastConfirmedInput_[player] = slotAt(player, static_cast<uint32_t>(next)).input;
            contiguousConfirmed_[player] = next++;
        }
    }

    void simulateFrame(uint32_t frame) {
        Inputs inputs{};
        for (uint32_t p = 0; p < playerCount_; ++p) {
            Slot& slot = slotAt(p, frame);
            if (slot.frame != static_cast<int64_t>(frame) || !slot.confirmed) {
                slot = {frame, lastConfirmedInput_[p], false};
            }
            inputs[p] = slot.input;
        }
        step_(state_, inputs, frame);
    }

    void resolvePendingRollback() {
        if (rollbackTo_ == kNoRollback) return;
        const uint32_t target = rollbackTo_;
        rollbackTo_ = kNoRollback;
        if (target >= frame_) return;

        state_ = snapshotAt(target).state;
        ++stats_.rollbacks;
        for (uint32_t f = target; f < frame_; ++f) {
            simulateFrame(f);
            snapshotAt(f + 1) = {f + 1, state_};
            ++stats_.resimulatedFrames;
        }
    }

    static constexpr uint32_t kNoRollback = UINT32_MAX;

    uint32_t playerCount_;
    uint32_t localPlayer_;
    uint32_t window_;
    StepFn step_;
    State state_;
    uint32_t frame_ = 0;
    uint32_t rollbackTo_ = kNoRollback;
    std::vector<Snapshot> snapshots_;
    std::vector<std::vector<Slot>> inputs_;
    std::array<Input, MaxPlayers> lastConfirmedInput_{};
    std::array<int64_t, MaxPlayers> contiguousConfirmed_{};
    Stats stats_;
};

} // namespace engine::net
