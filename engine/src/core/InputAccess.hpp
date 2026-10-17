#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace engine::core::inputaccess {

// Hold or toggle (880): the same action works either way, depending on the player's setting.
class HoldOrToggle {
public:
    explicit HoldOrToggle(bool toggleMode = false) : toggle_(toggleMode) {}
    void setToggleMode(bool on) {
        toggle_ = on;
        active_ = false;
    }
    // Feed the raw button state every frame; returns whether the action is on.
    bool update(bool buttonDown) {
        if (!toggle_) active_ = buttonDown;
        else if (buttonDown && !wasDown_) active_ = !active_;
        wasDown_ = buttonDown;
        return active_;
    }

private:
    bool toggle_, active_ = false, wasDown_ = false;
};

// Captions (885) and UI scale (886, 6), cursor (887): settings with safe limits.
struct CaptionStyle {
    float textScale = 1.0f;
    float backgroundOpacity = 0.6f;
    bool speakerNames = true;
};

inline CaptionStyle clampCaptionStyle(CaptionStyle s) {
    s.textScale = std::clamp(s.textScale, 0.75f, 2.5f);
    s.backgroundOpacity = std::clamp(s.backgroundOpacity, 0.0f, 1.0f);
    return s;
}

inline float clampUiScale(float scale) { return std::clamp(scale, 0.75f, 2.0f); }

// UI scale suggested from the display: DPI scale, never below 1 on a 4K screen, never tiny text on small windows.
inline float suggestedUiScale(float dpiScale, int windowHeightPx) {
    float s = dpiScale;
    if (windowHeightPx >= 1800 && s < 1.5f) s = 1.5f;
    if (windowHeightPx <= 720 && s > 1.0f) s = 1.0f;
    return clampUiScale(s);
}

struct FocusStyle {
    float outlineThickness = 2.0f;
    float cursorScale = 1.0f;
};

inline FocusStyle clampFocusStyle(FocusStyle s) {
    s.outlineThickness = std::clamp(s.outlineThickness, 1.0f, 6.0f);
    s.cursorScale = std::clamp(s.cursorScale, 1.0f, 3.0f);
    return s;
}

// Button glyphs by device family (890).
enum class Device { Keyboard, Xbox, PlayStation, Nintendo };

inline std::string glyphFor(Device d, const std::string& action) {
    static const std::map<std::string, std::vector<std::string>> table = {
        {"confirm", {"Enter", "A", "Cross", "B"}}, {"cancel", {"Esc", "B", "Circle", "A"}}, {"jump", {"Space", "A", "Cross", "B"}},
        {"interact", {"E", "X", "Square", "Y"}},   {"menu", {"Esc", "Menu", "Options", "+"}},
    };
    auto it = table.find(action);
    if (it == table.end()) return action;
    return it->second[static_cast<std::size_t>(d)];
}

// Input contexts (891): the newest pushed context handles an action first; "blocks" stops it falling through.
class ActionMap {
public:
    struct Context {
        std::string name;
        std::map<std::string, std::string> bindings; // key -> action
        bool blocks = false;
    };
    void push(Context c) { stack_.push_back(std::move(c)); }
    void pop() {
        if (!stack_.empty()) stack_.pop_back();
    }
    [[nodiscard]] std::string resolve(const std::string& key) const {
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
            auto b = it->bindings.find(key);
            if (b != it->bindings.end()) return b->second;
            if (it->blocks) break;
        }
        return {};
    }

private:
    std::vector<Context> stack_;
};

// Last-used device (892): glyphs and cursor follow whichever device the player touched most recently.
class DeviceTracker {
public:
    void onInput(Device d, double now) {
        if (d != current_ && now - lastSwitch_ > 0.25) { // ignore a quick stray tap on another device
            current_ = d;
            lastSwitch_ = now;
        }
    }
    [[nodiscard]] Device current() const { return current_; }

private:
    Device current_ = Device::Keyboard;
    double lastSwitch_ = -1;
};

// Virtual joystick (893): touch position to a movement vector with a dead zone, clamped to 1.
struct Stick {
    float x = 0, y = 0;
};

inline Stick virtualStick(float centerX, float centerY, float touchX, float touchY, float radiusPx, float deadZone = 0.15f) {
    float dx = (touchX - centerX) / radiusPx, dy = (touchY - centerY) / radiusPx;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < deadZone) return {};
    if (len > 1.0f) {
        dx /= len;
        dy /= len;
    }
    return {dx, dy};
}

// Gestures (894, 908): classifies a finished touch as tap, long press or swipe.
enum class Gesture { Tap, LongPress, SwipeLeft, SwipeRight, SwipeUp, SwipeDown };

inline Gesture classifyGesture(float dx, float dy, double seconds, float tapSlopPx = 12.0f, double longPress = 0.5) {
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < tapSlopPx) return seconds >= longPress ? Gesture::LongPress : Gesture::Tap;
    if (std::fabs(dx) > std::fabs(dy)) return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
}

// Saved per user (895): simple key=value text.
struct AccessSettings {
    bool reducedMotion = false, toggleSprint = false, captions = false;
    float uiScale = 1.0f, captionScale = 1.0f;
};

inline std::string serializeSettings(const AccessSettings& s) {
    std::ostringstream out;
    out << "reducedMotion=" << s.reducedMotion << "\ntoggleSprint=" << s.toggleSprint << "\ncaptions=" << s.captions << "\nuiScale=" << s.uiScale
        << "\ncaptionScale=" << s.captionScale << "\n";
    return out.str();
}

inline AccessSettings parseSettings(const std::string& text) {
    AccessSettings s;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "reducedMotion") s.reducedMotion = v == "1";
        else if (k == "toggleSprint") s.toggleSprint = v == "1";
        else if (k == "captions") s.captions = v == "1";
        else if (k == "uiScale") s.uiScale = clampUiScale(static_cast<float>(std::atof(v.c_str())));
        else if (k == "captionScale") s.captionScale = clampCaptionStyle({static_cast<float>(std::atof(v.c_str())), 0.5f, true}).textScale;
    }
    return s;
}

// Input latency (898): average and 95th percentile from per-event delays in milliseconds.
struct LatencyStats {
    double averageMs = 0, p95Ms = 0, maxMs = 0;
};

inline LatencyStats latencyStats(std::vector<double> ms) {
    LatencyStats s;
    if (ms.empty()) return s;
    std::sort(ms.begin(), ms.end());
    double sum = 0;
    for (double v : ms) sum += v;
    s.averageMs = sum / static_cast<double>(ms.size());
    s.p95Ms = ms[std::min(ms.size() - 1, static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(ms.size())) - 1))];
    s.maxMs = ms.back();
    return s;
}

// Power policy (910, 912, 925).
struct PowerState {
    bool onBattery = false;
    float batteryPercent = 100;
    bool thermalThrottled = false;
    bool inBackground = false;
};

inline int frameRateCap(const PowerState& p, int normalCap = 60) {
    if (p.inBackground) return 5;
    if (p.thermalThrottled || (p.onBattery && p.batteryPercent < 20)) return 30;
    if (p.onBattery) return std::min(normalCap, 45);
    return normalCap;
}

inline bool shouldPauseAudio(const PowerState& p, bool gameWantsBackgroundAudio) { return p.inBackground && !gameWantsBackgroundAudio; }

inline std::string qualityHint(const PowerState& p) {
    if (p.thermalThrottled) return "The device is hot: lowering quality will cool it down.";
    if (p.onBattery && p.batteryPercent < 20) return "Battery is low: use the battery saver (30 fps).";
    return {};
}

} // namespace engine::core::inputaccess
