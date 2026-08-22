#pragma once

#include <cstdint>
#include <string>

#include "net/NetTypes.hpp"
#include "net/Serialization.hpp"

namespace engine::net {

// Chat channels.
//
// Roblox-style platforms route chat through named channels rather than one
// global stream, and the distinction is load-bearing: a team message must
// not reach the other team, and a whisper must not reach the room. Kept as
// explicit ids rather than strings so a channel can never be spoofed by a
// client sending an unexpected name.
enum class ChatChannel : uint32_t {
    General = 0,
    Team = 1,
    System = 2,   // server-authored notices; clients may not send on this
    Whisper = 3,
};

[[nodiscard]] const char* chatChannelName(ChatChannel channel);
// True when a CLIENT is allowed to originate on this channel. System is
// server-only: a client that could author on it would be able to forge
// messages that render as official notices.
[[nodiscard]] bool clientMaySendOn(ChatChannel channel);

// The wire form of one chat message.
//
// Deliberately a struct with its own read/write rather than inline
// ByteWriter calls at each site: the previous chat path serialised its
// fields inline in three different places (send, broadcast, receive), and
// that is exactly the shape where a field added on one side and missed on
// another desynchronises the stream and corrupts every message after it.
struct ChatMessagePacket {
    PlayerId senderId = 0;
    uint32_t channelId = static_cast<uint32_t>(ChatChannel::General);
    std::string body;
    // Milliseconds since the Unix epoch, stamped by the SERVER on relay.
    // Never trusted from the client -- a client-stamped time can be set to
    // anything, and chat ordering is exactly the kind of thing people try
    // to manipulate.
    uint64_t timestampMillis = 0;

    void write(ByteWriter& writer) const;
    // Returns false when the stream was malformed or a field failed
    // validation; `out` is left untouched in that case so a caller cannot
    // accidentally act on a half-read packet.
    [[nodiscard]] static bool read(ByteReader& reader, ChatMessagePacket& out);

    // Longest body accepted on the wire. ByteWriter::writeString is
    // u8-length-prefixed, so anything above 255 would be silently
    // truncated -- this rejects it explicitly instead.
    static constexpr size_t kMaxBodyBytes = 240;
};

[[nodiscard]] uint64_t currentUnixMillis();

} // namespace engine::net
