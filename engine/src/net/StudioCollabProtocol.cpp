#include "net/StudioCollabProtocol.hpp"

#include <algorithm>

namespace engine::net {

namespace {
// CrdtValue's std::variant alternative index doubles as its own wire
// tag -- same "u8 tag + typed payload" shape RemoteEvent's own
// FieldTypeTag already establishes (RemoteEvent.cpp), just enumerated
// directly off the variant instead of a separately maintained enum,
// since CrdtValue's declared alternative order (StudioCollabState.hpp)
// is itself the one source of truth this tag has to stay in sync with.
enum class CrdtValueTag : uint8_t { Float = 0, Vec3 = 1, Quat = 2, Bool = 3, String = 4 };

void writeCrdtValue(const CrdtValue& value, ByteWriter& writer) {
    writer.writeU8(static_cast<uint8_t>(value.index()));
    if (std::holds_alternative<float>(value)) {
        writer.writeFloat(std::get<float>(value));
    } else if (std::holds_alternative<glm::vec3>(value)) {
        writer.writeVec3(std::get<glm::vec3>(value));
    } else if (std::holds_alternative<glm::quat>(value)) {
        writer.writeQuat(std::get<glm::quat>(value));
    } else if (std::holds_alternative<bool>(value)) {
        writer.writeBool(std::get<bool>(value));
    } else {
        writer.writeString(std::get<std::string>(value));
    }
}

// Returns false (leaving `out` untouched) on an unrecognized tag --
// same "stop early, don't guess" contract deserializeRemoteEventPayload
// already documents for its own unknown-tag case.
bool readCrdtValue(ByteReader& reader, CrdtValue& out) {
    auto tag = static_cast<CrdtValueTag>(reader.readU8());
    switch (tag) {
        case CrdtValueTag::Float: out = reader.readFloat(); return true;
        case CrdtValueTag::Vec3: out = reader.readVec3(); return true;
        case CrdtValueTag::Quat: out = reader.readQuat(); return true;
        case CrdtValueTag::Bool: out = reader.readBool(); return true;
        case CrdtValueTag::String: out = reader.readString(); return true;
    }
    return false;
}
} // namespace

void CrdtOpPacket::write(ByteWriter& writer) const {
    writer.writeU32(op.entityId);
    writer.writeString(op.propertyKey);
    writeCrdtValue(op.value, writer);
    writer.writeU64(op.timestamp);
    writer.writeU32(op.siteId);
}

bool CrdtOpPacket::read(ByteReader& reader, CrdtOpPacket& out) {
    CrdtOp parsed;
    parsed.entityId = reader.readU32();
    parsed.propertyKey = reader.readString();
    bool validValue = readCrdtValue(reader, parsed.value);
    parsed.timestamp = reader.readU64();
    parsed.siteId = reader.readU32();
    if (reader.hasError() || !validValue) return false;
    out.op = std::move(parsed);
    return true;
}

void CrdtDeletePacket::write(ByteWriter& writer) const {
    writer.writeU32(entityId);
    writer.writeU64(timestamp);
    writer.writeU32(siteId);
}

bool CrdtDeletePacket::read(ByteReader& reader, CrdtDeletePacket& out) {
    CrdtDeletePacket parsed;
    parsed.entityId = reader.readU32();
    parsed.timestamp = reader.readU64();
    parsed.siteId = reader.readU32();
    if (reader.hasError()) return false;
    out = parsed;
    return true;
}

void PresenceUpdatePacket::write(ByteWriter& writer) const {
    writer.writeU32(siteId);
    writer.writeString(displayName);
    writer.writeU32(colorRgb);
    writer.writeVec3(cameraPosition);
    writer.writeQuat(cameraRotation);
    uint32_t count = static_cast<uint32_t>(std::min(selectedEntityIds.size(), kMaxSelectedEntities));
    writer.writeU32(count);
    for (uint32_t i = 0; i < count; ++i) writer.writeU32(selectedEntityIds[i]);
}

bool PresenceUpdatePacket::read(ByteReader& reader, PresenceUpdatePacket& out) {
    PresenceUpdatePacket parsed;
    parsed.siteId = reader.readU32();
    parsed.displayName = reader.readString();
    parsed.colorRgb = reader.readU32();
    parsed.cameraPosition = reader.readVec3();
    parsed.cameraRotation = reader.readQuat();
    uint32_t count = reader.readU32();
    if (count > kMaxSelectedEntities) return false; // malformed/hostile count, not a valid presence packet
    parsed.selectedEntityIds.reserve(count);
    for (uint32_t i = 0; i < count && !reader.hasError(); ++i) {
        parsed.selectedEntityIds.push_back(reader.readU32());
    }
    if (reader.hasError()) return false;
    if (parsed.displayName.size() > kMaxDisplayNameBytes) return false;
    out = std::move(parsed);
    return true;
}

void EntityLockRequestPacket::write(ByteWriter& writer) const {
    writer.writeU32(entityId);
    writer.writeU32(siteId);
    writer.writeBool(acquire);
}

bool EntityLockRequestPacket::read(ByteReader& reader, EntityLockRequestPacket& out) {
    EntityLockRequestPacket parsed;
    parsed.entityId = reader.readU32();
    parsed.siteId = reader.readU32();
    parsed.acquire = reader.readBool();
    if (reader.hasError()) return false;
    out = parsed;
    return true;
}

} // namespace engine::net
