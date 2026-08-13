#include "core/ECS.hpp"

#include <cstdio>

namespace engine::core {

// Most of ECS is a template-heavy header (EnTT views are compile-time
// constructs, so there's little to hide in a .cpp). This translation unit
// exists for the handful of things that don't need to be inline -- starting
// with a debug dump used by Studio's Output panel (§5) and by
// engine_runtime's own startup log.
void logEcsStats(ECS& ecs) {
    std::fprintf(stdout, "ECS: %zu live entities\n", ecs.entityCount());
}

} // namespace engine::core
