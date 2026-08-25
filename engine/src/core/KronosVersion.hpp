#pragma once

namespace engine::core {

// Kronos ("Branding + Release Prep"): the real, single source of truth
// for "what version of Kronos is this" -- both engine_runtime (About
// panel) and studio (About panel, README generator) read this one
// constant rather than each hand-writing their own version string that
// could drift out of sync. Bump this by hand for each real alpha build;
// there is no build-numbering/CI-stamping system in this codebase to
// derive it from automatically.
//
// Kronos ("In-App Auto-Updater"): this is also the real value the
// startup update check compares against the latest published GitHub
// release (core::checkForUpdate()), which makes keeping it in step with
// the real released tag load-bearing rather than cosmetic -- it was
// found still reading "0.1.0-alpha" after v0.2.0-alpha had already been
// tagged, which would have made every user of that build be offered an
// "update" to the release they were already running. Bump this in the
// same commit that cuts a real tag.
inline constexpr const char* kKronosVersion = "0.2.3-alpha";

// Real, honest build-timestamp -- __DATE__/__TIME__ are the compiler's
// own real values for when this translation unit was actually compiled,
// not a fabricated/hand-maintained string that could go stale.
inline constexpr const char* kKronosBuildDate = __DATE__ " " __TIME__;

} // namespace engine::core
