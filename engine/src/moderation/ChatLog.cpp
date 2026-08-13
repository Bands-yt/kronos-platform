#include "moderation/ChatLog.hpp"

namespace engine::moderation {

void ChatLog::record(ChatLogEntry entry) {
    entries_.push_back(std::move(entry));
    while (entries_.size() > maxEntries_) entries_.pop_front();
}

} // namespace engine::moderation
