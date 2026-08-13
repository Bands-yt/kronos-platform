#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "net/NetTypes.hpp"

namespace engine::net {

// Real, hand-rolled little-endian binary (de)serialization -- no external
// serialization library, matching this codebase's existing "small, real,
// hand-rolled format" style (see core::AnimationClip::saveToFile's own
// plain-text format for the closest precedent, and core::SceneFile's
// JSON-via-nlohmann for the "when a real library earns its weight"
// counter-example this deliberately isn't -- a per-tick network payload
// needs to be small and fast to (de)serialize, not human-readable).
// This is the real wire format NetTypes.hpp's own header comment named
// as future work ("the pre-serialization shape... not the wire format
// itself").

// Real bounds-checked byte writer -- grows as needed, no fixed cap.
class ByteWriter {
public:
    void writeU8(uint8_t v);
    void writeU32(uint32_t v);
    void writeU64(uint64_t v); // two little-endian writeU32() calls (low, then high) -- see .cpp
    void writeFloat(float v);
    void writeBool(bool v) { writeU8(v ? 1 : 0); }
    void writeVec3(const glm::vec3& v);
    void writeQuat(const glm::quat& q);
    void writeString(const std::string& s); // length-prefixed (u8 length, truncated at 255 -- real, bounded, matches every other short-identifier field in this codebase)

    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return buffer_; }
    [[nodiscard]] size_t size() const { return buffer_.size(); }

private:
    std::vector<uint8_t> buffer_;
};

// Real bounds-checked byte reader -- every read past the end of the
// buffer sets a sticky error flag and returns a real, honest default
// value (0/false/identity) instead of reading out of bounds. A caller
// MUST check hasError() after a full deserialize -- this is what makes
// it safe to run against adversarial/truncated network input, the same
// "never trust a client-sent payload" boundary net::RemoteEvent's schema
// validation already enforces one layer up.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    [[nodiscard]] uint8_t readU8();
    [[nodiscard]] uint32_t readU32();
    [[nodiscard]] uint64_t readU64();
    [[nodiscard]] float readFloat();
    [[nodiscard]] bool readBool() { return readU8() != 0; }
    [[nodiscard]] glm::vec3 readVec3();
    [[nodiscard]] glm::quat readQuat();
    [[nodiscard]] std::string readString();

    [[nodiscard]] bool hasError() const { return error_; }
    [[nodiscard]] bool atEnd() const { return offset_ >= size_; }
    [[nodiscard]] size_t bytesRemaining() const { return offset_ <= size_ ? size_ - offset_ : 0; }

private:
    [[nodiscard]] bool ensure(size_t n);

    const uint8_t* data_;
    size_t size_;
    size_t offset_ = 0;
    bool error_ = false;
};

// Real (de)serialization for the three NetTypes.hpp structs.
void serializeInputCommand(const InputCommand& cmd, ByteWriter& writer);
[[nodiscard]] bool deserializeInputCommand(ByteReader& reader, InputCommand& outCommand);

void serializeEntityState(const EntityState& state, ByteWriter& writer);
[[nodiscard]] bool deserializeEntityState(ByteReader& reader, EntityState& outState);

// Full (non-delta) snapshot serialization -- what a join/keyframe
// snapshot (baselineTick == 0) uses.
void serializeSnapshotFull(const DeltaSnapshot& snapshot, ByteWriter& writer);
[[nodiscard]] bool deserializeSnapshotFull(ByteReader& reader, DeltaSnapshot& outSnapshot);

// Real delta-compression (Sprint 11 task 1's "Add delta-compression for
// efficient network updates") -- writes only entities whose state
// differs from `baseline` by more than a real, small epsilon (position/
// rotation/velocity each compared independently, any one exceeding its
// epsilon includes the whole entity), plus a real per-entity "removed"
// list for entities present in `baseline` but absent from `current`
// (despawned since the client's last acknowledged snapshot). Entities in
// `current` with no matching networkId in `baseline` are always included
// in full (a real "just became relevant" case, e.g. streaming/interest
// management newly including it). `baseline` is looked up by
// EntityState::networkId, not by array position -- position isn't a
// stable identity across snapshots.
void serializeSnapshotDelta(const DeltaSnapshot& current, const DeltaSnapshot& baseline, ByteWriter& writer);
// Reconstructs the full logical DeltaSnapshot by applying the decoded
// delta on top of `baseline` -- unchanged entities are copied through
// from baseline untouched, changed/new entities take the decoded values,
// removed entities are dropped.
[[nodiscard]] bool deserializeSnapshotDelta(ByteReader& reader, const DeltaSnapshot& baseline, DeltaSnapshot& outSnapshot);

} // namespace engine::net
