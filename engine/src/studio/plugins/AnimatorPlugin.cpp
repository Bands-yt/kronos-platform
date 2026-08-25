#include "studio/plugins/AnimatorPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "core/Components.hpp"

namespace engine::studio::plugins {

namespace {
core::EntityId findEntityByName(core::ECS& ecs, const std::string& targetName) {
    for (auto entity : ecs.view<core::Name>()) {
        const auto* nameComp = ecs.tryGetComponent<core::Name>(entity);
        if (nameComp != nullptr && nameComp->value == targetName) return entity;
    }
    return core::kNullEntity;
}

const core::AnimationTrack* findTrack(const core::AnimationClip& clip, const std::string& targetName) {
    for (const auto& track : clip.tracks) {
        if (track.targetName() == targetName) return &track;
    }
    return nullptr;
}

const core::PropertyTrack* findPropertyTrack(const core::AnimationClip& clip, const std::string& targetName,
                                              const std::string& propertyName) {
    for (const auto& track : clip.propertyTracks) {
        if (track.targetName() == targetName && track.propertyName() == propertyName) return &track;
    }
    return nullptr;
}

// Kronos ("Timeline & Dope Sheet" -- property curves): the real registry
// PropertyTrack's own class comment refers to as "a future AnimatorPlugin
// UI pass" -- resolves a propertyName to a real read/write location on
// core::Renderable. Deliberately a small, hand-picked list of this
// engine's own real material fields (Components.hpp), not every field on
// Renderable (meshHandle/materialHandle/visible/etc. aren't meaningfully
// "animatable" the way a color or scalar is) -- and Renderable only for
// now, matching the explicit "entities first" scope this pass was given;
// a Camera registry is a real, separate future addition, same shape as
// this one. Plain function pointers (no captures needed), so each entry
// is a real, direct read/write, not an indirection through std::function.
struct PropertyDescriptor {
    const char* name;
    core::PropertyValue (*get)(const core::Renderable&);
    void (*set)(core::Renderable&, const core::PropertyValue&);
};

constexpr PropertyDescriptor kRenderableProperties[] = {
    {"baseColor", [](const core::Renderable& r) -> core::PropertyValue { return r.baseColor; },
     [](core::Renderable& r, const core::PropertyValue& v) {
         if (const glm::vec4* p = std::get_if<glm::vec4>(&v)) r.baseColor = *p;
     }},
    {"emissiveColor", [](const core::Renderable& r) -> core::PropertyValue { return r.emissiveColor; },
     [](core::Renderable& r, const core::PropertyValue& v) {
         if (const glm::vec3* p = std::get_if<glm::vec3>(&v)) r.emissiveColor = *p;
     }},
    {"emissiveIntensity", [](const core::Renderable& r) -> core::PropertyValue { return r.emissiveIntensity; },
     [](core::Renderable& r, const core::PropertyValue& v) {
         if (const float* p = std::get_if<float>(&v)) r.emissiveIntensity = *p;
     }},
    {"metallic", [](const core::Renderable& r) -> core::PropertyValue { return r.metallic; },
     [](core::Renderable& r, const core::PropertyValue& v) {
         if (const float* p = std::get_if<float>(&v)) r.metallic = *p;
     }},
    {"roughness", [](const core::Renderable& r) -> core::PropertyValue { return r.roughness; },
     [](core::Renderable& r, const core::PropertyValue& v) {
         if (const float* p = std::get_if<float>(&v)) r.roughness = *p;
     }},
    {"transmission", [](const core::Renderable& r) -> core::PropertyValue { return r.transmission; },
     [](core::Renderable& r, const core::PropertyValue& v) {
         if (const float* p = std::get_if<float>(&v)) r.transmission = *p;
     }},
};
constexpr int kRenderablePropertyCount = static_cast<int>(sizeof(kRenderableProperties) / sizeof(PropertyDescriptor));

// Real, generic lerp across a PropertyValue's active alternative -- same
// "type-dispatch on the variant, hold at `a` if the two sides somehow
// disagree" shape as PropertyTrack::evaluate() itself (Animation.cpp),
// needed again here for crossfade blending between two clips' property
// tracks, the same real blend applyPoseToScene() already does for
// Transform tracks via glm::mix/glm::slerp.
core::PropertyValue mixPropertyValue(const core::PropertyValue& a, const core::PropertyValue& b, float t) {
    if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) {
        return glm::mix(std::get<float>(a), std::get<float>(b), t);
    }
    if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
        return glm::mix(std::get<glm::vec3>(a), std::get<glm::vec3>(b), t);
    }
    if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
        return glm::mix(std::get<glm::vec4>(a), std::get<glm::vec4>(b), t);
    }
    return a;
}
} // namespace

std::string AnimatorPlugin::nameOf(core::ECS& ecs, core::EntityId entity) {
    if (const auto* nameComp = ecs.tryGetComponent<core::Name>(entity)) {
        if (!nameComp->value.empty()) return nameComp->value;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "Entity %u", static_cast<uint32_t>(entity));
    return buf;
}

void AnimatorPlugin::update(float dt, core::ECS& ecs, core::EntityId /*selected*/,
                             const std::vector<core::EntityId>& /*selectedEntities*/) {
    if (activeClipIndex_ < 0 || activeClipIndex_ >= static_cast<int>(library_.size())) return;
    core::AnimationClip& active = library_[static_cast<size_t>(activeClipIndex_)];

    if (playing_) {
        playhead_ += dt;
        if (playhead_ > active.duration) {
            if (active.looping && active.duration > 0.0f) {
                playhead_ = std::fmod(playhead_, active.duration);
            } else {
                playhead_ = active.duration;
                playing_ = false;
            }
        }
    }

    if (crossfading_) {
        // The outgoing clip keeps advancing too, not frozen at the moment
        // the crossfade started -- a real crossfade blends two *playing*
        // clips, matching how Roblox's own AnimationTrack:Play() fade
        // parameter behaves, not a blend from a still frame into motion.
        crossfadeElapsed_ += dt;
        previousPlayhead_ += dt;
        if (crossfadeElapsed_ >= crossfadeDuration_) {
            crossfading_ = false;
        }
    }

    // Applied unconditionally, not just while playing_ -- dragging the
    // playhead slider while paused should show the real interpolated pose
    // immediately, same as scrubbing in any keyframe editor.
    applyPoseToScene(ecs);
}

void AnimatorPlugin::applyPoseToScene(core::ECS& ecs) const {
    if (activeClipIndex_ < 0 || activeClipIndex_ >= static_cast<int>(library_.size())) return;
    const core::AnimationClip& active = library_[static_cast<size_t>(activeClipIndex_)];

    const core::AnimationClip* previous = nullptr;
    float blend = 1.0f;
    if (crossfading_ && previousClipIndex_ >= 0 && previousClipIndex_ < static_cast<int>(library_.size())) {
        previous = &library_[static_cast<size_t>(previousClipIndex_)];
        blend = crossfadeDuration_ > 0.0f ? std::clamp(crossfadeElapsed_ / crossfadeDuration_, 0.0f, 1.0f) : 1.0f;
    }

    for (const auto& track : active.tracks) {
        if (track.keyframes().empty()) continue;
        core::EntityId target = findEntityByName(ecs, track.targetName());
        if (target == core::kNullEntity) continue;
        auto* transform = ecs.tryGetComponent<core::Transform>(target);
        if (transform == nullptr) continue;

        core::AnimatedPose pose = track.evaluate(playhead_);

        // Blend against the outgoing clip's pose for this same target, if
        // it has a track for it -- a track that exists only in the
        // incoming clip has nothing to blend from, so it snaps straight
        // to its own pose (see AnimatorPlugin.hpp's class comment: an
        // accepted limitation, not a bug).
        if (previous != nullptr) {
            const core::AnimationTrack* previousTrack = findTrack(*previous, track.targetName());
            if (previousTrack != nullptr && !previousTrack->keyframes().empty()) {
                core::AnimatedPose previousPose = previousTrack->evaluate(previousPlayhead_);
                pose.position = glm::mix(previousPose.position, pose.position, blend);
                pose.rotation = glm::slerp(previousPose.rotation, pose.rotation, blend);
                pose.scale = glm::mix(previousPose.scale, pose.scale, blend);
            }
        }

        transform->position = pose.position;
        transform->rotation = pose.rotation;
        transform->scale = pose.scale;
    }

    // Same real per-frame evaluate-and-write as the Transform loop above,
    // just against a Renderable field via kRenderableProperties instead
    // of Transform directly -- see this method's own header comment.
    for (const auto& track : active.propertyTracks) {
        if (track.keyframes().empty()) continue;
        core::EntityId target = findEntityByName(ecs, track.targetName());
        if (target == core::kNullEntity) continue;
        auto* renderable = ecs.tryGetComponent<core::Renderable>(target);
        if (renderable == nullptr) continue;

        const PropertyDescriptor* descriptor = nullptr;
        for (const auto& candidate : kRenderableProperties) {
            if (track.propertyName() == candidate.name) {
                descriptor = &candidate;
                break;
            }
        }
        if (descriptor == nullptr) continue; // unknown property name -- real, honest skip, not a crash

        core::PropertyValue value = track.evaluate(playhead_);
        if (previous != nullptr) {
            const core::PropertyTrack* previousTrack = findPropertyTrack(*previous, track.targetName(), track.propertyName());
            if (previousTrack != nullptr && !previousTrack->keyframes().empty()) {
                core::PropertyValue previousValue = previousTrack->evaluate(previousPlayhead_);
                value = mixPropertyValue(previousValue, value, blend);
            }
        }
        descriptor->set(*renderable, value);
    }
}

void AnimatorPlugin::startTransitionTo(int newClipIndex) {
    if (newClipIndex == activeClipIndex_) return;
    if (activeClipIndex_ >= 0 && activeClipIndex_ < static_cast<int>(library_.size())) {
        previousClipIndex_ = activeClipIndex_;
        previousPlayhead_ = playhead_;
        crossfading_ = true;
        crossfadeElapsed_ = 0.0f;
    }
    activeClipIndex_ = newClipIndex;
    playhead_ = 0.0f;
    playing_ = true; // switching clips implies "play the one I just picked"
    selectedTrackIndex_ = -1;
    selectedKeyIndex_ = -1;
    statusMessage_.clear();
}

void AnimatorPlugin::drawClipLibraryUi() {
    ImGui::SeparatorText("Clip Library");

    if (ImGui::Button("New Clip")) {
        core::AnimationClip fresh;
        fresh.name = "Clip " + std::to_string(library_.size() + 1);
        library_.push_back(fresh);
        startTransitionTo(static_cast<int>(library_.size()) - 1);
    }

    bool hasActive = activeClipIndex_ >= 0 && activeClipIndex_ < static_cast<int>(library_.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasActive);
    if (ImGui::Button("Duplicate")) {
        core::AnimationClip copy = library_[static_cast<size_t>(activeClipIndex_)];
        copy.name += " Copy";
        library_.push_back(copy);
        // Duplicating and then jumping to the copy is a plain switch, not
        // a blend -- the two clips hold an identical pose at time 0, so
        // crossfading between them would be pure wasted motion.
        activeClipIndex_ = static_cast<int>(library_.size()) - 1;
        playhead_ = 0.0f;
        playing_ = false;
        selectedTrackIndex_ = -1;
        selectedKeyIndex_ = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        library_.erase(library_.begin() + activeClipIndex_);
        if (previousClipIndex_ == activeClipIndex_) crossfading_ = false;
        activeClipIndex_ = -1;
        previousClipIndex_ = -1;
        crossfading_ = false;
        selectedTrackIndex_ = -1;
        selectedKeyIndex_ = -1;
    }
    ImGui::EndDisabled();

    ImGui::BeginChild("clip_library_list", ImVec2(0.0f, 90.0f), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < library_.size(); ++i) {
        bool isActive = static_cast<int>(i) == activeClipIndex_;
        char label[192];
        std::snprintf(label, sizeof(label), "%s##clip%zu", library_[i].name.c_str(), i);
        if (ImGui::Selectable(label, isActive)) {
            startTransitionTo(static_cast<int>(i));
        }
    }
    if (library_.empty()) {
        ImGui::TextDisabled("No clips yet -- click New Clip.");
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Crossfade (s)", &crossfadeDuration_, 0.02f, 0.0f, 5.0f, "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled(crossfading_ ? "(blending...)" : "(idle)");
}

void AnimatorPlugin::drawPanel(core::ECS& ecs, core::EntityId selected,
                                const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Animator");

    drawClipLibraryUi();

    if (activeClipIndex_ < 0 || activeClipIndex_ >= static_cast<int>(library_.size())) {
        ImGui::TextDisabled("No active clip -- click New Clip above to start.");
        ImGui::End();
        return;
    }
    core::AnimationClip& clip = library_[static_cast<size_t>(activeClipIndex_)];

    ImGui::Separator();
    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", clip.name.c_str());
    if (ImGui::InputText("Clip Name", nameBuf, sizeof(nameBuf))) {
        clip.name = nameBuf;
    }
    ImGui::DragFloat("Duration (s)", &clip.duration, 0.1f, 0.1f, 600.0f, "%.2f");
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &clip.looping);

    drawTransport(clip);
    drawFileControls();

    ImGui::Separator();
    ImGui::BeginDisabled(selected == core::kNullEntity);
    if (ImGui::Button("Add Track For Selection")) {
        clip.trackFor(nameOf(ecs, selected));
    }
    ImGui::SameLine();
    if (ImGui::Button("Set Key At Playhead")) {
        if (auto* transform = ecs.tryGetComponent<core::Transform>(selected)) {
            core::Keyframe kf;
            kf.time = snapEnabled_ && snapIncrementSeconds_ > 0.0f
                          ? std::round(playhead_ / snapIncrementSeconds_) * snapIncrementSeconds_
                          : playhead_;
            kf.position = transform->position;
            kf.rotation = transform->rotation;
            kf.scale = transform->scale;
            kf.easing = core::EasingMode::Linear;
            clip.trackFor(nameOf(ecs, selected)).addKeyframe(kf);
        }
    }
    ImGui::EndDisabled();
    if (selected == core::kNullEntity) {
        ImGui::TextDisabled("Select an entity in Explorer to add a track or set a key.");
    }

    drawTrackList(ecs, clip);
    drawTimeline(clip);
    drawCurveEditor(clip);
    drawPropertyTrackList(ecs, selected, clip);

    ImGui::End();
}

void AnimatorPlugin::drawTransport(core::AnimationClip& clip) {
    if (ImGui::Button(playing_ ? "Pause" : "Play")) {
        playing_ = !playing_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        playing_ = false;
        playhead_ = 0.0f;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    float duration = std::max(clip.duration, 0.01f);
    if (ImGui::SliderFloat("Playhead", &playhead_, 0.0f, duration, "%.2f s") && snapEnabled_ &&
        snapIncrementSeconds_ > 0.0f) {
        playhead_ = std::round(playhead_ / snapIncrementSeconds_) * snapIncrementSeconds_;
    }
}

void AnimatorPlugin::drawFileControls() {
    ImGui::InputText("File", clipPathBuffer_, sizeof(clipPathBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
        bool ok = library_[static_cast<size_t>(activeClipIndex_)].saveToFile(clipPathBuffer_);
        statusMessage_ = ok ? "Exported." : "Export failed (bad path?).";
    }
    ImGui::SameLine();
    if (ImGui::Button("Import As New Clip")) {
        core::AnimationClip imported;
        if (imported.loadFromFile(clipPathBuffer_)) {
            library_.push_back(imported);
            startTransitionTo(static_cast<int>(library_.size()) - 1);
            statusMessage_ = "Imported.";
        } else {
            statusMessage_ = "Import failed (missing/malformed file?).";
        }
    }
    if (!statusMessage_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }
}

void AnimatorPlugin::drawTrackList(core::ECS& /*ecs*/, core::AnimationClip& clip) {
    ImGui::Separator();
    ImGui::TextUnformatted("Tracks");

    for (size_t ti = 0; ti < clip.tracks.size(); ++ti) {
        core::AnimationTrack& track = clip.tracks[ti];
        ImGui::PushID(static_cast<int>(ti));

        bool open = ImGui::TreeNodeEx("track", ImGuiTreeNodeFlags_DefaultOpen, "%s (%zu keys)",
                                       track.targetName().c_str(), track.keyframes().size());
        ImGui::SameLine();
        bool removed = ImGui::SmallButton("Remove Track");

        if (open) {
            for (size_t ki = 0; ki < track.keyframes().size(); ++ki) {
                const core::Keyframe& kf = track.keyframes()[ki];
                ImGui::PushID(static_cast<int>(ki));

                bool isSelectedKey = (selectedTrackIndex_ == static_cast<int>(ti) && selectedKeyIndex_ == static_cast<int>(ki));
                char label[32];
                std::snprintf(label, sizeof(label), "t=%.2f", kf.time);
                if (ImGui::Selectable(label, isSelectedKey, 0, ImVec2(80.0f, 0.0f))) {
                    selectedTrackIndex_ = static_cast<int>(ti);
                    selectedKeyIndex_ = static_cast<int>(ki);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", core::easingModeName(kf.easing));
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) {
                    track.removeKeyframeAt(ki);
                    if (selectedTrackIndex_ == static_cast<int>(ti) && selectedKeyIndex_ == static_cast<int>(ki)) {
                        selectedKeyIndex_ = -1;
                    }
                    ImGui::PopID();
                    break; // keyframes_ mutated -- stop this track's loop, redraws clean next frame
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();

        if (removed) {
            clip.removeTrack(track.targetName());
            selectedTrackIndex_ = -1;
            selectedKeyIndex_ = -1;
            break; // tracks vector mutated -- stop iterating this frame
        }
    }

    if (selectedTrackIndex_ >= 0 && selectedTrackIndex_ < static_cast<int>(clip.tracks.size())) {
        core::AnimationTrack& track = clip.tracks[static_cast<size_t>(selectedTrackIndex_)];
        if (selectedKeyIndex_ >= 0 && selectedKeyIndex_ < static_cast<int>(track.keyframes().size())) {
            core::Keyframe& kf = track.keyframeAt(static_cast<size_t>(selectedKeyIndex_));
            ImGui::Separator();
            ImGui::Text("Selected Keyframe -- %s @ t=%.2fs", track.targetName().c_str(), kf.time);
            static const char* kEasingNames[] = {"Linear", "Ease In", "Ease Out", "Ease In Out", "Constant"};
            int easingIndex = static_cast<int>(kf.easing);
            if (ImGui::Combo("Easing", &easingIndex, kEasingNames, IM_ARRAYSIZE(kEasingNames))) {
                kf.easing = static_cast<core::EasingMode>(easingIndex);
            }
        }
    }
}

void AnimatorPlugin::drawPropertyTrackList(core::ECS& ecs, core::EntityId selected, core::AnimationClip& clip) {
    ImGui::Separator();
    ImGui::TextUnformatted("Property Tracks");
    ImGui::TextDisabled("Keys a single Renderable property (color/metallic/roughness/emissive/transmission) instead of the whole Transform.");

    const char* propertyNames[kRenderablePropertyCount];
    for (int i = 0; i < kRenderablePropertyCount; ++i) propertyNames[i] = kRenderableProperties[i].name;
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Property", &selectedPropertyDescriptorIndex_, propertyNames, kRenderablePropertyCount);

    const PropertyDescriptor& chosen = kRenderableProperties[selectedPropertyDescriptorIndex_];
    auto* renderable = ecs.tryGetComponent<core::Renderable>(selected);

    ImGui::BeginDisabled(selected == core::kNullEntity || renderable == nullptr);
    ImGui::SameLine();
    if (ImGui::Button("Add Property Track For Selection")) {
        clip.propertyTrackFor(nameOf(ecs, selected), chosen.name);
    }
    ImGui::SameLine();
    if (ImGui::Button("Set Key At Playhead##property")) {
        core::PropertyKeyframe pk;
        pk.time = snapEnabled_ && snapIncrementSeconds_ > 0.0f
                      ? std::round(playhead_ / snapIncrementSeconds_) * snapIncrementSeconds_
                      : playhead_;
        pk.value = chosen.get(*renderable);
        pk.easing = core::EasingMode::Linear;
        if (!clip.propertyTrackFor(nameOf(ecs, selected), chosen.name).addKeyframe(pk)) {
            // Real, honest failure surface -- see PropertyTrack::addKeyframe()'s
            // own comment on why a type mismatch is refused rather than
            // silently accepted. Shouldn't happen through this UI path
            // (the same descriptor's get() always returns the same
            // alternative), but a track loaded from a hand-edited/older
            // .anim file could genuinely hold a different one.
            statusMessage_ = "Set Key failed: this track already holds a different value type.";
        }
    }
    ImGui::EndDisabled();
    if (selected == core::kNullEntity) {
        ImGui::TextDisabled("Select an entity in Explorer to add a property track or set a key.");
    } else if (renderable == nullptr) {
        ImGui::TextDisabled("Selected entity has no Renderable component -- nothing to key here.");
    }

    for (size_t ti = 0; ti < clip.propertyTracks.size(); ++ti) {
        core::PropertyTrack& track = clip.propertyTracks[ti];
        ImGui::PushID(static_cast<int>(ti) + 100000); // offset clear of drawTrackList()'s own PushID(ti) range

        bool open = ImGui::TreeNodeEx("proptrack", ImGuiTreeNodeFlags_DefaultOpen, "%s.%s (%zu keys)",
                                       track.targetName().c_str(), track.propertyName().c_str(), track.keyframes().size());
        ImGui::SameLine();
        bool removed = ImGui::SmallButton("Remove Track");

        if (open) {
            for (size_t ki = 0; ki < track.keyframes().size(); ++ki) {
                const core::PropertyKeyframe& pk = track.keyframes()[ki];
                ImGui::PushID(static_cast<int>(ki));

                bool isSelectedKey =
                    (selectedPropertyTrackIndex_ == static_cast<int>(ti) && selectedPropertyKeyIndex_ == static_cast<int>(ki));
                char label[32];
                std::snprintf(label, sizeof(label), "t=%.2f", pk.time);
                if (ImGui::Selectable(label, isSelectedKey, 0, ImVec2(80.0f, 0.0f))) {
                    selectedPropertyTrackIndex_ = static_cast<int>(ti);
                    selectedPropertyKeyIndex_ = static_cast<int>(ki);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", core::easingModeName(pk.easing));
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) {
                    track.removeKeyframeAt(ki);
                    if (selectedPropertyTrackIndex_ == static_cast<int>(ti) && selectedPropertyKeyIndex_ == static_cast<int>(ki)) {
                        selectedPropertyKeyIndex_ = -1;
                    }
                    ImGui::PopID();
                    break; // keyframes_ mutated -- stop this track's loop, redraws clean next frame
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();

        if (removed) {
            clip.removePropertyTrack(track.targetName(), track.propertyName());
            selectedPropertyTrackIndex_ = -1;
            selectedPropertyKeyIndex_ = -1;
            break; // propertyTracks vector mutated -- stop iterating this frame
        }
    }

    if (selectedPropertyTrackIndex_ >= 0 && selectedPropertyTrackIndex_ < static_cast<int>(clip.propertyTracks.size())) {
        core::PropertyTrack& track = clip.propertyTracks[static_cast<size_t>(selectedPropertyTrackIndex_)];
        if (selectedPropertyKeyIndex_ >= 0 && selectedPropertyKeyIndex_ < static_cast<int>(track.keyframes().size())) {
            core::PropertyKeyframe& pk = track.keyframeAt(static_cast<size_t>(selectedPropertyKeyIndex_));
            ImGui::Separator();
            ImGui::Text("Selected Property Keyframe -- %s.%s @ t=%.2fs", track.targetName().c_str(),
                        track.propertyName().c_str(), pk.time);
            static const char* kEasingNames[] = {"Linear", "Ease In", "Ease Out", "Ease In Out", "Constant"};
            int easingIndex = static_cast<int>(pk.easing);
            if (ImGui::Combo("Easing##property", &easingIndex, kEasingNames, IM_ARRAYSIZE(kEasingNames))) {
                pk.easing = static_cast<core::EasingMode>(easingIndex);
            }
        }
    }
}

void AnimatorPlugin::drawTimeline(const core::AnimationClip& clip) {
    ImGui::Separator();
    ImGui::TextUnformatted("Timeline");
    ImGui::TextDisabled("Drag to scrub. Select/edit keyframes via the track list above.");

    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Zoom", &timelineZoom_, 1.0f, 10.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    float maxScroll = std::max(clip.duration - clip.duration / timelineZoom_, 0.0f);
    ImGui::SliderFloat("Scroll", &timelineScrollSeconds_, 0.0f, std::max(maxScroll, 0.01f), "%.2f s");
    timelineScrollSeconds_ = std::clamp(timelineScrollSeconds_, 0.0f, std::max(maxScroll, 0.0f));
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &snapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("Grid (s)", &snapIncrementSeconds_, 0.01f, 0.01f, 10.0f, "%.2f");

    const float rowHeight = 20.0f;
    const float headerHeight = 6.0f;
    float height = headerHeight + rowHeight * static_cast<float>(std::max<size_t>(clip.tracks.size(), 1)) + 6.0f;
    float width = ImGui::GetContentRegionAvail().x;
    if (width < 40.0f) width = 40.0f;

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize(width, height);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(28, 28, 32, 255));
    drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(70, 70, 78, 255));

    // Zoom narrows the visible window to duration/zoom seconds; scroll
    // slides that window's start. At zoom=1 this degenerates to the old
    // "whole clip fills the canvas" mapping exactly.
    float fullDuration = std::max(clip.duration, 0.01f);
    float visibleDuration = std::max(fullDuration / timelineZoom_, 0.01f);
    float scrollStart = std::clamp(timelineScrollSeconds_, 0.0f, std::max(fullDuration - visibleDuration, 0.0f));
    auto timeToX = [&](float t) {
        return canvasPos.x + std::clamp((t - scrollStart) / visibleDuration, 0.0f, 1.0f) * canvasSize.x;
    };

    // Snap-grid tick marks, drawn only when snapping is on -- otherwise
    // they'd just be visual noise for a value nothing enforces.
    if (snapEnabled_ && snapIncrementSeconds_ > 0.0f) {
        float firstTick = std::floor(scrollStart / snapIncrementSeconds_) * snapIncrementSeconds_;
        for (float t = firstTick; t <= scrollStart + visibleDuration; t += snapIncrementSeconds_) {
            if (t < 0.0f) continue;
            float x = timeToX(t);
            drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y), IM_COL32(50, 50, 58, 255));
        }
    }

    for (size_t ti = 0; ti < clip.tracks.size(); ++ti) {
        float rowY = canvasPos.y + headerHeight + static_cast<float>(ti) * rowHeight + rowHeight * 0.5f;
        const core::AnimationTrack& track = clip.tracks[ti];
        for (size_t ki = 0; ki < track.keyframes().size(); ++ki) {
            const core::Keyframe& kf = track.keyframes()[ki];
            float x = timeToX(kf.time);
            bool isSelected = (selectedTrackIndex_ == static_cast<int>(ti) && selectedKeyIndex_ == static_cast<int>(ki));
            ImU32 color = isSelected ? IM_COL32(255, 205, 90, 255) : IM_COL32(90, 170, 255, 255);
            const float r = 5.0f;
            ImVec2 p(x, rowY);
            drawList->AddQuadFilled(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y), color);
        }
    }

    float playheadX = timeToX(playhead_);
    drawList->AddLine(ImVec2(playheadX, canvasPos.y), ImVec2(playheadX, canvasPos.y + canvasSize.y), IM_COL32(255, 90, 90, 255), 2.0f);

    ImGui::InvisibleButton("animator_timeline_canvas", canvasSize);
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float mouseX = ImGui::GetIO().MousePos.x;
        float t = scrollStart + (mouseX - canvasPos.x) / canvasSize.x * visibleDuration;
        if (snapEnabled_ && snapIncrementSeconds_ > 0.0f) {
            t = std::round(t / snapIncrementSeconds_) * snapIncrementSeconds_;
        }
        playhead_ = std::clamp(t, 0.0f, fullDuration);
        playing_ = false; // scrubbing implicitly pauses, matching most timeline UIs
    }
}

void AnimatorPlugin::drawCurveEditor(core::AnimationClip& clip) {
    ImGui::Separator();
    ImGui::TextUnformatted("Easing Curve");

    if (selectedTrackIndex_ < 0 || selectedTrackIndex_ >= static_cast<int>(clip.tracks.size())) {
        ImGui::TextDisabled("Select a keyframe above to preview its easing curve.");
        return;
    }
    core::AnimationTrack& track = clip.tracks[static_cast<size_t>(selectedTrackIndex_)];
    if (selectedKeyIndex_ < 0 || selectedKeyIndex_ >= static_cast<int>(track.keyframes().size())) {
        ImGui::TextDisabled("Select a keyframe above to preview its easing curve.");
        return;
    }
    const core::Keyframe& kf = track.keyframeAt(static_cast<size_t>(selectedKeyIndex_));

    // Plots the exact function AnimationTrack::evaluate() applies to this
    // segment's normalized time -- what you see here is what the pose
    // interpolation actually does, not an illustrative approximation.
    constexpr int kSampleCount = 64;
    static float samples[kSampleCount];
    for (int i = 0; i < kSampleCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSampleCount - 1);
        samples[i] = kf.easing == core::EasingMode::Constant ? 0.0f : core::applyEasing(kf.easing, t);
    }
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%s (t=%.2fs)", core::easingModeName(kf.easing), kf.time);
    ImGui::PlotLines("##easing_curve", samples, kSampleCount, 0, overlay, 0.0f, 1.0f, ImVec2(0.0f, 90.0f));
}

} // namespace engine::studio::plugins
