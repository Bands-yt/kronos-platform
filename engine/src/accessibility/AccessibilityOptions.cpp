#include "accessibility/AccessibilityOptions.hpp"

namespace engine::accessibility {

std::string AccessibilityOptions::colorblindLutAssetPath() const {
    switch (colorblindMode_) {
        case ColorblindMode::Protanopia: return "engine/luts/colorblind_protanopia.dds";
        case ColorblindMode::Deuteranopia: return "engine/luts/colorblind_deuteranopia.dds";
        case ColorblindMode::Tritanopia: return "engine/luts/colorblind_tritanopia.dds";
        case ColorblindMode::None: default: return "";
    }
}

void AccessibilityOptions::remapAction(platform_adapters::UnifiedInput& input, const std::string& actionName,
                                        platform_adapters::InputBinding newBinding) const {
    input.clearBindings(actionName);
    input.bindAction(actionName, newBinding);
}

} // namespace engine::accessibility
