#include "net/ChatProtocol.hpp"

#include <chrono>

namespace engine::net {

const char* chatChannelName(ChatChannel channel) {
    switch (channel) {
        case ChatChannel::General: return "General";
        case ChatChannel::Team: return "Team";
        case ChatChannel::System: return "System";
        case ChatChannel::Whisper: return "Whisper";
    }
    return "Unknown";
}

bool clientMaySendOn(ChatChannel channel) {
    switch (channel) {
        case ChatChannel::General:
        case ChatChannel::Team:
        case ChatChannel::Whisper: return true;
        case ChatChannel::System: return false;
    }
    return false;
}

void ChatMessagePacket::write(ByteWriter& writer) const {
    writer.writeU32(senderId);
    writer.writeU32(channelId);
    writer.writeString(body);
    writer.writeU64(timestampMillis);
}

bool ChatMessagePacket::read(ByteReader& reader, ChatMessagePacket& out) {
    ChatMessagePacket parsed;
    parsed.senderId = reader.readU32();
    parsed.channelId = reader.readU32();
    parsed.body = reader.readString();
    parsed.timestampMillis = reader.readU64();
    // Checked AFTER reading every field, not early-returned between them:
    // the reader must consume the whole packet either way, or the next
    // message in the stream starts at the wrong offset.
    if (reader.hasError()) return false;
    if (parsed.body.size() > kMaxBodyBytes) return false;
    // An unknown channel id is a protocol violation, not something to
    // guess at -- silently remapping it to General would deliver a team
    // message to everyone.
    if (parsed.channelId > static_cast<uint32_t>(ChatChannel::Whisper)) return false;
    out = parsed;
    return true;
}

uint64_t currentUnixMillis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

} // namespace engine::net
