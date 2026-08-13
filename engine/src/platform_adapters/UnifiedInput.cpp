#include "platform_adapters/UnifiedInput.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <SDL2/SDL.h>

namespace engine::platform_adapters {

namespace {
constexpr float kAxisDeadzone = 0.15f;
}

UnifiedInput::UnifiedInput() = default;
UnifiedInput::~UnifiedInput() { shutdown(); }

bool UnifiedInput::initialize() {
    // SDL_InitSubSystem rather than SDL_Init: core::Window has likely
    // already brought up SDL_INIT_VIDEO/SDL_INIT_EVENTS, and this should
    // add game-controller support without touching those.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "UnifiedInput: SDL_InitSubSystem(GAMECONTROLLER) failed: %s\n", SDL_GetError());
        return false;
    }

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller_ = SDL_GameControllerOpen(i);
            if (controller_) {
                std::fprintf(stdout, "UnifiedInput: opened controller \"%s\"\n", SDL_GameControllerName(controller_));
                break;
            }
        }
    }
    if (!controller_) {
        std::fprintf(stdout, "UnifiedInput: no game controller connected (keyboard/mouse bindings still work)\n");
    }

    initialized_ = true;
    return true;
}

void UnifiedInput::shutdown() {
    if (controller_) {
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
    }
    if (initialized_) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        initialized_ = false;
    }
}

void UnifiedInput::bindAction(const std::string& actionName, InputBinding binding) {
    bindings_[actionName].push_back(binding);
}

void UnifiedInput::clearBindings(const std::string& actionName) {
    bindings_.erase(actionName);
    state_.erase(actionName);
}

bool UnifiedInput::sampleBinding(const InputBinding& binding, float& outAxisValue) const {
    outAxisValue = 0.0f;
    switch (binding.kind) {
        case PhysicalInputKind::KeyboardKey: {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            bool down = keys[binding.code] != 0;
            outAxisValue = down ? 1.0f : 0.0f;
            return down;
        }
        case PhysicalInputKind::MouseButton: {
            Uint32 buttons = SDL_GetMouseState(nullptr, nullptr);
            bool down = (buttons & SDL_BUTTON(binding.code)) != 0;
            outAxisValue = down ? 1.0f : 0.0f;
            return down;
        }
        case PhysicalInputKind::GamepadButton: {
            if (!controller_) return false;
            bool down = SDL_GameControllerGetButton(controller_, static_cast<SDL_GameControllerButton>(binding.code)) != 0;
            outAxisValue = down ? 1.0f : 0.0f;
            return down;
        }
        case PhysicalInputKind::GamepadAxis: {
            if (!controller_) return false;
            Sint16 raw = SDL_GameControllerGetAxis(controller_, static_cast<SDL_GameControllerAxis>(binding.code));
            outAxisValue = raw / 32767.0f;
            return std::fabs(outAxisValue) > kAxisDeadzone;
        }
    }
    return false;
}

void UnifiedInput::setRelativeMouseMode(bool enabled) {
    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
    if (enabled) {
        // Discard whatever accumulated before capture started -- SDL
        // reports the *first* post-enable delta relative to wherever the
        // cursor happened to be, which would otherwise show up as one
        // large, spurious look-snap.
        int dx = 0, dy = 0;
        SDL_GetRelativeMouseState(&dx, &dy);
    }
}

void UnifiedInput::update() {
    int dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy); // resets SDL's internal accumulator each call
    mouseDelta_ = {static_cast<float>(dx), static_cast<float>(dy)};

    int x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    mousePosition_ = {static_cast<float>(x), static_cast<float>(y)};

    for (auto& [actionName, actionBindings] : bindings_) {
        ActionState combined;
        for (const auto& binding : actionBindings) {
            float axisValue = 0.0f;
            bool down = sampleBinding(binding, axisValue);
            combined.down = combined.down || down;
            if (std::fabs(axisValue) > std::fabs(combined.axisValue)) {
                combined.axisValue = axisValue;
            }
        }
        state_[actionName] = combined;
    }
}

bool UnifiedInput::isActionDown(const std::string& actionName) const {
    auto it = state_.find(actionName);
    return it != state_.end() && it->second.down;
}

float UnifiedInput::actionAxisValue(const std::string& actionName) const {
    auto it = state_.find(actionName);
    return it != state_.end() ? it->second.axisValue : 0.0f;
}

void UnifiedInput::setHapticStrength(float strength01) {
    if (!controller_) return; // no-op without a connected controller -- Principle 2
    Uint16 magnitude = static_cast<Uint16>(std::clamp(strength01, 0.0f, 1.0f) * 0xFFFFu);
    SDL_GameControllerRumble(controller_, magnitude, magnitude, /*durationMs=*/200);
}

void UnifiedInput::setAdaptiveTriggerResistance(float /*resistance01*/) {
    // No-op: DualSense adaptive-trigger resistance profiles need raw HID
    // output reports (SDL_GameControllerSendEffect, backend/version-
    // dependent) beyond what this SDL2 build reliably exposes portably.
    // Real support is a PlayStationAdapter-specific extension point once
    // that adapter has a real backend (see adapters/README.md) -- until
    // then this stays a documented no-op rather than a partially-working
    // guess, per Principle 2.
}

} // namespace engine::platform_adapters
