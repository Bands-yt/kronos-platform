#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/CsgMesh.hpp"
#include "core/EditableMesh.hpp"

namespace engine::core {

// Kronos ("3D DCC Modeling Suite" -- real non-destructive modifier
// stack): closes a gap a real audit of this codebase found -- Boolean
// CSG (core::booleanOp(), CsgMesh.hpp) already existed but was only ever
// applied DESTRUCTIVELY, baked straight into the base mesh with no way
// to adjust it afterward (see ModelingModePlugin::applyCsg(), which
// still does exactly that as its own, separate, deliberately-unchanged
// "quick CSG" button -- this class doesn't replace it). This is the
// honest alternative: an ORDERED LIST of modifiers over a base mesh that
// is never itself mutated -- evaluate() always returns a fresh
// EditableMesh built by replaying every enabled modifier over a copy of
// the base, so disabling/reordering/removing a modifier, or editing the
// base mesh itself, changes the final result without losing any
// information the stack holds.
//
// No caching: evaluate() always recomputes from scratch. A dirty-flag
// cache keyed on "has the base mesh changed since last call" needs a
// version/identity signal EditableMesh itself doesn't carry (unlike
// EditableMeshComponent::editVersion, which exists precisely because ECS
// components DO need one) -- inventing one here to save a recompute this
// class's real caller (ModelingModePlugin) only ever triggers on a
// genuine edit, not every frame, would trade a real staleness-bug risk
// for a performance win nothing has asked for yet.
enum class ModifierType { Mirror, Array, Solidify, Subdivision, Boolean };

struct MirrorModifierParams {
    // 0 = X, 1 = Y, 2 = Z.
    int axis = 0;
    // Welds vertices that land within mergeThreshold of the mirror plane
    // after mirroring (EditableMesh::mergeVertices()) -- what closes the
    // seam down the middle instead of leaving a visible double wall.
    bool mergeAtCenter = true;
    float mergeThreshold = 0.001f;
};

struct ArrayModifierParams {
    // Total instances, including the original at offset*0. 1 is a real,
    // honest no-op -- the stack's own per-modifier `enabled` checkbox is
    // the normal way to skip a modifier entirely; this isn't a second one.
    int count = 2;
    glm::vec3 offset{1.0f, 0.0f, 0.0f};
};

struct SolidifyModifierParams {
    // Real, honest scope: offsets every face along its own real flat
    // normal (this mesh's per-face vertex storage means "vertex normal"
    // and "face normal" are already the same value -- see
    // core::EditableMesh's own class comment on why) to build an inner
    // shell with reversed winding, then walls the mesh's real open
    // boundary edges (an edge used by exactly 1 face, by real shared
    // VERTEX INDEX -- not by position). A mesh built with genuinely
    // shared/welded indices at its seams (e.g. via mergeVertices(), or
    // hand-built with a shared diagonal) correctly gets no wall at an
    // interior edge. EditableMesh::createBox() specifically does NOT
    // qualify: its own "24-vertex flat-shaded-per-face" storage gives
    // every face private corner vertices even where two faces meet, so
    // by this same real, index-based test every one of its outer edges
    // looks like a boundary -- the exact same limitation bevelEdge()'s
    // own header comment already documents for that mesh. Run
    // EditableMesh::mergeVertices() on a box first if real interior-edge
    // detection across its faces is needed before solidifying.
    float thickness = 0.1f;
};

struct SubdivisionModifierParams {
    // Real Catmull-Clark limit-surface smoothing (core::
    // catmullClarkSubdivide(), EditableMesh.hpp) applied `levels` times --
    // real face/edge points and the standard interior/boundary vertex
    // smoothing rules, not a flat/linear split. See
    // catmullClarkSubdivide()'s own header comment for its real, stated
    // scope limits (per-index, not per-position, adjacency -- the same
    // rule bevelEdge()/allEdges() already use).
    int levels = 1;
};

struct BooleanModifierParams {
    CsgOperation operation = CsgOperation::Union;
    // The real second operand is a real box, rebuilt fresh from these two
    // fields on every evaluate() (EditableMesh::createBox(boxHalfExtents,
    // boxOffset)) rather than stored as a baked EditableMesh -- same
    // "always a real box, not a placeholder" real second-operand
    // convention ModelingModePlugin's own destructive CSG panel already
    // uses, kept as plain draggable numbers here so a modifier-stack UI
    // panel can edit them directly (an ImGui::DragFloat3 on a live
    // EditableMesh has nothing sensible to drag).
    glm::vec3 boxOffset{1.0f, 0.0f, 0.0f};
    glm::vec3 boxHalfExtents{0.5f, 0.5f, 0.5f};
};

struct Modifier {
    ModifierType type = ModifierType::Mirror;
    bool enabled = true;
    MirrorModifierParams mirror;
    ArrayModifierParams array;
    SolidifyModifierParams solidify;
    SubdivisionModifierParams subdivision;
    BooleanModifierParams boolean;
};

class ModifierStack {
public:
    void addModifier(Modifier modifier) { modifiers_.push_back(std::move(modifier)); }
    // Real, honest no-op out of range -- same convention every
    // EditableMesh operator already uses for a bad index.
    void removeModifier(size_t index);
    // Swaps two real modifiers' order -- backs the stack's own real
    // "Move Up"/"Move Down" panel buttons.
    void swapModifiers(size_t a, size_t b);

    [[nodiscard]] std::vector<Modifier>& modifiers() { return modifiers_; }
    [[nodiscard]] const std::vector<Modifier>& modifiers() const { return modifiers_; }

    // Real non-destructive evaluation: replays every enabled modifier, in
    // order, over a COPY of `baseMesh` -- baseMesh itself is never
    // mutated. See this class's own header comment for why there is no
    // cache.
    [[nodiscard]] EditableMesh evaluate(const EditableMesh& baseMesh) const;

private:
    std::vector<Modifier> modifiers_;
};

} // namespace engine::core
