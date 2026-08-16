#include "net/LanDiscoveryProtocol.hpp"

#include "net/Serialization.hpp"

namespace engine::net {

namespace {
// 'KRNS' -- a real, arbitrary-but-fixed sentinel, not derived from
// anything; its only job is to make a stray, unrelated broadcast packet
// on the same UDP port fail readLanDiscoveryHeader() cleanly instead of
// being silently mis-parsed as a real announcement.
constexpr uint32_t kLanDiscoveryMagic = 0x4B524E53;
} // namespace

void writeLanDiscoveryHeader(ByteWriter& writer, LanMessageKind kind) {
    writer.writeU32(kLanDiscoveryMagic);
    writer.writeU8(static_cast<uint8_t>(kind));
}

LanMessageKind readLanDiscoveryHeader(ByteReader& reader) {
    uint32_t magic = reader.readU32();
    uint8_t kind = reader.readU8();
    if (reader.hasError() || magic != kLanDiscoveryMagic) return LanMessageKind::Invalid;
    if (kind != static_cast<uint8_t>(LanMessageKind::Announce) && kind != static_cast<uint8_t>(LanMessageKind::Echo) &&
        kind != static_cast<uint8_t>(LanMessageKind::EchoReply)) {
        return LanMessageKind::Invalid;
    }
    return static_cast<LanMessageKind>(kind);
}

void serializeLanAnnouncement(const LanSessionAnnouncement& announcement, ByteWriter& writer) {
    writer.writeU32(announcement.protocolVersion);
    writer.writeU64(announcement.sessionId);
    writer.writeString(announcement.sessionName);
    writer.writeString(announcement.hostDisplayName);
    writer.writeU32(announcement.gamePort); // real u16 value, widened -- no writeU16 primitive exists in this wire format
    writer.writeU8(announcement.currentPlayerCount);
    writer.writeU8(announcement.maxPlayerCount);
    writer.writeString(announcement.gameName);
    writer.writeFloat(announcement.gameThumbnailColor.x);
    writer.writeFloat(announcement.gameThumbnailColor.y);
    writer.writeFloat(announcement.gameThumbnailColor.z);
    writer.writeFloat(announcement.gameThumbnailColor.w);
    writer.writeU8(announcement.gameSafetyStatusValue);
    writer.writeU64(static_cast<uint64_t>(announcement.sessionStartUnixSeconds));
}

bool deserializeLanAnnouncement(ByteReader& reader, LanSessionAnnouncement& outAnnouncement) {
    LanSessionAnnouncement parsed;
    parsed.protocolVersion = reader.readU32();
    parsed.sessionId = reader.readU64();
    parsed.sessionName = reader.readString();
    parsed.hostDisplayName = reader.readString();
    parsed.gamePort = static_cast<uint16_t>(reader.readU32());
    parsed.currentPlayerCount = reader.readU8();
    parsed.maxPlayerCount = reader.readU8();
    parsed.gameName = reader.readString();
    parsed.gameThumbnailColor.x = reader.readFloat();
    parsed.gameThumbnailColor.y = reader.readFloat();
    parsed.gameThumbnailColor.z = reader.readFloat();
    parsed.gameThumbnailColor.w = reader.readFloat();
    parsed.gameSafetyStatusValue = reader.readU8();
    parsed.sessionStartUnixSeconds = static_cast<int64_t>(reader.readU64());
    if (reader.hasError()) return false;
    outAnnouncement = parsed;
    return true;
}

} // namespace engine::net
