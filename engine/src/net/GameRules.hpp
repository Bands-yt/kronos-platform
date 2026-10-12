#pragma once

#include "net/NetTypes.hpp"
#include "net/Serialization.hpp"

#include <cstdint>

namespace engine::net {

// Game-specific server rules and messages, so the network layer carries no
// game types. Wire ids 8-12 are reserved for these game messages.
class GameRules {
public:
    enum class Outcome { Ignored, Handled, Rejected };

    virtual ~GameRules() = default;

    virtual void playerRegistered(PlayerId player) { (void)player; }
    virtual void playerUnregistered(PlayerId player) { (void)player; }

    // Server: a game message from a client. A Handled outcome sends `broadcast`
    // reliably to everyone when it is not empty.
    virtual Outcome onClientMessage(uint8_t type, ByteReader& reader, PlayerId from, float clockSeconds,
                                    ByteWriter& broadcast) {
        (void)type; (void)reader; (void)from; (void)clockSeconds; (void)broadcast;
        return Outcome::Ignored;
    }

    // Client: a game message from the server.
    virtual Outcome onServerMessage(uint8_t type, ByteReader& reader) {
        (void)type; (void)reader;
        return Outcome::Ignored;
    }
};

constexpr bool isGameMessageId(uint8_t type) { return type >= 8 && type <= 12; }

} // namespace engine::net
