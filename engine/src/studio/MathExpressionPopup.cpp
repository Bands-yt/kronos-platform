#include "studio/MathExpressionPopup.hpp"

#include <imgui.h>

#include "core/MathExpression.hpp"

namespace engine::studio {

bool Vec3MathExpressionPopup::draw(const char* popupId, glm::vec3* value) {
    if (shouldOpen_) {
        ImGui::OpenPopup(popupId);
        shouldOpen_ = false;
    }

    bool changed = false;
    if (ImGui::BeginPopup(popupId)) {
        ImGui::TextDisabled("Formula per axis (e.g. +10, *2, /3, 180 - 45). Blank = unchanged.");
        ImGui::SetNextItemWidth(120.0f);
        bool enterPressed = ImGui::InputText("X", xBuffer_, sizeof(xBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetNextItemWidth(120.0f);
        enterPressed |= ImGui::InputText("Y", yBuffer_, sizeof(yBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetNextItemWidth(120.0f);
        enterPressed |= ImGui::InputText("Z", zBuffer_, sizeof(zBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::Button("Apply") || enterPressed) {
            float result = 0.0f;
            if (xBuffer_[0] != '\0' && core::evaluateMathExpression(xBuffer_, value->x, result)) {
                value->x = result;
                changed = true;
            }
            if (yBuffer_[0] != '\0' && core::evaluateMathExpression(yBuffer_, value->y, result)) {
                value->y = result;
                changed = true;
            }
            if (zBuffer_[0] != '\0' && core::evaluateMathExpression(zBuffer_, value->z, result)) {
                value->z = result;
                changed = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return changed;
}

} // namespace engine::studio
