#pragma once

#include "core/ECS.hpp"

namespace engine::studio::panels {

// Filters the scene's entity list by name substring and/or component
// presence -- a faster way to find one entity in a large scene than
// scrolling Explorer's flat list (see ExplorerPanel.hpp's note on why
// that's a flat list, not a tree, in the first place). Deliberately
// decoupled from ExplorerPanel's own selection state: draw() returns the
// entity clicked this frame (or kNullEntity), and the caller (StudioApp)
// applies it via ExplorerPanel::setSelected() -- same "explicit return,
// caller wires it up" shape as every other cross-panel interaction in
// this codebase, rather than this panel reaching into ExplorerPanel directly.
class SceneSearchPanel {
public:
    [[nodiscard]] core::EntityId draw(core::ECS& ecs, core::EntityId currentSelection);

private:
    char filterText_[128] = "";
    bool requireRenderable_ = false;
    bool requireRigidBody_ = false;
    bool requireParticleEmitter_ = false;
};

} // namespace engine::studio::panels
