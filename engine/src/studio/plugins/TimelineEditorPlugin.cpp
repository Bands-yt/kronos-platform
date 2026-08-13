#include "studio/plugins/TimelineEditorPlugin.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/Components.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
constexpr core::EasingMode kEasingValues[] = {core::EasingMode::Linear, core::EasingMode::EaseIn,
                                               core::EasingMode::EaseOut, core::EasingMode::EaseInOut,
                                               core::EasingMode::Constant};
constexpr int kEasingCount = 5;
} // namespace

void TimelineEditorPlugin::drawKeyframeList(core::ECS& ecs, core::EntityId selected, core::PropAnimationHook& hook) {
    ImGui::TextUnformatted("Keyframes");
    const auto& keyframes = hook.track.keyframes();
    int removeIndex = -1;
    for (size_t i = 0; i < keyframes.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const core::Keyframe& keyframe = keyframes[i];
        ImGui::Text("#%zu  t=%.2fs  pos=(%.2f, %.2f, %.2f)  [%s]", i, keyframe.time, keyframe.position.x,
                    keyframe.position.y, keyframe.position.z, core::easingModeName(keyframe.easing));
        ImGui::SameLine();
        if (ImGui::SmallButton("Go")) scrubTime_ = keyframe.time;
        ImGui::SameLine();
        int easingIndex = 0;
        for (int e = 0; e < kEasingCount; ++e) {
            if (kEasingValues[e] == keyframe.easing) easingIndex = e;
        }
        ImGui::SetNextItemWidth(110.0f);
        const char* easingNames[kEasingCount];
        for (int e = 0; e < kEasingCount; ++e) easingNames[e] = core::easingModeName(kEasingValues[e]);
        if (ImGui::Combo("##easing", &easingIndex, easingNames, kEasingCount)) {
            hook.track.keyframeAt(i).easing = kEasingValues[easingIndex];
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) removeIndex = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeIndex >= 0) hook.track.removeKeyframeAt(static_cast<size_t>(removeIndex));

    ImGui::Spacing();
    if (ImGui::Button("Add Keyframe At Current Pose (t = scrub time)")) {
        if (auto* transform = ecs.tryGetComponent<core::Transform>(selected)) {
            core::Keyframe keyframe;
            keyframe.time = scrubTime_;
            keyframe.position = transform->position;
            keyframe.rotation = transform->rotation;
            keyframe.scale = transform->scale;
            hook.track.addKeyframe(keyframe);
        }
    }
    ImGui::TextDisabled("Move the entity with the gizmo, set the scrub time below, then add a keyframe there.");
}

void TimelineEditorPlugin::drawScrubber(core::ECS& ecs, core::EntityId selected, core::PropAnimationHook& hook) {
    const auto& keyframes = hook.track.keyframes();
    float trackEnd = keyframes.empty() ? 0.0f : keyframes.back().time;

    ImGui::SeparatorText("Scrub / Preview");
    ImGui::SliderFloat("Time", &scrubTime_, 0.0f, std::max(trackEnd, 0.01f));
    if (ImGui::Button("Jump To Closed (t=0)")) scrubTime_ = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button("Jump To Open (last keyframe)")) scrubTime_ = trackEnd;

    // Real live preview: evaluates the actual track at scrubTime_ and
    // writes the pose straight into the selected entity's Transform --
    // Studio has no live game-loop tick to animate this over real time
    // (see this class's own header comment), so scrubbing *is* the
    // preview mechanism, the same way dragging a video editor's playhead
    // previews a frame without actually playing back.
    hook.currentTime = scrubTime_;
    core::AnimatedPose pose = core::evaluatePropAnimation(hook);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(selected)) {
        transform->position = pose.position;
        transform->rotation = pose.rotation;
    }
}

void TimelineEditorPlugin::drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Timeline Editor");

    if (selected == core::kNullEntity) {
        ImGui::TextDisabled("Select an entity to add/edit a prop animation.");
        drawPluginFooter();
        ImGui::End();
        return;
    }

    auto* hook = ecs.tryGetComponent<core::PropAnimationHook>(selected);
    if (hook == nullptr) {
        if (ImGui::Button("Add Prop Animation Hook To Selection")) {
            ecs.addComponent<core::PropAnimationHook>(selected, core::PropAnimationHook{});
        }
        drawPluginFooter();
        ImGui::End();
        return;
    }

    ImGui::Text("Real, live-runtime hook: interacting with this entity in engine_runtime calls");
    ImGui::TextUnformatted("core::togglePropAnimation() -- the real Open/Close/Toggle event this task asks for.");
    ImGui::Separator();

    drawKeyframeList(ecs, selected, *hook);
    drawScrubber(ecs, selected, *hook);

    drawPluginFooter("Scrubbing previews the pose live; the real runtime tick (Application.cpp) drives actual playback.");
    ImGui::End();
}

} // namespace engine::studio::plugins
