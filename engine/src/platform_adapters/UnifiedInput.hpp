#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

struct _SDL_GameController;
using SDL_GameController = _SDL_GameController;

namespace engine::platform_adapters {

enum class PhysicalInputKind { KeyboardKey, MouseButton, GamepadButton, GamepadAxis };

struct InputBinding {
    PhysicalInputKind kind;
    int code; // SDL_Scancode / SDL mouse button index / SDL_GameControllerButton / SDL_GameControllerAxis
};

// docs/ARCHITECTURE.md §8.2's InputAction abstraction: scripts/UI bind to
// logical action names ("Jump", "Fire"), never to physical device codes.
// Every binding lives in one data-driven table (bindAction()), which is
// also what makes accessibility's remappable controls (§12) free --
// remapping is "call bindAction() again with a different InputBinding",
// not a second system built later.
//
// Backed by SDL2's real keyboard/mouse/game-controller APIs (already a
// dependency), so keyboard/mouse/gamepad polling and controller rumble are
// genuinely functional on desktop here, not stubbed. Touch, gyro, and
// PlayStation adaptive triggers are the platform-specific extensions
// Principle 2 requires to no-op cleanly where hardware doesn't support
// them -- see UnifiedInput.cpp for exactly where each does that.
class UnifiedInput {
public:
    UnifiedInput();
    ~UnifiedInput();

    [[nodiscard]] bool initialize();
    void shutdown();

    void bindAction(const std::string& actionName, InputBinding binding);
    void clearBindings(const std::string& actionName);

    // Call once per frame, after core::Window::pumpEvents() -- samples
    // current physical device state for every bound action.
    void update();

    [[nodiscard]] bool isActionDown(const std::string& actionName) const;
    [[nodiscard]] float actionAxisValue(const std::string& actionName) const;

    // Relative mouse mode (cursor hidden + captured, motion reported as
    // deltas with no window-edge clamping) -- the standard mode for a
    // mouse-look camera. engine_runtime enables this; Studio never does,
    // since it needs a normal cursor for UI interaction (see
    // core::Application::initialize() vs. studio::StudioApp -- Studio owns
    // no UnifiedInput at all today, only engine_runtime's character
    // controller does).
    void setRelativeMouseMode(bool enabled);

    // Accumulated mouse motion since the last update() call. Only
    // meaningful while relative mouse mode is enabled.
    [[nodiscard]] glm::vec2 mouseDelta() const { return mouseDelta_; }

    // Kronos ("Active Joining UI" -- engine_runtime ImGui + input
    // integration): real, absolute window-space cursor position, updated
    // every update() call same as mouseDelta_. Only meaningful outside
    // relative mouse mode (SDL doesn't move the real OS cursor while
    // captured, so this stays pinned at wherever it was when capture
    // started). Not needed by ImGui itself -- its own SDL2 backend reads
    // absolute position independently from the raw SDL_MOUSEMOTION events
    // already forwarded via core::Window::setRawEventCallback() -- added
    // for any future native (non-ImGui) cursor/reticle rendering, closing
    // the literal "no absolute mouse position anywhere" gap this session
    // found while auditing input readiness for a real, clickable shell.
    [[nodiscard]] glm::vec2 mousePosition() const { return mousePosition_; }

    // Additive/optional per Principle 2 -- no-ops where unsupported.
    void setHapticStrength(float strength01);              // real: SDL_GameControllerRumble
    void setAdaptiveTriggerResistance(float resistance01); // stub: see .cpp

private:
    struct ActionState {
        bool down = false;
        float axisValue = 0.0f;
    };

    [[nodiscard]] bool sampleBinding(const InputBinding& binding, float& outAxisValue) const;

    std::unordered_map<std::string, std::vector<InputBinding>> bindings_;
    std::unordered_map<std::string, ActionState> state_;
    SDL_GameController* controller_ = nullptr;
    glm::vec2 mouseDelta_{0.0f};
    glm::vec2 mousePosition_{0.0f};
    bool initialized_ = false;
};

} // namespace engine::platform_adapters
