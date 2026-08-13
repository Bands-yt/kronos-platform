#include "studio/plugins/AnimationPreviewerPlugin.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "core/Components.hpp"

namespace engine::studio::plugins {

namespace {

// A real, small demo clip so the previewer always has something visibly
// moving to show by default, not just a static bind pose -- sways the
// torso/head side to side and raises one arm partway, targeting
// core::buildHumanoidSkeleton()'s real joint names. Every keyframe
// authors the joint's full bind-local position (only rotation actually
// varies) -- see AnimationTrack/Keyframe's "a keyframe is a full pose,
// not a sparse per-channel delta" convention (Animation.hpp).
core::AnimationClip makeDemoSwayClip(const core::Skeleton& skeleton) {
    core::AnimationClip clip;
    clip.name = "Demo Sway";
    clip.duration = 2.0f;
    clip.looping = true;
    clip.events.push_back({0.0f, "cycle_start"});
    clip.events.push_back({1.0f, "cycle_mid"});

    auto poseAt = [&](const char* jointName) { return skeleton.joints[static_cast<size_t>(skeleton.findJointIndex(jointName))].localPosition; };

    auto& torso = clip.trackFor("torso");
    core::Keyframe t0;
    t0.time = 0.0f;
    t0.position = poseAt("torso");
    torso.addKeyframe(t0);
    core::Keyframe t1;
    t1.time = 1.0f;
    t1.position = poseAt("torso");
    t1.rotation = glm::angleAxis(glm::radians(15.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    torso.addKeyframe(t1);
    core::Keyframe t2;
    t2.time = 2.0f;
    t2.position = poseAt("torso");
    torso.addKeyframe(t2);

    auto& rightArm = clip.trackFor("rightArm");
    core::Keyframe a0;
    a0.time = 0.0f;
    a0.position = poseAt("rightArm");
    rightArm.addKeyframe(a0);
    core::Keyframe a1;
    a1.time = 1.0f;
    a1.position = poseAt("rightArm");
    a1.rotation = glm::angleAxis(glm::radians(-70.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    rightArm.addKeyframe(a1);
    core::Keyframe a2;
    a2.time = 2.0f;
    a2.position = poseAt("rightArm");
    rightArm.addKeyframe(a2);

    return clip;
}

} // namespace

AnimationPreviewerPlugin::AnimationPreviewerPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                    VkQueue queue, core::RiggedMeshLibrary& riggedMeshLibrary)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), riggedMeshLibrary_(&riggedMeshLibrary),
      skeleton_(core::buildHumanoidSkeleton()), player_(skeleton_) {
    spawnDemoBody();
    previewClip(makeDemoSwayClip(skeleton_), "Demo Sway (built-in)");
}

void AnimationPreviewerPlugin::spawnDemoBody() {
    std::string error;
    if (!core::spawnRiggedAvatar(scene_.ecs(), skeleton_, emptyLoadout_, emptyIndex_, *riggedMeshLibrary_, allocator_,
                                  device_, cmdPool_, queue_, skinnedEntities_, error)) {
        statusMessage_ = "Failed to spawn demo body: " + error;
        std::fprintf(stderr, "AnimationPreviewerPlugin: %s\n", statusMessage_.c_str());
    }
}

void AnimationPreviewerPlugin::previewClip(core::AnimationClip clip, std::string displayName) {
    if (playingHandle_ != core::AnimationPlayer::kInvalidHandle) player_.stop(playingHandle_);
    currentClip_ = std::move(clip);
    currentClipName_ = std::move(displayName);
    // Show the pose at time 0 immediately (a real, if paused, first
    // frame) rather than leaving the previous clip's last pose on screen
    // until Play is pressed.
    playFromStart();
    player_.pause(playingHandle_);
    playing_ = false;
    setOpen(true); // an external caller (e.g. AvatarPreviewer's emote equip) handing off a clip should actually show it
}

void AnimationPreviewerPlugin::playFromStart() {
    playingHandle_ = player_.play(currentClip_, core::AnimationLayer::Base, looping_, /*fadeSeconds=*/0.0f);
    player_.seek(playingHandle_, 0.0f);
    playing_ = true;
}

void AnimationPreviewerPlugin::update(float dt, core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                       const std::vector<core::EntityId>& /*selectedEntities*/) {
    // dt=0 while paused still recomputes the pose from the current
    // playhead (e.g. right after a scrub) without advancing time -- see
    // this method's header comment.
    player_.tick(playing_ ? dt : 0.0f);
    (void)player_.consumeFiredEvents(); // no consumer wired up yet in this pass -- draining keeps the queue from growing unbounded

    const auto& matrices = player_.skinningMatrices();
    for (core::EntityId entity : skinnedEntities_) {
        if (auto* skinned = scene_.ecs().tryGetComponent<core::SkinnedRenderable>(entity)) {
            skinned->skinningMatrices = matrices;
        }
    }
}

void AnimationPreviewerPlugin::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    // No MeshLibrary/TextureLibrary content of its own (the demo body is
    // entirely SkinnedRenderable, no plain Renderable) -- still needs real
    // instances to satisfy render()'s signature, same as CataloguePanel's
    // item-detail popup does for an item preview scene with no
    // particle/instanced content either.
    static core::MeshLibrary sUnusedMeshLibrary;
    static core::TextureLibrary sUnusedTextureLibrary;
    scene_.render(cmd, renderer, sUnusedMeshLibrary, sUnusedTextureLibrary, riggedMeshLibrary_);
}

void AnimationPreviewerPlugin::drawPanel(core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                          const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Animation Previewer");

    ImGui::TextWrapped("Previewing \"%s\" on a real rigged demo body -- drag to orbit, scroll to zoom.",
                        currentClipName_.c_str());
    if (!statusMessage_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", statusMessage_.c_str());

    ImGui::Separator();
    if (ImGui::Button("Load Built-in Demo Sway")) {
        previewClip(makeDemoSwayClip(skeleton_), "Demo Sway (built-in)");
        statusMessage_.clear();
    }
    ImGui::InputText("File", clipPathBuffer_, sizeof(clipPathBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Load Clip File")) {
        core::AnimationClip imported;
        if (imported.loadFromFile(clipPathBuffer_)) {
            previewClip(std::move(imported), clipPathBuffer_);
            statusMessage_.clear();
        } else {
            statusMessage_ = std::string("Failed to load \"") + clipPathBuffer_ + "\".";
        }
    }

    ImGui::Separator();
    ImGui::Text("Clip: %s", currentClip_.name.c_str());
    ImGui::Text("Duration: %.2fs   Looping: %s   Tracks: %zu   Events: %zu", currentClip_.duration,
                currentClip_.looping ? "yes" : "no", currentClip_.tracks.size(), currentClip_.events.size());

    ImGui::Separator();
    if (playing_) {
        if (ImGui::Button("Pause")) {
            player_.pause(playingHandle_);
            playing_ = false;
        }
    } else {
        if (ImGui::Button("Play")) {
            player_.resume(playingHandle_);
            playing_ = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) playFromStart();
    ImGui::SameLine();
    if (ImGui::Checkbox("Loop", &looping_)) playFromStart(); // looping is a play()-time flag -- toggling re-triggers from the current settings, see AnimationPlayer::play()'s header comment

    float playhead = player_.playhead(playingHandle_);
    float duration = std::max(currentClip_.duration, 0.01f);
    if (ImGui::SliderFloat("Playhead", &playhead, 0.0f, duration, "%.2fs")) {
        player_.seek(playingHandle_, playhead);
        player_.pause(playingHandle_); // scrubbing implies "let me look at this exact frame" -- doesn't resume playback
        playing_ = false;
    }

    // Keyframe markers -- small ticks under the scrubber at every real
    // keyframe time across every track, the timeline "here's where a pose
    // was actually authored" view a plain slider alone doesn't give.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 sliderMin = ImGui::GetItemRectMin();
    ImVec2 sliderMax = ImGui::GetItemRectMax();
    float trackY = sliderMax.y + 2.0f;
    for (float t : core::collectKeyframeTimes(currentClip_)) {
        float fraction = duration > 0.0f ? std::clamp(t / duration, 0.0f, 1.0f) : 0.0f;
        float x = sliderMin.x + fraction * (sliderMax.x - sliderMin.x);
        drawList->AddLine(ImVec2(x, trackY), ImVec2(x, trackY + 6.0f), IM_COL32(255, 200, 80, 255), 2.0f);
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    ImGui::Separator();
    scene_.drawAndHandleOrbit();

    ImGui::End();
}

void AnimationPreviewerPlugin::shutdown(core::Renderer& renderer) { scene_.destroy(renderer, allocator_, device_); }

} // namespace engine::studio::plugins
