#include "net/Serialization.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace engine::net {

// --- ByteWriter --------------------------------------------------------

void ByteWriter::writeU8(uint8_t v) { buffer_.push_back(v); }

void ByteWriter::writeU32(uint32_t v) {
    // Explicit little-endian byte order, independent of host endianness --
    // this is the one place that matters, since everything else in this
    // codebase runs in-process and never cares about byte order.
    buffer_.push_back(static_cast<uint8_t>(v & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void ByteWriter::writeU64(uint64_t v) {
    writeU32(static_cast<uint32_t>(v & 0xFFFFFFFFu));
    writeU32(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}

void ByteWriter::writeFloat(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32(bits);
}

void ByteWriter::writeVec3(const glm::vec3& v) {
    writeFloat(v.x);
    writeFloat(v.y);
    writeFloat(v.z);
}

void ByteWriter::writeQuat(const glm::quat& q) {
    writeFloat(q.w);
    writeFloat(q.x);
    writeFloat(q.y);
    writeFloat(q.z);
}

void ByteWriter::writeString(const std::string& s) {
    uint8_t length = static_cast<uint8_t>(std::min<size_t>(s.size(), 255));
    writeU8(length);
    buffer_.insert(buffer_.end(), s.begin(), s.begin() + length);
}

// --- ByteReader ----------------------------------------------------------

bool ByteReader::ensure(size_t n) {
    if (error_ || offset_ + n > size_) {
        error_ = true;
        return false;
    }
    return true;
}

uint8_t ByteReader::readU8() {
    if (!ensure(1)) return 0;
    return data_[offset_++];
}

uint32_t ByteReader::readU32() {
    if (!ensure(4)) return 0;
    uint32_t v = static_cast<uint32_t>(data_[offset_]) | (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                 (static_cast<uint32_t>(data_[offset_ + 2]) << 16) | (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
    offset_ += 4;
    return v;
}

uint64_t ByteReader::readU64() {
    uint64_t low = readU32();
    uint64_t high = readU32();
    return low | (high << 32);
}

float ByteReader::readFloat() {
    uint32_t bits = readU32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

glm::vec3 ByteReader::readVec3() {
    float x = readFloat();
    float y = readFloat();
    float z = readFloat();
    return glm::vec3(x, y, z);
}

glm::quat ByteReader::readQuat() {
    float w = readFloat();
    float x = readFloat();
    float y = readFloat();
    float z = readFloat();
    return glm::quat(w, x, y, z);
}

std::string ByteReader::readString() {
    uint8_t length = readU8();
    if (!ensure(length)) return {};
    std::string s(reinterpret_cast<const char*>(data_ + offset_), length);
    offset_ += length;
    return s;
}

// --- InputCommand / EntityState -----------------------------------------

void serializeInputCommand(const InputCommand& cmd, ByteWriter& writer) {
    writer.writeU32(cmd.sequence);
    writer.writeFloat(cmd.deltaTime);
    writer.writeVec3(cmd.moveAxis);
    writer.writeBool(cmd.jump);
    writer.writeBool(cmd.primaryAction);
    writer.writeFloat(cmd.yaw);
    writer.writeFloat(cmd.pitch);
}

bool deserializeInputCommand(ByteReader& reader, InputCommand& outCommand) {
    outCommand.sequence = reader.readU32();
    outCommand.deltaTime = reader.readFloat();
    outCommand.moveAxis = reader.readVec3();
    outCommand.jump = reader.readBool();
    outCommand.primaryAction = reader.readBool();
    outCommand.yaw = reader.readFloat();
    outCommand.pitch = reader.readFloat();
    return !reader.hasError();
}

void serializeEntityState(const EntityState& state, ByteWriter& writer) {
    writer.writeU32(state.networkId);
    writer.writeVec3(state.position);
    writer.writeQuat(state.rotation);
    writer.writeVec3(state.velocity);
}

bool deserializeEntityState(ByteReader& reader, EntityState& outState) {
    outState.networkId = reader.readU32();
    outState.position = reader.readVec3();
    outState.rotation = reader.readQuat();
    outState.velocity = reader.readVec3();
    return !reader.hasError();
}

// --- Full snapshot ---------------------------------------------------------

void serializeSnapshotFull(const DeltaSnapshot& snapshot, ByteWriter& writer) {
    writer.writeU32(snapshot.tick);
    writer.writeU32(snapshot.baselineTick);
    writer.writeU32(snapshot.lastProcessedInputSequence);
    writer.writeU32(static_cast<uint32_t>(snapshot.entities.size()));
    for (const EntityState& entity : snapshot.entities) serializeEntityState(entity, writer);
}

bool deserializeSnapshotFull(ByteReader& reader, DeltaSnapshot& outSnapshot) {
    outSnapshot.tick = reader.readU32();
    outSnapshot.baselineTick = reader.readU32();
    outSnapshot.lastProcessedInputSequence = reader.readU32();
    uint32_t count = reader.readU32();
    outSnapshot.entities.clear();
    // Real, bounded sanity cap -- an adversarial/corrupted count field
    // (e.g. 0xFFFFFFFF) must not turn into an attempted multi-gigabyte
    // allocation; a real snapshot never legitimately carries anywhere
    // near this many entities.
    constexpr uint32_t kMaxReasonableEntities = 100000;
    if (count > kMaxReasonableEntities) return false;
    outSnapshot.entities.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        EntityState state;
        if (!deserializeEntityState(reader, state)) return false;
        outSnapshot.entities.push_back(state);
    }
    return !reader.hasError();
}

// --- Delta snapshot ----------------------------------------------------

namespace {
// Real, small, per-field epsilons -- deliberately different magnitudes
// (rotation is a unit quaternion, its components live in [-1,1]; position/
// velocity are world-space units) rather than one shared epsilon that
// would either be too loose for rotation or too tight for position.
constexpr float kPositionEpsilon = 0.01f;
constexpr float kRotationEpsilon = 0.001f;
constexpr float kVelocityEpsilon = 0.02f;

bool entityStateChanged(const EntityState& current, const EntityState& baseline) {
    glm::vec3 dp = current.position - baseline.position;
    if (glm::dot(dp, dp) > kPositionEpsilon * kPositionEpsilon) return true;
    glm::vec3 dv = current.velocity - baseline.velocity;
    if (glm::dot(dv, dv) > kVelocityEpsilon * kVelocityEpsilon) return true;
    // Quaternion difference: 1 - |dot| is a real, cheap "angular distance"
    // proxy that's 0 for identical rotations (up to the real q/-q double
    // cover) and grows toward 1 as they diverge -- exact angle isn't
    // needed, just a real, monotonic "did this change enough to matter"
    // signal.
    float dot = glm::dot(current.rotation, baseline.rotation);
    if (1.0f - std::fabs(dot) > kRotationEpsilon) return true;
    return false;
}
} // namespace

void serializeSnapshotDelta(const DeltaSnapshot& current, const DeltaSnapshot& baseline, ByteWriter& writer) {
    writer.writeU32(current.tick);
    writer.writeU32(baseline.tick); // real baselineTick -- the client's own last-acknowledged tick, not hardcoded 0
    writer.writeU32(current.lastProcessedInputSequence);

    std::unordered_map<uint32_t, const EntityState*> baselineById;
    baselineById.reserve(baseline.entities.size());
    for (const EntityState& entity : baseline.entities) baselineById[entity.networkId] = &entity;

    std::vector<const EntityState*> changed;
    changed.reserve(current.entities.size());
    for (const EntityState& entity : current.entities) {
        auto it = baselineById.find(entity.networkId);
        if (it == baselineById.end() || entityStateChanged(entity, *it->second)) {
            changed.push_back(&entity);
        }
    }

    std::unordered_map<uint32_t, bool> currentIds;
    currentIds.reserve(current.entities.size());
    for (const EntityState& entity : current.entities) currentIds[entity.networkId] = true;
    std::vector<uint32_t> removed;
    for (const EntityState& entity : baseline.entities) {
        if (currentIds.find(entity.networkId) == currentIds.end()) removed.push_back(entity.networkId);
    }

    writer.writeU32(static_cast<uint32_t>(changed.size()));
    for (const EntityState* entity : changed) serializeEntityState(*entity, writer);
    writer.writeU32(static_cast<uint32_t>(removed.size()));
    for (uint32_t id : removed) writer.writeU32(id);
}

bool deserializeSnapshotDelta(ByteReader& reader, const DeltaSnapshot& baseline, DeltaSnapshot& outSnapshot) {
    outSnapshot.tick = reader.readU32();
    outSnapshot.baselineTick = reader.readU32();
    outSnapshot.lastProcessedInputSequence = reader.readU32();

    uint32_t changedCount = reader.readU32();
    constexpr uint32_t kMaxReasonableEntities = 100000;
    if (changedCount > kMaxReasonableEntities) return false;
    std::unordered_map<uint32_t, EntityState> merged;
    merged.reserve(baseline.entities.size() + changedCount);
    for (const EntityState& entity : baseline.entities) merged[entity.networkId] = entity;

    for (uint32_t i = 0; i < changedCount; ++i) {
        EntityState entity;
        if (!deserializeEntityState(reader, entity)) return false;
        merged[entity.networkId] = entity;
    }

    uint32_t removedCount = reader.readU32();
    if (removedCount > kMaxReasonableEntities) return false;
    for (uint32_t i = 0; i < removedCount; ++i) {
        uint32_t id = reader.readU32();
        merged.erase(id);
    }
    if (reader.hasError()) return false;

    outSnapshot.entities.clear();
    outSnapshot.entities.reserve(merged.size());
    for (auto& [id, entity] : merged) outSnapshot.entities.push_back(entity);
    return true;
}

} // namespace engine::net
