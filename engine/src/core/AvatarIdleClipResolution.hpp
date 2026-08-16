#pragma once

#include <string>

#include "core/AnimationDatabase.hpp"
#include "core/LocalProfile.hpp"

namespace engine::core {

// Kronos ("Home Screen Avatar Preview" -- "idle animation"): real, pure,
// headlessly-testable resolution logic -- which idle clip a rigged
// avatar preview should actually play, extracted so it's testable
// without the real GPU mesh-upload/skeletal-animation machinery around
// it (runtime::HomeAvatarPreview::spawnPreviewBody() is the one real,
// GPU-touching caller; see that class's own header comment for why it
// can't itself be exercised in this codebase's GPU-free test binary).
// Real, fail-soft precedence, same as core::Application::
// spawnLocalPlayerAvatar()'s own loadClip() lambda: `localProfile`'s own
// animOverrideIdleId if set AND it resolves in `animationDatabase`, else
// `shippedIdleClipPath` (the real, shipped default).
[[nodiscard]] std::string resolveAvatarIdleClipPath(const LocalProfile& localProfile,
                                                      const AnimationDatabase& animationDatabase,
                                                      const std::string& shippedIdleClipPath);

} // namespace engine::core
