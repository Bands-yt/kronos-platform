#pragma once

#include <cstddef>
#include <deque>
#include <string>

#include "net/NetTypes.hpp"

namespace engine::moderation {

// Sprint 12 task 1's "Add server-side chat logs". One entry per real
// chat message the server accepted (rate-limit/world-safety rejections
// never reach here -- there is nothing to log about a message that was
// never processed). `text` is always the real, original, uncensored
// message -- a moderation log that only stored the censored text would
// be useless as evidence for exactly the messages worth reviewing.
// `containedProfanity`/`flaggedByClassifier` record what the real-time
// filters *decided*, so a reviewer can see both the raw text and the
// system's real judgment on it side by side.
struct ChatLogEntry {
    net::PlayerId sender = net::kInvalidPlayer;
    std::string text;
    bool containedProfanity = false;
    bool flaggedByClassifier = false;
    double serverTimestampSeconds = 0.0;
};

// Real, bounded (ring-buffer) log -- a live chat can run indefinitely,
// so this caps real memory use the same way
// net::RemoteEntityInterpolator/core::Profiler's own event logs already
// bound their own real, unbounded-otherwise event streams.
class ChatLog {
public:
    explicit ChatLog(size_t maxEntries = 1000) : maxEntries_(maxEntries) {}

    void record(ChatLogEntry entry);

    [[nodiscard]] const std::deque<ChatLogEntry>& entries() const { return entries_; }
    [[nodiscard]] size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); }

private:
    std::deque<ChatLogEntry> entries_;
    size_t maxEntries_;
};

} // namespace engine::moderation
