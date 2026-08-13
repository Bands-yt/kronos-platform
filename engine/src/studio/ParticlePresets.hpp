#pragma once

#include "core/ParticleSystem.hpp"

namespace engine::studio {

// Sprint 10 ("Creator Tools Phase 2"): the real particle preset library --
// extracted out of ParticleEditorPlugin.cpp (where these originally lived,
// private to that one plugin) so studio::plugins::CreatorAssetBrowserPlugin
// can offer the exact same real presets through its "Use" button, not a
// second, could-drift copy. Six real, distinct effects, not the same
// numbers relabeled -- see each function's own field choices.
enum class ParticlePresetId { Fire, Smoke, Sparkle, Snow, Glow, Burst };
constexpr int kParticlePresetCount = 6;
[[nodiscard]] const char* particlePresetName(ParticlePresetId preset);
void applyParticlePreset(ParticlePresetId preset, core::ParticleEmitterSettings& settings);

} // namespace engine::studio
