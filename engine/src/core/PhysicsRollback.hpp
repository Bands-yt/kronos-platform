#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/Physics.hpp"
#include "net/RollbackSession.hpp"

namespace engine::core {

inline constexpr uint32_t kMaxRollbackPlayers = 8;

// One player's input for one frame, quantized so every peer sees exactly
// the same values.
struct RollbackInput {
    int8_t moveX = 0; // -127..127
    int8_t moveZ = 0;
    uint16_t buttons = 0;
    int16_t yaw = 0; // a full turn is 65536
    int16_t pitch = 0;

    enum Button : uint16_t { Jump = 1u << 0, Primary = 1u << 1, Secondary = 1u << 2, Sprint = 1u << 3, Crouch = 1u << 4 };

    [[nodiscard]] static RollbackInput make(glm::vec2 move, float yawRadians, float pitchRadians, uint16_t buttons);
    [[nodiscard]] glm::vec2 move() const { return glm::vec2(moveX, moveZ) / 127.0f; }
    [[nodiscard]] float yawRadians() const;
    [[nodiscard]] float pitchRadians() const;
    [[nodiscard]] bool held(Button button) const { return (buttons & button) != 0; }

    friend bool operator==(const RollbackInput&, const RollbackInput&) = default;
};
static_assert(sizeof(RollbackInput) == 8);

using RollbackInputs = std::array<RollbackInput, kMaxRollbackPlayers>;

// Everything a deterministic frame needs besides the physics world.
struct RollbackFrameContext {
    Physics& physics;
    ECS& ecs;
    const RollbackInputs& inputs;
    uint32_t playerCount;
    uint32_t frame;
    float dt;
    std::vector<uint8_t>& gameState;                         // the game's own rollback state
    const std::vector<Physics::CollisionEvent>& contacts;    // from the previous frame's step
};

// Game logic for one frame, run before physics steps. It must give the same
// result for the same inputs and state on every machine, keep everything it
// remembers between frames in `gameState`, and not create or destroy
// bodies (snapshots can't bring them back).
using RollbackStepFn = std::function<void(RollbackFrameContext&)>;

// Recorded inputs and per-frame checksums of a deterministic run, enough
// to replay it exactly or to check a build still simulates it the same way.
struct PhysicsReplay {
    struct Frame {
        RollbackInputs inputs{};
        uint64_t checksum = 0; // of the state after this frame
    };

    uint32_t players = 0;
    float frameDt = 1.0f / 60.0f;
    std::vector<uint8_t> initialPhysics;
    std::vector<uint8_t> initialGame;
    std::vector<Frame> frames;

    [[nodiscard]] std::vector<uint8_t> serialize() const;
    [[nodiscard]] bool parse(const std::vector<uint8_t>& bytes, std::string& error);
    [[nodiscard]] bool save(const std::string& path, std::string* error = nullptr) const;
    [[nodiscard]] bool load(const std::string& path, std::string* error = nullptr);

    struct Verification {
        bool restored = false;
        int64_t firstMismatch = -1; // frame index, or -1 when every checksum matched
        uint32_t framesChecked = 0;
        std::vector<uint8_t> finalGame;
    };
    // Restores the initial state into `physics` (which must hold the same
    // bodies as when recording started) and resimulates every frame.
    [[nodiscard]] Verification verify(Physics& physics, ECS& ecs, const RollbackStepFn& step) const;
};

[[nodiscard]] uint64_t rollbackChecksum(const std::vector<uint8_t>& physics, const std::vector<uint8_t>& game);

// Rollback netcode for physics games: every peer simulates the whole
// world, predicts missing remote inputs, and when a real input arrives
// that differs, restores the physics snapshot from before that frame and
// resimulates. Jolt is built cross-platform deterministic, so peers on
// different machines stay bit-identical; checksums of confirmed frames
// catch it if they ever don't.
class PhysicsRollback {
    struct State {
        uint64_t serial = 0;
        std::vector<uint8_t> physics;
        std::vector<uint8_t> game;
        std::vector<Physics::CollisionEvent> contacts;
    };
    using Session = net::RollbackSession<State, RollbackInput, kMaxRollbackPlayers>;

public:
    struct Settings {
        uint32_t players = 2;
        uint32_t localPlayer = 0;
        uint32_t maxRollbackFrames = 8;
        float frameDt = 1.0f / 60.0f;
        bool record = false; // keep a PhysicsReplay of every confirmed frame
    };
    using Stats = Session::Stats;

    PhysicsRollback(Physics& physics, ECS& ecs, const Settings& settings, RollbackStepFn step,
                    std::vector<uint8_t> initialGameState = {});

    [[nodiscard]] bool canAdvance() const { return session_.canAdvance(); }
    // Records the local input for the current frame and simulates it.
    // False (nothing happens) when too far ahead of the slowest peer.
    bool advance(const RollbackInput& localInput);
    bool addRemoteInput(uint32_t player, uint32_t frame, const RollbackInput& input);
    // Resimulates for late inputs now, so physics and gameState() are current.
    void synchronize();

    [[nodiscard]] uint32_t currentFrame() const { return session_.currentFrame(); }
    [[nodiscard]] int64_t confirmedFrame() const { return session_.confirmedFrame(); }
    [[nodiscard]] const std::vector<uint8_t>& gameState() const { return session_.state().game; }
    [[nodiscard]] const Stats& stats() const { return session_.stats(); }
    [[nodiscard]] const Settings& settings() const { return settings_; }
    // Checksum of the starting state, for checking peers start out identical.
    [[nodiscard]] uint64_t initialChecksum() const { return initialChecksum_; }

    // Checksum of the state after `frame` frames, once every input that led
    // there is confirmed. Kept for the last 256 confirmed frames.
    [[nodiscard]] std::optional<uint64_t> checksum(uint32_t frame) const;
    [[nodiscard]] std::optional<std::pair<uint32_t, uint64_t>> latestChecksum() const;
    // Compares a peer's checksum (now or once ours is known). A mismatch
    // means the peers have desynced; desyncFrame() reports the first one.
    void addRemoteChecksum(uint32_t frame, uint64_t checksum);
    [[nodiscard]] std::optional<uint32_t> desyncFrame() const { return desyncFrame_; }
    // A snapshot failed to restore (bodies were created or destroyed).
    [[nodiscard]] bool restoreFailed() const { return restoreFailed_; }

    [[nodiscard]] const PhysicsReplay& replay() const { return replay_; }

private:
    static State initialState(Physics& physics, std::vector<uint8_t> game);
    void simulate(State& state, const RollbackInputs& inputs, uint32_t frame);
    void collectConfirmed();
    void compare(uint32_t frame, uint64_t local, uint64_t remote);

    Physics& physics_;
    ECS& ecs_;
    Settings settings_;
    RollbackStepFn step_;
    uint64_t liveSerial_ = 1;
    uint64_t nextSerial_ = 2;
    Session session_;
    uint64_t initialChecksum_ = 0;
    uint32_t nextChecksumFrame_ = 1;
    std::deque<std::pair<uint32_t, uint64_t>> checksums_;
    std::vector<std::pair<uint32_t, uint64_t>> pendingRemote_;
    std::optional<uint32_t> desyncFrame_;
    bool restoreFailed_ = false;
    PhysicsReplay replay_;
};

} // namespace engine::core
