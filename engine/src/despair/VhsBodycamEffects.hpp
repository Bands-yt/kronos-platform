#pragma once

namespace engine::despair {

// PROJECT: DESPAIR's "VHS / Analog Bodycam" static-noise burst -- pure,
// headless, no ECS/Vulkan dependency (same split despair::isPlayerCaught()
// already establishes for its own proximity check), so the actual distance
// math is independently unit-testable even though the composite.frag
// shader it feeds isn't. Application.cpp's own post-physics hook resolves
// which AI position is the nearest real threat (only a Hunting Tormentor
// or an armed Culler counts -- see isPlayerCaught()'s own header comment)
// before calling this, exactly the same "caller resolves the threat,
// callee just does the geometry" split isPlayerCaught() itself uses.
//
// Full burst (1.0) at or inside burstRadius, fading linearly to 0 by
// fadeRadius, and a real, exact 0.0 with no threat in range at all
// (nearestThreatDistance < 0.0, this function's "no threat" sentinel --
// callers pass a negative distance when no Hunting Tormentor/Culler exists
// this tick, e.g. -1.0).
[[nodiscard]] float computeVhsStaticNoiseIntensity(float nearestThreatDistance, float burstRadius = 4.0f,
                                                    float fadeRadius = 10.0f);

} // namespace engine::despair
