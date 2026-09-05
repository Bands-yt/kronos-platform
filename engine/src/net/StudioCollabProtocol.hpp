#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "net/NetTypes.hpp"
#include "net/Serialization.hpp"
#include "net/StudioCollabState.hpp"

namespace engine::net {

// Wire (de)serialization for Live Collaboration (Beta Roadmap). Same
// struct-with-its-own-read/write shape as ChatMessagePacket
// (ChatProtocol.hpp) -- see that header's own comment for why (a field
// added on one side and missed on another desynchronises the stream and
// corrupts every message after it).

// One CrdtOp (StudioCollabState.hpp) over the wire.
struct CrdtOpPacket {
    CrdtOp op;

    void write(ByteWriter& writer) const;
    // Returns false when the stream was malformed or the value tag was
    // unrecognized; `out` is left untouched in that case, same contract
    // ChatMessagePacket::read() already carries.
    [[nodiscard]] static bool read(ByteReader& reader, CrdtOpPacket& out);
};

// One entity-delete tombstone over the wire -- CrdtPropertyRegister::
// applyDelete()'s 3 real arguments, not a CrdtOp (a delete has no
// propertyKey/value).
struct CrdtDeletePacket {
    uint32_t entityId = 0;
    uint64_t timestamp = 0;
    PlayerId siteId = kInvalidPlayer;

    void write(ByteWriter& writer) const;
    [[nodiscard]] static bool read(ByteReader& reader, CrdtDeletePacket& out);
};

// One Studio instance's real-time presence (Task brief's "user-colored
// selection bounds and camera frustums"), broadcast periodically by
// each connected Studio instance.
struct PresenceUpdatePacket {
    PlayerId siteId = kInvalidPlayer;
    std::string displayName;
    uint32_t colorRgb = 0xFFFFFFu; // 0xRRGGBB, this site's presence-indicator tint
    glm::vec3 cameraPosition{0.0f};
    glm::quat cameraRotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<uint32_t> selectedEntityIds; // this site's current viewport selection, for the peer selection-bounds overlay

    void write(ByteWriter& writer) const;
    [[nodiscard]] static bool read(ByteReader& reader, PresenceUpdatePacket& out);

    // Bounds a malformed/hostile count field the same way
    // ChatMessagePacket::kMaxBodyBytes bounds a chat body, rather than
    // trusting an attacker-controlled length to drive an unbounded read
    // loop.
    static constexpr size_t kMaxSelectedEntities = 256;
    static constexpr size_t kMaxDisplayNameBytes = 64;
};

// A lock acquire/release request or relay (EntityLockTable,
// StudioCollabState.hpp).
struct EntityLockRequestPacket {
    uint32_t entityId = 0;
    PlayerId siteId = kInvalidPlayer;
    bool acquire = true; // false = release

    void write(ByteWriter& writer) const;
    [[nodiscard]] static bool read(ByteReader& reader, EntityLockRequestPacket& out);
};

} // namespace engine::net
