#pragma once

#include <deque>
#include <string>

#include "net/NetTypes.hpp"

namespace engine::moderation {

// Kronos ("Moderation Architecture v2", "DM System v1"): the real,
// server-side audit record for direct messages -- same real shape/
// precedent as moderation::ChatLogEntry/ChatLog (real, uncensored text
// for moderation review; real, bounded ring-buffer, a live DM stream can
// run indefinitely same as chat can), with a real `recipient` field
// chat doesn't need (chat is a broadcast, a DM is one-to-one).
struct DirectMessageLogEntry {
    net::PlayerId sender = net::kInvalidPlayer;
    net::PlayerId recipient = net::kInvalidPlayer;
    std::string text;
    bool flaggedByClassifier = false;
    bool blocked = false; // real, honest record of whether this DM was real-hard-blocked (see PolicyEngine) rather than delivered
    double serverTimestampSeconds = 0.0;
};

class DirectMessageLog {
public:
    explicit DirectMessageLog(size_t maxEntries = 1000) : maxEntries_(maxEntries) {}

    void record(DirectMessageLogEntry entry);

    [[nodiscard]] const std::deque<DirectMessageLogEntry>& entries() const { return entries_; }
    [[nodiscard]] size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); }

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::deque<DirectMessageLogEntry> entries_;
    size_t maxEntries_;
};

} // namespace engine::moderation
