#include "studio/StudioStyle.hpp"

#include "core/UITheme.hpp"

namespace engine::studio {

void applyStudioStyle(StudioAccent accent) { core::applyKronosUITheme(core::UIAccent{accent.r, accent.g, accent.b}); }

} // namespace engine::studio
