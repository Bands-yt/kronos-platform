#pragma once

#include <string>

#include "platform_adapters/UnifiedInput.hpp"

namespace engine::accessibility {

enum class ColorblindMode { None, Protanopia, Deuteranopia, Tritanopia };

// docs/ARCHITECTURE.md §12: "a shared CameraAccessibilityOptions object
// (FOV control, reduced shake/head-bob, comfort vignette-on-turn,
// snap-turn) that any developer camera script can opt into, so
// accessibility doesn't require every developer to reimplement it from
// scratch." Plain data -- no camera system exists yet to read it (that's
// gameplay/Scripting-layer code, per §6's TODO on the Instance/DataModel
// layer not existing), but the shared shape is the point: one struct every
// future camera implementation reads the same way.
struct CameraAccessibilityOptions {
    float fieldOfViewDegrees = 90.0f;
    float cameraShakeScale = 1.0f; // 0 disables shake entirely
    bool reducedHeadBob = false;
    bool comfortVignetteOnTurn = false;
    bool snapTurn = false;
    float snapTurnIncrementDegrees = 45.0f;
};

// docs/ARCHITECTURE.md §12's accessibility surface, minus the two pieces
// that are already fully solved elsewhere by construction rather than
// needing dedicated code here:
//   - Remappable controls: UnifiedInput's binding table (§8.2) already IS
//     the remapping mechanism, because it had to be data-driven for
//     cross-platform input anyway. remapAction() below is a thin
//     forwarding call, not a second system.
//   - Haptic feedback: UnifiedInput::setHapticStrength already exists
//     (§8.2/§12) and no-ops where hardware lacks it (Principle 2); nothing
//     to add here.
// What's left -- text scale, colorblind/high-contrast mode, camera
// comfort options -- are plain settings state with no renderer/camera
// system built yet to consume them (see the render-graph post-process TODO
// below and CameraAccessibilityOptions' comment).
class AccessibilityOptions {
public:
    void setTextScale(float scale) { textScale_ = scale; }
    [[nodiscard]] float textScale() const { return textScale_; }

    void setColorblindMode(ColorblindMode mode) { colorblindMode_ = mode; }
    [[nodiscard]] ColorblindMode colorblindMode() const { return colorblindMode_; }

    // Maps the selected mode to an asset-path convention -- the LUT
    // texture itself doesn't exist yet. This is the selection state
    // docs/ARCHITECTURE.md §12 says attaches "as optional post-process LUT
    // passes inserted into the existing render-graph post-processing
    // stage (§4.1)"; Renderer::renderFrame() has no post-process stage
    // yet for it to bind into (see Renderer.cpp's own TODO on that).
    [[nodiscard]] std::string colorblindLutAssetPath() const;

    void setHighContrast(bool enabled) { highContrast_ = enabled; }
    [[nodiscard]] bool highContrast() const { return highContrast_; }

    // Forwards to UnifiedInput -- see class comment. Exists as the
    // accessibility-facing entry point a settings UI would call, so that
    // UI code depends on "accessibility::remap an action" rather than
    // reaching into platform_adapters directly.
    void remapAction(platform_adapters::UnifiedInput& input, const std::string& actionName,
                      platform_adapters::InputBinding newBinding) const;

    [[nodiscard]] CameraAccessibilityOptions& camera() { return camera_; }
    [[nodiscard]] const CameraAccessibilityOptions& camera() const { return camera_; }

private:
    float textScale_ = 1.0f;
    ColorblindMode colorblindMode_ = ColorblindMode::None;
    bool highContrast_ = false;
    CameraAccessibilityOptions camera_;
};

} // namespace engine::accessibility
