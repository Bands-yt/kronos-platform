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
    // Bumped by any mutator that changes `mesh` outside Studio's own
    // ImGui buttons -- today, only core::ScriptMeshApi's Luau bindings.
    // ModelingModePlugin::update() runs every frame regardless of panel
    // visibility (IStudioPlugin's own convention) and re-uploads to the
    // GPU whenever an entity's editVersion has moved past what it last
    // uploaded, which is what makes a script-driven mesh edit show up in
    // the viewport even with the Modeling Mode panel closed. Studio's
    // own drawPanel() buttons keep calling reuploadMesh() directly for
    // zero-latency feedback and never touch this field -- see
    // ModelingModePlugin.cpp's own comment on why that leaves the sweep
    // a no-op for UI-only edits instead of a redundant double-upload.
    uint64_t editVersion = 0;
};

} // namespace engine::core
