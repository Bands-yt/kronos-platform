#pragma once

#include <vector>

#include "core/PropAnimation.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Sprint 10 ("Creator Tools Phase 2") task category 3's "simple timeline
// editor for creators" -- real authoring for a selected entity's
// core::PropAnimationHook (PropAnimation.hpp): add/remove keyframes,
// scrub the track live (evaluates the real AnimationTrack and writes the
// pose straight into the selected entity's Transform for immediate
// visual feedback -- no live game-loop tick needed for that, Studio
// authoring is inherently paused/static, see StudioApp's class comment),
// and jump straight to the open/closed ends. "Add Keyframe at Current
// Pose" is the real, natural authoring flow: move the entity with the
// existing gizmo, then capture that pose as a keyframe -- not a
// numeric-only editor a creator has to type raw transform values into.
class TimelineEditorPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] const char* name() const override { return "Timeline Editor"; }
    [[nodiscard]] const char* category() const override { return "Effects"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawKeyframeList(core::ECS& ecs, core::EntityId selected, core::PropAnimationHook& hook);
    void drawScrubber(core::ECS& ecs, core::EntityId selected, core::PropAnimationHook& hook);

    float scrubTime_ = 0.0f;
};

} // namespace engine::studio::plugins
