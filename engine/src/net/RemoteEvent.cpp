#include "net/RemoteEvent.hpp"

#include <algorithm>
#include <cstdio>

#include "net/Serialization.hpp"

namespace engine::net {

namespace {
enum class FieldTypeTag : uint8_t { Number = 0, String = 1, Boolean = 2 };
} // namespace

bool RemoteEvent::handleFromClient(PlayerId sender, const Payload& payload) {
    if (!schema_.empty() && !validateSchema(payload)) {
        std::fprintf(stderr, "RemoteEvent[%s]: rejected payload from player=%u (schema mismatch)\n",
                     name_.c_str(), sender);
        return false;
    }

    if (maxCallsPerSecond_ > 0.0f && !checkRateLimit(sender)) {
        std::fprintf(stderr, "RemoteEvent[%s]: rejected call from player=%u (rate limit exceeded)\n",
                     name_.c_str(), sender);
        return false;
    }

    if (handler_) {
        handler_(sender, payload);
    }
    return true;
}

bool RemoteEvent::validateSchema(const Payload& payload) const {
    for (const auto& [fieldName, fieldType] : schema_) {
        auto it = payload.find(fieldName);
        if (it == payload.end()) return false; // required field missing

        bool typeMatches = false;
        switch (fieldType) {
            case FieldType::Number: typeMatches = std::holds_alternative<double>(it->second); break;
            case FieldType::String: typeMatches = std::holds_alternative<std::string>(it->second); break;
            case FieldType::Boolean: typeMatches = std::holds_alternative<bool>(it->second); break;
        }
        if (!typeMatches) return false;
    }
    // Extra fields beyond the schema are allowed through, matching how a
    // permissive additive-only validator should behave -- SetInboundSchema
    // declares a required minimum, not an exhaustive allowlist.
    return true;
}

bool RemoteEvent::checkRateLimit(PlayerId sender) {
    auto now = std::chrono::steady_clock::now();
    auto& state = rateState_[sender]; // default-constructs on first call for this player

    if (state.lastRefill.time_since_epoch().count() == 0) {
        state.tokens = maxCallsPerSecond_; // start with a full burst allowance
        state.lastRefill = now;
    } else {
        double elapsedSeconds = std::chrono::duration<double>(now - state.lastRefill).count();
        state.tokens = std::min(static_cast<double>(maxCallsPerSecond_), state.tokens + elapsedSeconds * maxCallsPerSecond_);
        state.lastRefill = now;
    }

    if (state.tokens < 1.0) {
        return false;
    }
    state.tokens -= 1.0;
    return true;
}

void serializeRemoteEventPayload(const RemoteEvent::Payload& payload, ByteWriter& writer) {
    uint8_t count = static_cast<uint8_t>(std::min<size_t>(payload.size(), kMaxRemoteEventFields));
    writer.writeU8(count);
    uint8_t written = 0;
    for (const auto& [key, value] : payload) {
        if (written >= count) break;
        writer.writeString(key);
        if (std::holds_alternative<double>(value)) {
            writer.writeU8(static_cast<uint8_t>(FieldTypeTag::Number));
            // real, documented precision narrowing (double -> float) --
            // ByteWriter has no writeDouble(), matching every other
            // float-only field this wire format already uses (Transform
            // position/rotation, etc.); more than enough precision for
            // RPC-style gameplay payloads (item counts, damage amounts,
            // coordinates), not claimed lossless for arbitrary doubles.
            writer.writeFloat(static_cast<float>(std::get<double>(value)));
        } else if (std::holds_alternative<std::string>(value)) {
            writer.writeU8(static_cast<uint8_t>(FieldTypeTag::String));
            writer.writeString(std::get<std::string>(value));
        } else {
            writer.writeU8(static_cast<uint8_t>(FieldTypeTag::Boolean));
            writer.writeBool(std::get<bool>(value));
        }
        ++written;
    }
}

RemoteEvent::Payload deserializeRemoteEventPayload(ByteReader& reader) {
    RemoteEvent::Payload payload;
    uint8_t count = reader.readU8();
    for (uint8_t i = 0; i < count && !reader.hasError(); ++i) {
        std::string key = reader.readString();
        auto tag = static_cast<FieldTypeTag>(reader.readU8());
        switch (tag) {
            case FieldTypeTag::Number: payload[key] = static_cast<double>(reader.readFloat()); break;
            case FieldTypeTag::String: payload[key] = reader.readString(); break;
            case FieldTypeTag::Boolean: payload[key] = reader.readBool(); break;
            default: return payload; // unrecognized tag -- see this function's own doc comment
        }
    }
    return payload;
}

} // namespace engine::net
