#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/LocalProfile.hpp"

namespace engine::notification {

// Kronos ("Notifications System"): the real, pure (headlessly-testable)
// persistence/query logic behind the Notification Center panel and toast
// popups -- same "free functions over core::LocalProfile&" shape
// social::FriendsService already establishes for the same real reason:
// core::LocalProfile stays a plain data struct, the behavior lives next
// to the feature that owns it, not inside core/.
//
// Real cap -- oldest notifications are trimmed first once the log
// exceeds this, same "don't grow unbounded" reasoning
// social::kMaxStoredFriendMessages already applies to messages.
constexpr size_t kMaxStoredNotifications = 200;

// Appends a real, new, unread notification and enforces the cap above.
// Real, honest scope: this only persists the record -- it does NOT by
// itself pop a toast (a toast is transient UI state, not persisted data;
// see runtime::RuntimeShell's own pendingToasts_ for that half).
void push(core::LocalProfile& profile, core::NotificationKind kind, std::string title, std::string body,
          std::string relatedId = std::string());

// Index is into profile.notifications (0 = oldest). Real, honest no-op
// (returns false) on an out-of-range index.
bool markRead(core::LocalProfile& profile, size_t index);
void markAllRead(core::LocalProfile& profile);

[[nodiscard]] size_t unreadCount(const core::LocalProfile& profile);

// Real indices into profile.notifications, most-recent-first, optionally
// restricted to one NotificationKind and/or unread-only -- what the
// Notification Center panel's own filter dropdown/tab reads. Indices
// (not copies) so the panel's "Mark read" button can call markRead()
// against the exact same real record it just displayed.
[[nodiscard]] std::vector<size_t> filteredIndicesMostRecentFirst(const core::LocalProfile& profile,
                                                                   std::optional<core::NotificationKind> kindFilter,
                                                                   bool unreadOnly);

} // namespace engine::notification
