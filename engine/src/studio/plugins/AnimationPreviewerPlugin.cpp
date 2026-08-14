#include "studio/plugins/AnimationPreviewerPlugin.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "core/Components.hpp"
#include "core/ResourcePaths.hpp"

namespace engine::studio::plugins {

namespace {

// The 6 real shipped clips (engine/assets/animations/*.anim) -- exactly
// the set core::AvatarController's state machine drives (see
// AvatarController.cpp's tickAnimation()), in the same order the user's
// spec lists them.
struct ShippedClip {
    const char* fileBaseName;
    const char* displayName;
};
constexpr ShippedClip kShippedClips[] = {
    {"idle", "Idle"}, {"walk", "Walk"}, {"run", "Run"},
    {"jump_start", "Jump Start"}, {"jump_air", "Jump Air"}, {"jump_land", "Jump Land"},
};

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

    auto& torso = clip.trackFor("spine_upper");
    core::Keyframe t0;
    t0.time = 0.0f;
    t0.position = poseAt("spine_upper");
    torso.addKeyframe(t0);
    core::Keyframe t1;
    t1.time = 1.0f;
    t1.position = poseAt("spine_upper");
    t1.rotation = glm::angleAxis(glm::radians(15.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    torso.addKeyframe(t1);
    core::Keyframe t2;
    t2.time = 2.0f;
    t2.position = poseAt("spine_upper");
    torso.addKeyframe(t2);

    auto& rightArm = clip.trackFor("arm_R_upper");
    core::Keyframe a0;
    a0.time = 0.0f;
    a0.position = poseAt("arm_R_upper");
    rightArm.addKeyframe(a0);
    core::Keyframe a1;
    a1.time = 1.0f;
    a1.position = poseAt("arm_R_upper");
    a1.rotation = glm::angleAxis(glm::radians(-70.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    rightArm.addKeyframe(a1);
    core::Keyframe a2;
    a2.time = 2.0f;
    a2.position = poseAt("arm_R_upper");
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

void AnimationPreviewerPlugin::loadShippedClip(const char* fileBaseName, const char* displayName) {
    std::string animDir = core::resolveResourceDir(core::executableDirectory(), "assets", ENGINE_ASSET_DIR) + "/animations";
    std::string path = animDir + "/" + fileBaseName + ".anim";
    core::AnimationClip clip;
    if (clip.loadFromFile(path)) {
        previewClip(std::move(clip), displayName);
        statusMessage_.clear();
    } else {
        statusMessage_ = std::string("Failed to load shipped clip \"") + path + "\".";
    }
}

void AnimationPreviewerPlugin::playFromStart() {
    playingHandle_ = player_.play(currentClip_, core::AnimationLayer::Base, looping_, /*fadeSeconds=*/0.0f);
    player_.seek(playingHandle_, 0.0f);
    playing_ = true;
}

void AnimationPreviewerPlugin::drawSkeletonOverlay() {
    ImVec2 imageMin = ImGui::GetItemRectMin();
    ImVec2 imageMax = ImGui::GetItemRectMax();
    float width = imageMax.x - imageMin.x;
    float height = imageMax.y - imageMin.y;
    if (width < 1.0f || height < 1.0f) return;

    const core::Camera& camera = scene_.camera();
    glm::mat4 viewProj = camera.projectionMatrix(width / height) * camera.viewMatrix();

    std::vector<glm::mat4> bindWorld = skeleton_.bindPoseMatrices();
    const auto& skinning = player_.skinningMatrices();
    if (skinning.size() != skeleton_.joints.size() || bindWorld.size() != skeleton_.joints.size()) return;

    auto project = [&](const glm::vec3& worldPos, bool& visible) -> ImVec2 {
        glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.001f) {
            visible = false;
            return ImVec2();
        }
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        visible = true;
        // Vulkan's projection already flips clip-space Y to point down (see
        // core::Camera::projectionMatrix()'s comment), the same convention
        // the offscreen texture was rasterized with, so mapping NDC ->
        // screen here needs no extra flip to line up with what's rendered.
        return ImVec2(imageMin.x + (ndc.x * 0.5f + 0.5f) * width, imageMin.y + (ndc.y * 0.5f + 0.5f) * height);
    };

    std::vector<ImVec2> screenPos(skeleton_.joints.size());
    std::vector<bool> jointVisible(skeleton_.joints.size(), false);
    for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
        // The real current world position of joint i: the skinning matrix
        // applied to a point sitting exactly at that joint's own bind-pose
        // world position (its own pivot) -- the same reasoning
        // tests/test_main.cpp's testAnimationPlayerPlaybackAndPoseGeneration()
        // already establishes.
        glm::vec3 bindWorldPos = glm::vec3(bindWorld[i][3]);
        glm::vec4 currentWorld = skinning[i] * glm::vec4(bindWorldPos, 1.0f);
        bool visible = false;
        screenPos[i] = project(glm::vec3(currentWorld), visible);
        jointVisible[i] = visible;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
        int parent = skeleton_.joints[i].parentIndex;
        if (parent < 0 || !jointVisible[i] || !jointVisible[static_cast<size_t>(parent)]) continue;
        drawList->AddLine(screenPos[static_cast<size_t>(parent)], screenPos[i], IM_COL32(80, 220, 255, 220), 2.0f);
    }
    for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
        if (!jointVisible[i]) continue;
        bool selected = static_cast<int>(i) == selectedJointIndex_;
        ImU32 color = selected ? IM_COL32(255, 210, 60, 255) : IM_COL32(255, 255, 255, 230);
        drawList->AddCircleFilled(screenPos[i], selected ? 6.0f : 4.0f, color);
    }

    // Click-to-select, gated to hovering the actual preview image and a
    // real, small pick radius -- drawAndHandleOrbit() already owns
    // click-and-drag on this same image for orbiting, but that only
    // triggers past ImGui's drag-distance threshold, so a plain
    // press-and-release click (what picking a bone actually looks like)
    // never conflicts with it.
    if (ImGui::IsMouseHoveringRect(imageMin, imageMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mouse = ImGui::GetMousePos();
        int closest = -1;
        float closestDistSq = 144.0f; // 12px real pick radius
        for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
            if (!jointVisible[i]) continue;
            float dx = screenPos[i].x - mouse.x;
            float dy = screenPos[i].y - mouse.y;
            float distSq = dx * dx + dy * dy;
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                closest = static_cast<int>(i);
            }
        }
        if (closest >= 0) selectedJointIndex_ = closest;
    }
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
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Shipped Clip", "idle / walk / run / jump...")) {
        for (const auto& shipped : kShippedClips) {
            if (ImGui::Selectable(shipped.displayName)) loadShippedClip(shipped.fileBaseName, shipped.displayName);
        }
        ImGui::EndCombo();
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
    ImGui::Checkbox("Show Skeleton Overlay", &showSkeletonOverlay_);
    ImGui::SameLine();
    if (selectedJointIndex_ >= 0 && selectedJointIndex_ < static_cast<int>(skeleton_.joints.size())) {
        ImGui::Text("Selected bone: %s", skeleton_.joints[static_cast<size_t>(selectedJointIndex_)].name.c_str());
    } else {
        ImGui::TextDisabled("Selected bone: (none -- click a joint marker)");
    }

    scene_.drawAndHandleOrbit();
    // Must run immediately after drawAndHandleOrbit() -- it reads the
    // image's own ImGui item rect (GetItemRectMin/Max) and this frame's
    // just-updated orbit camera, both of which drawAndHandleOrbit() just
    // established, and it must do so before any other ImGui item changes
    // what GetItemRectMin/Max would return.
    if (showSkeletonOverlay_) drawSkeletonOverlay();

    ImGui::End();
}

void AnimationPreviewerPlugin::shutdown(core::Renderer& renderer) { scene_.destroy(renderer, allocator_, device_); }

} // namespace engine::studio::plugins
