#include "net/StudioCollabState.hpp"

#include <algorithm>

namespace engine::net {

uint64_t LamportClockGenerator::tick() {
    ++counter_;
    return counter_;
}

void LamportClockGenerator::observe(uint64_t remoteTimestamp) {
    counter_ = std::max(counter_, remoteTimestamp) + 1;
}

bool CrdtPropertyRegister::apply(const CrdtOp& op) {
    if (isDeleted(op.entityId)) return false; // tombstoned entities reject further property writes

    PropertyKey key{op.entityId, op.propertyKey};
    auto it = entries_.find(key);
    if (it != entries_.end() && !op.happensAfter(it->second.timestamp, it->second.siteId)) {
        return false; // the existing entry already wins (or ties and loses the siteId tiebreak)
    }
    entries_[key] = Entry{op.value, op.timestamp, op.siteId};
    return true;
}

const CrdtValue* CrdtPropertyRegister::get(uint32_t entityId, const std::string& propertyKey) const {
    auto it = entries_.find(PropertyKey{entityId, propertyKey});
    return it != entries_.end() ? &it->second.value : nullptr;
}

bool CrdtPropertyRegister::applyDelete(uint32_t entityId, uint64_t timestamp, PlayerId siteId) {
    auto it = tombstones_.find(entityId);
    if (it != tombstones_.end()) {
        bool newerWins = timestamp != it->second.timestamp ? timestamp > it->second.timestamp : siteId > it->second.siteId;
        if (!newerWins) return false;
    }
    tombstones_[entityId] = Tombstone{timestamp, siteId};
    return true;
}

bool CrdtPropertyRegister::isDeleted(uint32_t entityId) const {
    return tombstones_.find(entityId) != tombstones_.end();
}

bool EntityLockTable::tryAcquire(uint32_t entityId, PlayerId siteId, double nowSeconds, double leaseDurationSeconds) {
    auto it = leases_.find(entityId);
    if (it != leases_.end() && it->second.expiresAt > nowSeconds && it->second.siteId != siteId) {
        return false; // a different site holds a still-live lease
    }
    leases_[entityId] = Lease{siteId, nowSeconds + leaseDurationSeconds};
    return true;
}

bool EntityLockTable::release(uint32_t entityId, PlayerId siteId) {
    auto it = leases_.find(entityId);
    if (it == leases_.end() || it->second.siteId != siteId) return false;
    leases_.erase(it);
    return true;
}

bool EntityLockTable::isLocked(uint32_t entityId, double nowSeconds) const {
    auto it = leases_.find(entityId);
    return it != leases_.end() && it->second.expiresAt > nowSeconds;
}

PlayerId EntityLockTable::ownerOf(uint32_t entityId, double nowSeconds) const {
    auto it = leases_.find(entityId);
    if (it == leases_.end() || it->second.expiresAt <= nowSeconds) return kInvalidPlayer;
    return it->second.siteId;
}

} // namespace engine::net
