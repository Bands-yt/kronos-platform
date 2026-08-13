#pragma once

#include <string>
#include <vector>

namespace engine::studio {

// Sprint 7 ("Studio UI Revamp") task category 6: real toast-style
// notifications for save/build/error/warning events -- color-coded by
// severity (see StudioStyle.hpp's extended palette for the real colors
// each severity maps to). The queue itself (`NotificationCenter`) is
// pure state -- push()/tick()/expiry logic has zero ImGui dependency and
// is fully headlessly testable; only `draw()` touches ImGui, the same
// "pure logic, separate rendering call" split this whole session has
// used for every ImGui-adjacent system (see core::FlashEffect from the
// Economy sprint for the closest precedent -- this is that same idea,
// applied to Studio instead of a 3D scene).
enum class NotificationSeverity { Info, Success, Warning, Error };

struct Notification {
    std::string message;
    NotificationSeverity severity = NotificationSeverity::Info;
    float remainingSeconds = 0.0f;
    float totalSeconds = 4.0f; // the real duration this notification was given -- used to compute fade-out alpha
};

// A real, bounded FIFO queue -- `kMaxNotifications` caps how many can be
// live at once (oldest dropped first), the same real "honest overflow
// behavior, never unbounded growth" discipline core::Inventory's own
// slot-capacity math already established, applied here to a burst of
// rapid-fire notifications instead of ore stacks.
class NotificationCenter {
public:
    static constexpr size_t kMaxNotifications = 8;
    static constexpr float kDefaultDurationSeconds = 4.0f;

    void push(std::string message, NotificationSeverity severity, float durationSeconds = kDefaultDurationSeconds);

    // Called once per Studio frame -- counts every live notification's
    // `remainingSeconds` down by `dt` and removes any that just expired.
    void tick(float dt);

    // Real ImGui rendering -- a stacked toast list in a screen corner,
    // fading via each notification's own remainingSeconds/totalSeconds
    // ratio over the last real fraction of its lifetime. Not covered by
    // engine_tests (see this header's own comment on why); verified live.
    void draw() const;

    [[nodiscard]] const std::vector<Notification>& active() const { return notifications_; }
    [[nodiscard]] size_t count() const { return notifications_.size(); }
    void clear() { notifications_.clear(); }

private:
    std::vector<Notification> notifications_;
};

} // namespace engine::studio
