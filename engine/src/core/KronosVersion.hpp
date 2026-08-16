#pragma once

namespace engine::core {

// Kronos ("Branding + Release Prep"): the real, single source of truth
// for "what version of Kronos is this" -- both engine_runtime (About
// panel) and studio (About panel, README generator) read this one
// constant rather than each hand-writing their own version string that
// could drift out of sync. Bump this by hand for each real alpha build;
// there is no build-numbering/CI-stamping system in this codebase to
// derive it from automatically.
inline constexpr const char* kKronosVersion = "0.1.0-alpha";

// Real, honest build-timestamp -- __DATE__/__TIME__ are the compiler's
// own real values for when this translation unit was actually compiled,
// not a fabricated/hand-maintained string that could go stale.
inline constexpr const char* kKronosBuildDate = __DATE__ " " __TIME__;

} // namespace engine::core
