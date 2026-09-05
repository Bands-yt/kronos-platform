#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "net/NetTypes.hpp"

namespace engine::net {

// Kronos ("Live Collaboration & In-Studio 3D Modeling Pipeline" -- Beta
// Roadmap): a real, deliberately small property-level CRDT for
// multi-Studio-instance scene editing.
//
// Scope, stated plainly (matching this codebase's "real, honest
// capability" convention -- see core::EditableMesh.hpp's own class
// comment for the same pattern): this is a per-(entity, property)
// Last-Writer-Wins Register, ordered by a Lamport LOGICAL clock with
// siteId as the tiebreak -- NOT wall-clock time (clock skew between two
// developers' machines would break convergence and make merges
// non-deterministic across replicas). LWW is safe for scalar
// TRANSFORM/COMPONENT PROPERTIES (position, color, a material name)
// because the newest write is always the intended final value
// regardless of arrival order. It is deliberately NOT extended to
// arbitrary scene-graph structural edits (reparenting) here -- two
// developers concurrently reparenting the same entity into two
// different new parents can build a cycle under naive LWW, which needs
// real cycle-breaking logic this class doesn't attempt to smuggle in;
// that's real, separate scope, not silently pretended-away. Entity
// deletion uses a real tombstone (applyDelete()/isDeleted()) rather
// than LWW on a "deleted" property, because a property register has no
// way to represent "stop existing" as a value; once tombstoned, an
// entity stays deleted permanently under this register (no CRDT-level
// undelete -- Studio's own UndoStack.hpp is the real, separate, LOCAL
// mechanism for a developer reverting their own delete before it syncs
// away).
class LamportClockGenerator {
public:
    // Advances and returns this site's own next timestamp -- call once
    // per locally originated CrdtOp.
    uint64_t tick();
    // Standard Lamport rule (local = max(local, remote) + 1) -- call on
    // every remotely received op's timestamp, including ones this
    // register ends up rejecting (a stale op still tells you the
    // sender's clock).
    void observe(uint64_t remoteTimestamp);
    [[nodiscard]] uint64_t current() const { return counter_; }

private:
    uint64_t counter_ = 0;
};

// Same "one wire-safe generic value" shape net::RemoteEvent::FieldValue
// (RemoteEvent.hpp) already establishes for RPC payloads, just with the
// alternatives a Transform/Renderable property actually needs.
using CrdtValue = std::variant<float, glm::vec3, glm::quat, bool, std::string>;

// One real, causally ordered write: "as of (timestamp, siteId),
// entity's propertyKey is value."
struct CrdtOp {
    uint32_t entityId = 0;
    std::string propertyKey;
    CrdtValue value{0.0f};
    uint64_t timestamp = 0;
    PlayerId siteId = kInvalidPlayer;

    // Real total order over concurrent ops -- higher timestamp wins; a
    // tie (genuinely concurrent, e.g. two sites editing before either
    // has seen the other's clock) is broken by siteId so every replica
    // resolves it identically no matter which op it received first.
    [[nodiscard]] bool happensAfter(uint64_t otherTimestamp, PlayerId otherSiteId) const {
        if (timestamp != otherTimestamp) return timestamp > otherTimestamp;
        return siteId > otherSiteId;
    }
};

// The real register. Every apply() is idempotent and commutative under
// the ordering above -- feeding the same set of ops to two registers in
// any order leaves both in the identical final state, the actual
// convergence property a CRDT exists to guarantee (see test_main.cpp's
// testCrdtPropertyRegister* tests, which apply the same ops in
// different orders and assert identical results).
class CrdtPropertyRegister {
public:
    // Returns true if `op` actually changed the stored value (i.e. it
    // won the ordering above against whatever was already there) --
    // callers use this to decide whether to also apply it to the live
    // ECS component; a losing op is real, expected CRDT behavior, not
    // an error. Always loses against a tombstoned entity (see
    // applyDelete()).
    bool apply(const CrdtOp& op);

    [[nodiscard]] const CrdtValue* get(uint32_t entityId, const std::string& propertyKey) const;

    // Real tombstoned delete -- see class comment for why this exists
    // separately from apply(). Returns true if this op actually
    // tombstoned the entity (false if an existing tombstone already
    // wins the ordering above, i.e. this op is stale).
    bool applyDelete(uint32_t entityId, uint64_t timestamp, PlayerId siteId);
    [[nodiscard]] bool isDeleted(uint32_t entityId) const;

    [[nodiscard]] size_t propertyCount() const { return entries_.size(); }

private:
    struct PropertyKey {
        uint32_t entityId;
        std::string propertyKey;
        bool operator==(const PropertyKey& other) const {
            return entityId == other.entityId && propertyKey == other.propertyKey;
        }
    };
    struct PropertyKeyHash {
        size_t operator()(const PropertyKey& k) const {
            return std::hash<uint32_t>()(k.entityId) ^ (std::hash<std::string>()(k.propertyKey) << 1);
        }
    };
    struct Entry {
        CrdtValue value;
        uint64_t timestamp;
        PlayerId siteId;
    };
    struct Tombstone {
        uint64_t timestamp;
        PlayerId siteId;
    };

    std::unordered_map<PropertyKey, Entry, PropertyKeyHash> entries_;
    std::unordered_map<uint32_t, Tombstone> tombstones_;
};

// Real transient mutex-style workspace lock (Task brief's "Presence &
// Workspace Locking"). Deliberately NOT a consensus/correctness
// mechanism -- two Studio instances racing tryAcquire() in the same
// network round-trip can both briefly believe they hold the lock before
// a server-relayed rejection arrives, same as any optimistic
// client-side UI lock. Its real job is cutting down accidental
// simultaneous-edit churn during normal use, not preventing a
// determined/malicious client from ignoring it -- CrdtPropertyRegister's
// own ordering above is what actually still converges correctly even if
// a lock is bypassed entirely.
class EntityLockTable {
public:
    // Real acquire-or-renew: succeeds (true) if the entity is unlocked,
    // its existing lease has expired, or `siteId` already holds it
    // (idempotent renew, extends the lease). Fails (false, table
    // unchanged) if a DIFFERENT site currently holds a live lease.
    bool tryAcquire(uint32_t entityId, PlayerId siteId, double nowSeconds, double leaseDurationSeconds);
    // Real explicit release. No-op (false) if `siteId` doesn't hold
    // entityId's lock -- a site can't release a lock it doesn't own.
    bool release(uint32_t entityId, PlayerId siteId);
    [[nodiscard]] bool isLocked(uint32_t entityId, double nowSeconds) const;
    // kInvalidPlayer if the entity is unlocked or its lease expired --
    // an expired lease is treated as free without needing an explicit
    // release message, covering a disconnected/crashed Studio instance
    // that never sent one.
    [[nodiscard]] PlayerId ownerOf(uint32_t entityId, double nowSeconds) const;

private:
    struct Lease {
        PlayerId siteId;
        double expiresAt;
    };
    std::unordered_map<uint32_t, Lease> leases_;
};

} // namespace engine::net
