#pragma once

#include <glm/glm.hpp>

namespace engine::studio {

// Kronos ("Developer Velocity Sprint" -- "Live Math Evaluation"): a
// real, right-click "Enter Formula" popup for a vec3 field -- three
// independent per-component formula inputs (X/Y/Z), each evaluated via
// core::evaluateMathExpression() on Apply/Enter, blank = leave that
// component unchanged. A separate popup, not inline text-edit support
// inside ImGui::DragFloat3 itself: Dear ImGui's own DragFloat text-edit
// mode parses via a bare sscanf (imgui_widgets.cpp's own "this is not a
// full expression evaluator" comment) and never exposes the raw text a
// user typed back to a caller, so real "+10"/"*2"/"180 - 45" support
// needs to own its own text buffer -- this class does.
//
// One instance per real field (InspectorPanel owns one each for
// Position/Rotation/Scale) -- state (the typed text) is real,
// per-instance, not shared/global.
class Vec3MathExpressionPopup {
public:
    // Call once, the same frame a right-click on the field is detected
    // -- resets the buffers to blank and arranges for the matching
    // ImGui::OpenPopup(popupId) call to fire on the very next draw().
    void open() {
        xBuffer_[0] = yBuffer_[0] = zBuffer_[0] = '\0';
        shouldOpen_ = true;
    }

    // Call every frame after open() -- a real no-op whenever this
    // popup isn't open. Returns true the frame Apply/Enter genuinely
    // changed `value`.
    bool draw(const char* popupId, glm::vec3* value);

private:
    bool shouldOpen_ = false;
    char xBuffer_[32] = "";
    char yBuffer_[32] = "";
    char zBuffer_[32] = "";
};

} // namespace engine::studio
