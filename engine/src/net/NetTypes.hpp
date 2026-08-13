#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::net {

using PlayerId = uint32_t;
inline constexpr PlayerId kInvalidPlayer = 0;

// Kronos ("Active Joining UI" -- NetworkSession protocol foundation): a
// real, plain, compiled-in, exact-match version number for this hand-
// rolled wire format -- deliberately NOT a semver-style compatible-range
// check the way core::ProjectFile::isCompatibleVersion() treats a saved
// file's major version. There is no cross-build wire-compatibility
// promise anywhere in this codebase (see Serialization.hpp's own header
// comment on why this is a small, fast, hand-rolled format, not a
// versioned schema library) -- a real deployment always has both ends
// running the same compiled binary, so a mismatch here means "these two
// processes were built from different source," a hard reject, not a
// range to negotiate. Bump this whenever a wire message's real layout
// changes.
inline constexpr uint32_t kNetworkProtocolVersion = 1;

// What a client sends, every local tick -- intent, never state. Matches
// docs/ARCHITECTURE.md §4.2's "clients send input/intent, never state" and
// the reconciliation pseudocode's `input.seq`.
struct InputCommand {
    uint32_t sequence = 0;
    float deltaTime = 0.0f;
    glm::vec3 moveAxis{0.0f};    // e.g. WASD / left stick, already normalized client-side
    bool jump = false;
    bool primaryAction = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// One entity's replicated state, as of a given server tick. Deliberately
// flat and small -- the real replication format is a bit-packed delta
// against the client's last-acknowledged baseline (§4.2 / §3's "Custom
// bit-packed delta snapshots" stack entry), which this struct is the
// pre-serialization shape for, not the wire format itself. See
// ServerReconciliation.hpp's TODO for where delta-compression attaches.
struct EntityState {
    uint32_t networkId = 0;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
};

// What the server broadcasts. `baselineTick` is the client's
// last-acknowledged tick this snapshot is a delta against; 0 means "this
// is a full/keyframe snapshot" (e.g. on join). Interest management
// (§8.5's streaming replication) filters `entities` per-recipient before
// this is serialized -- that filtering is ServerReconciliation's job, not
// this struct's.
struct DeltaSnapshot {
    uint32_t tick = 0;
    uint32_t baselineTick = 0;
    uint32_t lastProcessedInputSequence = 0; // echoes InputCommand::sequence for reconciliation
    std::vector<EntityState> entities;
};

} // namespace engine::net
