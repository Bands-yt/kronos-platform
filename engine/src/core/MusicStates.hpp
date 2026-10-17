#pragma once

#include <algorithm>
#include <map>
#include <string>

namespace engine::core {

// Music states with timed crossfades ("explore" -> "combat" over 2 s).
// update() returns the gain for each state; gains of the fading pair always sum to 1.
class MusicStateMachine {
public:
    void setFadeSeconds(float s) { fade_ = std::max(s, 0.0f); }

    void setState(const std::string& name) {
        if (name == target_) return;
        if (current_.empty()) {
            current_ = target_ = name;
            progress_ = 1.0f;
            return;
        }
        from_ = target_; // a fade cut short restarts from the state it was heading to
        target_ = name;
        progress_ = 0.0f;
    }

    void update(float dt) {
        if (progress_ >= 1.0f) return;
        progress_ = fade_ <= 0.0f ? 1.0f : std::min(1.0f, progress_ + dt / fade_);
        if (progress_ >= 1.0f) current_ = target_;
    }

    [[nodiscard]] float gainOf(const std::string& state) const {
        if (target_.empty()) return 0.0f;
        if (progress_ >= 1.0f) return state == target_ ? 1.0f : 0.0f;
        if (state == target_) return progress_;
        if (state == from_) return 1.0f - progress_;
        return 0.0f;
    }

    [[nodiscard]] const std::string& target() const { return target_; }
    [[nodiscard]] bool fading() const { return progress_ < 1.0f; }

private:
    std::string current_, target_, from_;
    float fade_ = 1.0f;
    float progress_ = 1.0f;
};

} // namespace engine::core
