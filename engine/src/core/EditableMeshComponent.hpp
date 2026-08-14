#pragma once

#include <utility>

#include "core/EditableMesh.hpp"

namespace engine::core {

// Attaches a real, CPU-retained, editable mesh to an entity -- kept
// separate from Components.hpp (included nearly everywhere) rather than
// added to it, since EditableMesh pulls in real topology-editing code
// only studio::plugins::ModelingModePlugin actually needs; every other
// consumer of Components.hpp doesn't pay for it.
struct EditableMeshComponent {
    EditableMesh mesh;
    // Real UI selection state -- which face/edge Modeling Mode's own
    // sidebar currently has picked, persisted here (not plugin-local)
    // so it survives switching the Explorer selection away and back.
    size_t selectedFace = 0;
    std::pair<uint32_t, uint32_t> selectedEdge{0, 0};
};

} // namespace engine::core
