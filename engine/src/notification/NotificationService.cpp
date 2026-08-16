#include "notification/NotificationService.hpp"

#include <chrono>

namespace engine::notification {

void push(core::LocalProfile& profile, core::NotificationKind kind, std::string title, std::string body,
          std::string relatedId) {
    core::NotificationRecord record;
    record.kind = kind;
    record.title = std::move(title);
    record.body = std::move(body);
    record.relatedId = std::move(relatedId);
    record.timestampUnixSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    record.read = false;
    profile.notifications.push_back(std::move(record));
    if (profile.notifications.size() > kMaxStoredNotifications) {
        profile.notifications.erase(profile.notifications.begin(),
                                     profile.notifications.begin() +
                                         static_cast<long>(profile.notifications.size() - kMaxStoredNotifications));
    }
}

bool markRead(core::LocalProfile& profile, size_t index) {
    if (index >= profile.notifications.size()) return false;
    profile.notifications[index].read = true;
    return true;
}

void markAllRead(core::LocalProfile& profile) {
    for (auto& n : profile.notifications) n.read = true;
}

size_t unreadCount(const core::LocalProfile& profile) {
    size_t count = 0;
    for (const auto& n : profile.notifications) {
        if (!n.read) ++count;
    }
    return count;
}

std::vector<size_t> filteredIndicesMostRecentFirst(const core::LocalProfile& profile,
                                                     std::optional<core::NotificationKind> kindFilter,
                                                     bool unreadOnly) {
    std::vector<size_t> result;
    for (size_t i = profile.notifications.size(); i-- > 0;) {
        const core::NotificationRecord& n = profile.notifications[i];
        if (kindFilter.has_value() && n.kind != *kindFilter) continue;
        if (unreadOnly && n.read) continue;
        result.push_back(i);
    }
    return result;
}

} // namespace engine::notification
