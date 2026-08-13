#pragma once

namespace engine::studio {

// Applies Studio's own visual identity on top of ImGui::StyleColorsDark()
// (called once, right after it, in StudioApp::initImGuiVulkanBackend()) --
// consistent rounding/spacing across every panel and a single curated
// accent color used for every interactive element (buttons, active tabs,
// active headers, slider grabs, checkmarks), instead of each panel
// picking its own ad hoc ImGui::PushStyleColor() calls (MaterialPlugin's
// preset swatches are a deliberate, stated exception -- see its own
// comment -- everything else in Studio goes through this one palette).
// Pure ImGui::GetStyle() mutation, no fonts/textures touched -- see
// StudioIcons.hpp for the separate, vector-drawn icon system this
// pairs with instead of an icon font (see that header's comment on why).
void applyStudioStyle();

} // namespace engine::studio
