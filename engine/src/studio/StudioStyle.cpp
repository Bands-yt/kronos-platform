#include "studio/StudioStyle.hpp"

#include "core/UITheme.hpp"

namespace engine::studio {

// Kronos ("UI/UX Revamp"): the real palette/rounding/spacing this
// function used to own directly now lives in core::applyKronosUITheme()
// (core/UITheme.hpp) instead -- moved, not duplicated, so
// runtime::RuntimeShell's own ImGui context can share the exact same
// real Kronos visual identity instead of drifting from Studio's. This
// function is kept as a thin, real pass-through rather than deleted
// outright so studio::initImGuiVulkanBackend()'s own existing call site
// keeps compiling unchanged.
void applyStudioStyle() { core::applyKronosUITheme(); }

} // namespace engine::studio
