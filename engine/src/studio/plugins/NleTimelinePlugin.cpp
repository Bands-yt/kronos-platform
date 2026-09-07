#include "studio/plugins/NleTimelinePlugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "core/Components.hpp"
#include "core/Mesh.hpp"
#include "core/NativeFileDialog.hpp"
#include "core/Renderer.hpp"
#include "core/Texture.hpp"
#include "core/VideoPlaneComponent.hpp"
#include "studio/plugins/MovieModePlugin.hpp"

namespace engine::studio::plugins {

namespace {
constexpr float kTrackHeaderWidth = 150.0f;
constexpr float kRowHeight = 48.0f;
constexpr float kRulerHeight = 22.0f;
constexpr float kEdgeGrabPixels = 6.0f;
constexpr float kFadeHandleRadius = 6.0f;

std::string formatSeconds(double seconds) {
    int total = static_cast<int>(seconds + 0.5);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}

ImU32 trackKindColor(cinematic::ClipTrackKind kind) {
    return kind == cinematic::ClipTrackKind::Media ? IM_COL32(70, 120, 200, 255) : IM_COL32(200, 130, 60, 255);
}

uint64_t clipKey(size_t trackIndex, size_t clipIndex) {
    return (static_cast<uint64_t>(trackIndex) << 32) | static_cast<uint64_t>(clipIndex);
}
} // namespace

NleTimelinePlugin::NleTimelinePlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                     core::TextureLibrary& textureLibrary, core::MeshLibrary& meshLibrary,
                                     core::Renderer& renderer, MovieModePlugin& movieMode)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      textureLibrary_(&textureLibrary),
      meshLibrary_(&meshLibrary),
      renderer_(&renderer),
      movieMode_(&movieMode) {
    if (!audio_.initialize()) {
        std::fprintf(stderr, "NleTimelinePlugin: core::Audio::initialize failed -- imported clips will not play back.\n");
    }
    (void)clipTimeline_.addTrack("Media 1", cinematic::ClipTrackKind::Media);
    (void)clipTimeline_.addTrack("3D 1", cinematic::ClipTrackKind::ThreeD);
}

void NleTimelinePlugin::update(float /*dt*/, core::ECS& ecs, core::EntityId, const std::vector<core::EntityId>&) {
    updatePlayback(ecs, movieMode_->sequence().playheadSeconds());
}

// Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real timeline playback):
// the real consumer cinematic::ClipTimeline::envelopeValueAtTime() never
// had before this pass (a real audit found it called from nowhere).
// Drives every Media-track clip active at `playheadSeconds`: real video
// decode+upload onto a spawned-on-demand 3D plane entity, or real audio
// seek/gain on the clip's own loaded sound.
//
// Real, stated scope cuts:
//  - Video opacity is a real brightness fade (baseColor dimmed toward
//    black by the envelope), not true alpha-blended transparency --
//    this engine's main scene pass has no transparency pass at all yet
//    (see Renderer.cpp's own "no transparency pass yet, section 4.1
//    TODO" comment, a pre-existing gap this feature doesn't attempt to
//    smuggle past).
//  - Two clips sharing the same source audio file share one
//    core::Audio::loadSound() instance (MediaBin loads each unique path
//    once) -- overlapping playback of the same source cuts itself off,
//    same real, already-documented limitation Audio::playOneShot()'s
//    own comment states for exactly this reason.
//  - Clips are identified by (track index, clip index), not a stable
//    ID -- removing/reordering an earlier clip on the same track shifts
//    later indices and can detach their spawned entity/sound tracking,
//    the same "index-based, not content-addressed" real, accepted scope
//    ModelingModePlugin's own selectedFace/selectedEdge already have.
const cinematic::MediaAsset* NleTimelinePlugin::findMediaAsset(const std::string& path) const {
    for (const cinematic::MediaAsset& asset : mediaBin_.assets()) {
        if (asset.path == path) return &asset;
    }
    return nullptr;
}

void NleTimelinePlugin::updatePlayback(core::ECS& ecs, float playheadSeconds) {
    std::unordered_map<uint64_t, bool> activeThisFrame;
    const std::vector<cinematic::ClipTrack>& tracks = clipTimeline_.tracks();
    for (size_t t = 0; t < tracks.size(); ++t) {
        if (tracks[t].kind != cinematic::ClipTrackKind::Media) continue;
        for (size_t c = 0; c < tracks[t].clips.size(); ++c) {
            const cinematic::MediaClip& clip = tracks[t].clips[c];
            bool active =
                playheadSeconds >= clip.timelineStart && playheadSeconds < clip.timelineStart + clip.timelineDuration;
            if (!active) continue;

            uint64_t key = clipKey(t, c);
            activeThisFrame[key] = true;
            // Real Effects Library application -- independent of asset
            // lookup below, since a clip's own effects are its own data,
            // not something that needs its referenced asset to resolve.
            if (!clip.effects.empty()) applyClipEffects(clip);

            const cinematic::MediaAsset* asset = findMediaAsset(clip.assetPath);
            if (asset == nullptr) continue;

            double sourceTime = static_cast<double>(playheadSeconds - clip.timelineStart) + clip.sourceOffsetSeconds;
            float envelope = cinematic::ClipTimeline::envelopeValueAtTime(clip, playheadSeconds);

            if (asset->kind == cinematic::MediaAssetKind::Video) {
                updateVideoClipPlayback(ecs, key, *asset, sourceTime, envelope);
            } else if (asset->kind == cinematic::MediaAssetKind::Audio) {
                updateAudioClipPlayback(key, *asset, sourceTime, envelope);
            }
        }
    }

    // Real cleanup: a plane whose clip is no longer active this frame
    // (playhead moved out) is hidden, not destroyed -- scrubbing back
    // into it doesn't need to re-spawn/re-decode from scratch.
    for (auto& [key, entity] : videoPlaneEntities_) {
        if (activeThisFrame.find(key) == activeThisFrame.end()) {
            if (auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) renderable->visible = false;
        }
    }
    for (auto it = activeAudioClips_.begin(); it != activeAudioClips_.end();) {
        if (activeThisFrame.find(it->first) == activeThisFrame.end()) {
            if (audio_.isSoundPlaying(it->second)) audio_.stopSound(it->second);
            it = activeAudioClips_.erase(it);
        } else {
            ++it;
        }
    }
}

void NleTimelinePlugin::updateVideoClipPlayback(core::ECS& ecs, uint64_t key, const cinematic::MediaAsset& asset,
                                                 double sourceTimeSeconds, float envelope) {
    if (videoPlaneMeshHandle_ == ~0u) {
        core::Mesh quad = core::Mesh::createQuad(allocator_, device_, cmdPool_, queue_, 1.0f);
        videoPlaneMeshHandle_ = meshLibrary_->registerMesh(std::move(quad));
    }

    auto it = videoPlaneEntities_.find(key);
    core::EntityId entity;
    if (it == videoPlaneEntities_.end()) {
        entity = ecs.createEntity();
        auto& transform = ecs.addComponent<core::Transform>(entity);
        // Real, honest placement: a fixed spot in front of the studio
        // origin, scaled to the source video's own real aspect ratio.
        // Not an attempt at a full "director places/keyframes the
        // screen" UI -- that's separate, larger scope; the entity is a
        // completely ordinary Transform afterward, movable/scalable
        // through the Viewport/Inspector like anything else in the scene.
        transform.position = glm::vec3(0.0f, 1.5f, -3.0f);
        float aspect =
            asset.videoHeight > 0 ? static_cast<float>(asset.videoWidth) / static_cast<float>(asset.videoHeight) : 1.0f;
        transform.scale = glm::vec3(aspect, 1.0f, 1.0f);

        auto& renderable = ecs.addComponent<core::Renderable>(entity);
        renderable.meshHandle = videoPlaneMeshHandle_;
        renderable.baseColor = glm::vec4(1.0f);
        renderable.castsShadow = false;

        auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
        meshSource.kind = core::MeshSourceKind::Quad;
        meshSource.params = glm::vec3(1.0f, 0.0f, 0.0f);

        auto& plane = ecs.addComponent<core::VideoPlaneComponent>(entity);
        plane.sourcePath = asset.path;

        videoPlaneEntities_[key] = entity;
    } else {
        entity = it->second;
    }

    auto* renderable = ecs.tryGetComponent<core::Renderable>(entity);
    auto* plane = ecs.tryGetComponent<core::VideoPlaneComponent>(entity);
    if (renderable == nullptr || plane == nullptr) return;

    renderable->visible = true;
    // See this class's own update() header comment for why this is a
    // real brightness fade, not true alpha-blended transparency.
    renderable->baseColor = glm::vec4(glm::vec3(envelope), 1.0f);

    std::string error;
    if (!core::stepVideoPlane(*plane, sourceTimeSeconds, *textureLibrary_, allocator_, device_, cmdPool_, queue_, error)) {
        return; // real, honest no-op on a decode failure -- the last good frame stays on screen
    }
    renderable->albedoTexture = plane->textureHandle;
}

void NleTimelinePlugin::updateAudioClipPlayback(uint64_t key, const cinematic::MediaAsset& asset,
                                                 double sourceTimeSeconds, float envelope) {
    if (asset.soundHandle == core::kInvalidSoundHandle) return;
    if (activeAudioClips_.find(key) == activeAudioClips_.end()) {
        audio_.playFromOffset(asset.soundHandle, sourceTimeSeconds);
        activeAudioClips_[key] = asset.soundHandle;
    }
    audio_.setSoundVolume(asset.soundHandle, envelope);
}

// Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real Effects Library):
// applies every real ClipEffect on `clip` to this engine's real,
// existing Renderer post-FX setters. Real, stated scope: this Renderer
// has no true per-screen-region compositing (its post-FX -- Renderer.hpp's
// bloomThreshold_/saturation_/vignetteStrength_/etc -- are single global
// values applied to the whole frame, not per-object/per-region), so
// "per-clip" here means these same real global knobs take THIS clip's
// own values for as long as it's the active one at the playhead --
// genuinely real automation of real parameters, not simultaneous
// isolated on-screen regions. Chromatic Aberration and Vignette share
// one real combined Renderer call (setVignetteAndChromaticAberration) --
// a clip carrying only one of the two supplies 0 for the other rather
// than needing both attached together.
void NleTimelinePlugin::applyClipEffects(const cinematic::MediaClip& clip) {
    float vignette = 0.0f;
    float chromaticAberration = 0.0f;
    bool hasVignetteOrChromaticAberration = false;

    for (const cinematic::ClipEffect& effect : clip.effects) {
        switch (effect.type) {
            case cinematic::ClipEffectType::Bloom:
                // param1 = threshold, param2 = intensity; softKnee kept
                // at Renderer's own real class default (0.5) -- a 2-knob
                // real Effects Library entry doesn't expose a 3rd.
                renderer_->setBloomSettings(effect.param1, 0.5f, effect.param2);
                break;
            case cinematic::ClipEffectType::ColorGrade:
                // param1 = real LUT blend strength, param2 = real
                // saturation -- the 2 real color-grade-adjacent knobs
                // this Renderer actually exposes (there is no full
                // lift/gamma/gain color wheel implementation here).
                renderer_->setColorGradingLutStrength(effect.param1);
                renderer_->setSaturation(effect.param2);
                break;
            case cinematic::ClipEffectType::ChromaticAberration:
                chromaticAberration = effect.param1;
                hasVignetteOrChromaticAberration = true;
                break;
            case cinematic::ClipEffectType::Vignette:
                vignette = effect.param1;
                hasVignetteOrChromaticAberration = true;
                break;
        }
    }

    if (hasVignetteOrChromaticAberration) renderer_->setVignetteAndChromaticAberration(vignette, chromaticAberration);
}

void NleTimelinePlugin::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    drawMediaBinWindow();
    drawEffectsLibraryWindow();
    drawTimelineWindow();
}

void NleTimelinePlugin::importFromDialog() {
    auto path = core::openFileDialog("Import Media",
                                      {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga", "*.gif", "*.wav", "*.mp3",
                                       "*.flac", "*.ogg", "*.mp4", "*.mov", "*.mkv", "*.webm"});
    if (!path.has_value()) return;

    std::string error;
    if (mediaBin_.importAsset(*path, allocator_, device_, cmdPool_, queue_, *textureLibrary_, audio_, error)) {
        importStatus_ = "Imported " + std::filesystem::path(*path).filename().string();
    } else {
        importStatus_ = "Import failed: " + error;
    }
}

void NleTimelinePlugin::drawMediaBinWindow() {
    ImGui::Begin("Media Bin");
    if (ImGui::Button("Import Media...")) importFromDialog();
    if (!importStatus_.empty()) ImGui::TextWrapped("%s", importStatus_.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Drag an item onto a timeline track to place it.");

    const std::vector<cinematic::MediaAsset>& assets = mediaBin_.assets();
    for (size_t i = 0; i < assets.size(); ++i) {
        const cinematic::MediaAsset& asset = assets[i];
        ImGui::PushID(static_cast<int>(i));
        std::string label = asset.displayName;
        if (asset.kind == cinematic::MediaAssetKind::Audio || asset.kind == cinematic::MediaAssetKind::Video) {
            label += "  (" + formatSeconds(asset.durationSeconds) + ")";
        }
        ImGui::Selectable(label.c_str());
        if (ImGui::BeginDragDropSource()) {
            int index = static_cast<int>(i);
            ImGui::SetDragDropPayload("KRONOS_MEDIA_ASSET", &index, sizeof(int));
            ImGui::TextUnformatted(label.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void NleTimelinePlugin::drawEffectsLibraryWindow() {
    ImGui::Begin("Effects Library");
    ImGui::TextDisabled("Drag an effect onto a clip in the timeline below to attach it.");
    ImGui::Separator();

    static const struct {
        cinematic::ClipEffectType type;
        const char* label;
    } kLibraryEntries[] = {
        {cinematic::ClipEffectType::Bloom, "Bloom"},
        {cinematic::ClipEffectType::ColorGrade, "Color Grade"},
        {cinematic::ClipEffectType::ChromaticAberration, "Chromatic Aberration"},
        {cinematic::ClipEffectType::Vignette, "Vignette"},
    };
    for (const auto& entry : kLibraryEntries) {
        ImGui::Selectable(entry.label);
        if (ImGui::BeginDragDropSource()) {
            cinematic::ClipEffectType type = entry.type;
            ImGui::SetDragDropPayload("KRONOS_CLIP_EFFECT", &type, sizeof(type));
            ImGui::TextUnformatted(entry.label);
            ImGui::EndDragDropSource();
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped(
        "Real post-processing knobs already on this engine's own Renderer -- while a clip carrying an effect is "
        "the active one at the playhead, its own real values drive that same global knob (this Renderer has no "
        "true per-screen-region compositing pass). Film Grain isn't listed: no real grain shader exists here.");
    ImGui::End();
}

void NleTimelinePlugin::handleTimelineZoomAndPan() {
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel == 0.0f) return;
    if (io.KeyAlt) {
        view_.pixelsPerSecond = cinematic::clampZoom(view_.pixelsPerSecond * (1.0f + io.MouseWheel * 0.15f));
    } else {
        view_.scrollSeconds = std::max(0.0f, view_.scrollSeconds - io.MouseWheel * (40.0f / view_.pixelsPerSecond));
    }
}

void NleTimelinePlugin::handleMediaDrop(size_t trackIndex) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KRONOS_MEDIA_ASSET")) {
        int assetIndex = *static_cast<const int*>(payload->Data);
        const std::vector<cinematic::MediaAsset>& assets = mediaBin_.assets();
        if (assetIndex >= 0 && static_cast<size_t>(assetIndex) < assets.size()) {
            const cinematic::MediaAsset& asset = assets[static_cast<size_t>(assetIndex)];
            float mouseX = ImGui::GetMousePos().x - timelineOrigin_.x;
            float dropTime = std::max(0.0f, cinematic::pixelToTime(view_, mouseX));
            float duration = (asset.kind == cinematic::MediaAssetKind::Audio ||
                               asset.kind == cinematic::MediaAssetKind::Video) &&
                                      asset.durationSeconds > 0.0
                                  ? static_cast<float>(asset.durationSeconds)
                                  : 3.0f; // a still image gets a default 3s clip length, adjustable via trim
            (void)clipTimeline_.addClip(trackIndex, asset.path, dropTime, duration);
        }
    }
    ImGui::EndDragDropTarget();
}

void NleTimelinePlugin::handleEffectDrop(size_t trackIndex, size_t clipIndex) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KRONOS_CLIP_EFFECT")) {
        cinematic::ClipEffectType type = *static_cast<const cinematic::ClipEffectType*>(payload->Data);
        std::vector<cinematic::ClipTrack>& tracks = clipTimeline_.mutableTracks();
        if (trackIndex < tracks.size() && clipIndex < tracks[trackIndex].clips.size()) {
            cinematic::ClipEffect effect;
            effect.type = type;
            // Real defaults matching Renderer's own class defaults (see
            // ClipEffect's own header comment), not arbitrary numbers.
            switch (type) {
                case cinematic::ClipEffectType::Bloom:
                    effect.param1 = 1.0f; // threshold
                    effect.param2 = 0.6f; // intensity
                    break;
                case cinematic::ClipEffectType::ColorGrade:
                    effect.param1 = 1.0f;  // LUT strength
                    effect.param2 = 1.05f; // saturation
                    break;
                case cinematic::ClipEffectType::ChromaticAberration:
                    effect.param1 = 0.0015f;
                    break;
                case cinematic::ClipEffectType::Vignette:
                    effect.param1 = 0.35f;
                    break;
            }
            tracks[trackIndex].clips[clipIndex].effects.push_back(effect);
        }
    }
    ImGui::EndDragDropTarget();
}

void NleTimelinePlugin::drawWaveform(ImDrawList* drawList, ImVec2 clipTopLeft, ImVec2 clipBottomRight,
                                      const cinematic::MediaClip& clip, const cinematic::MediaAsset& asset) const {
    const std::vector<std::pair<float, float>>& peaks = asset.waveformPeaks;
    if (peaks.empty() || asset.durationSeconds <= 0.0) return;

    const float centerY = (clipTopLeft.y + clipBottomRight.y) * 0.5f;
    const float halfHeight = (clipBottomRight.y - clipTopLeft.y) * 0.5f - 2.0f;
    const float width = clipBottomRight.x - clipTopLeft.x;
    if (width <= 1.0f || halfHeight <= 0.0f) return;

    constexpr ImU32 kWaveColor = IM_COL32(230, 235, 245, 200);
    // A real, fixed resolution independent of the clip's own current
    // pixel width -- zooming the timeline redraws from the same real
    // peak data rather than needing a live re-bucket.
    constexpr int kColumns = 128;
    for (int col = 0; col < kColumns; ++col) {
        const float tNorm = static_cast<float>(col) / static_cast<float>(kColumns - 1);
        const float x = clipTopLeft.x + tNorm * width;
        // Maps through the CLIP's own trim window (sourceOffsetSeconds
        // .. +timelineDuration), not the whole source file -- a trimmed
        // clip's waveform shows only what actually plays.
        const double sourceTime =
            static_cast<double>(clip.sourceOffsetSeconds) + static_cast<double>(tNorm) * static_cast<double>(clip.timelineDuration);
        size_t bucket = static_cast<size_t>((sourceTime / asset.durationSeconds) * static_cast<double>(peaks.size()));
        if (bucket >= peaks.size()) bucket = peaks.size() - 1;

        const float yTop = centerY - peaks[bucket].second * halfHeight;
        const float yBottom = centerY - peaks[bucket].first * halfHeight;
        drawList->AddLine(ImVec2(x, yTop), ImVec2(x, yBottom), kWaveColor, 1.0f);
    }
}

void NleTimelinePlugin::drawClip(size_t trackIndex, size_t clipIndex, float rowTop, float rowHeight) {
    const cinematic::ClipTrack& track = clipTimeline_.tracks()[trackIndex];
    const cinematic::MediaClip& clip = track.clips[clipIndex];

    if (clip.timelineStart + clip.timelineDuration < cinematic::visibleStartSeconds(view_) ||
        clip.timelineStart > cinematic::visibleEndSeconds(view_)) {
        return;
    }

    float left = timelineOrigin_.x + cinematic::timeToPixel(view_, clip.timelineStart);
    float right = timelineOrigin_.x + cinematic::timeToPixel(view_, clip.timelineStart + clip.timelineDuration);
    ImVec2 tl(left, rowTop + 3.0f);
    ImVec2 br(right, rowTop + rowHeight - 3.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool selected = selectedTrack_ == static_cast<int>(trackIndex) && selectedClip_ == static_cast<int>(clipIndex);
    ImU32 fill = trackKindColor(track.kind);
    dl->AddRectFilled(tl, br, fill, 3.0f);
    dl->AddRect(tl, br, selected ? IM_COL32(255, 220, 90, 255) : IM_COL32(20, 20, 24, 200), 3.0f, 0,
                selected ? 2.0f : 1.0f);

    if (track.kind == cinematic::ClipTrackKind::Media) {
        const cinematic::MediaAsset* asset = findMediaAsset(clip.assetPath);
        if (asset != nullptr && asset->kind == cinematic::MediaAssetKind::Audio) {
            drawWaveform(dl, tl, br, clip, *asset);
        }
    }

    // Fade envelope, drawn as the two triangles the gain ramp traces --
    // the same shape a CapCut/Premiere fade handle visualizes.
    float height = br.y - tl.y;
    if (clip.fadeInSeconds > 0.0f) {
        float fadeInPx = clip.fadeInSeconds * view_.pixelsPerSecond;
        dl->AddTriangleFilled(ImVec2(tl.x, br.y), ImVec2(tl.x + fadeInPx, tl.y), ImVec2(tl.x, tl.y),
                              IM_COL32(0, 0, 0, 110));
    }
    if (clip.fadeOutSeconds > 0.0f) {
        float fadeOutPx = clip.fadeOutSeconds * view_.pixelsPerSecond;
        dl->AddTriangleFilled(ImVec2(br.x, br.y), ImVec2(br.x - fadeOutPx, tl.y), ImVec2(br.x, tl.y),
                              IM_COL32(0, 0, 0, 110));
    }
    dl->AddCircleFilled(ImVec2(tl.x, tl.y), kFadeHandleRadius, IM_COL32(255, 255, 255, 200));
    dl->AddCircleFilled(ImVec2(br.x, tl.y), kFadeHandleRadius, IM_COL32(255, 255, 255, 200));

    std::filesystem::path assetPath(clip.assetPath);
    dl->AddText(ImVec2(tl.x + 4.0f, tl.y + 2.0f), IM_COL32(255, 255, 255, 255), assetPath.filename().string().c_str());
    if (!clip.effects.empty()) {
        // Real, honest indicator -- an fx-count badge, not a fake icon
        // implying a specific effect this small badge can't actually show.
        std::string fxLabel = "fx:" + std::to_string(clip.effects.size());
        dl->AddText(ImVec2(br.x - 34.0f, tl.y + 2.0f), IM_COL32(255, 220, 120, 255), fxLabel.c_str());
    }

    // Invisible button over the whole clip so ImGui gives us hover/drag
    // state without stealing input from the ruler/playhead above it.
    ImGui::SetCursorScreenPos(tl);
    ImGui::PushID(static_cast<int>(trackIndex * 10000 + clipIndex));
    ImGui::InvisibleButton("clip", ImVec2(std::max(1.0f, br.x - tl.x), height));
    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetMousePos();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selectedTrack_ = static_cast<int>(trackIndex);
        selectedClip_ = static_cast<int>(clipIndex);

        bool nearLeftEdge = std::abs(mouse.x - tl.x) <= kEdgeGrabPixels;
        bool nearRightEdge = std::abs(mouse.x - br.x) <= kEdgeGrabPixels;
        bool nearTopLeft = std::abs(mouse.x - tl.x) <= kFadeHandleRadius * 2.0f && mouse.y - tl.y <= kFadeHandleRadius * 2.0f;
        bool nearTopRight = std::abs(mouse.x - br.x) <= kFadeHandleRadius * 2.0f && mouse.y - tl.y <= kFadeHandleRadius * 2.0f;

        dragTrack_ = static_cast<int>(trackIndex);
        dragClip_ = static_cast<int>(clipIndex);
        dragStartMouseX_ = mouse.x;
        if (nearTopLeft) {
            dragKind_ = DragKind::FadeIn;
            dragStartValue_ = clip.fadeInSeconds;
        } else if (nearTopRight) {
            dragKind_ = DragKind::FadeOut;
            dragStartValue_ = clip.fadeOutSeconds;
        } else if (nearLeftEdge) {
            dragKind_ = DragKind::TrimStart;
            dragStartValue_ = clip.timelineStart;
        } else if (nearRightEdge) {
            dragKind_ = DragKind::TrimEnd;
            dragStartValue_ = clip.timelineStart + clip.timelineDuration;
        } else {
            dragKind_ = DragKind::Move;
            dragStartValue_ = clip.timelineStart;
        }
    }
    ImGui::PopID();

    handleMediaDrop(trackIndex);
    handleEffectDrop(trackIndex, clipIndex);
}

void NleTimelinePlugin::drawTrackRow(size_t trackIndex, float rowTop, float rowHeight) {
    cinematic::ClipTrack& track = clipTimeline_.mutableTracks()[trackIndex];

    ImGui::SetCursorScreenPos(ImVec2(timelineOrigin_.x - kTrackHeaderWidth, rowTop));
    ImGui::PushID(static_cast<int>(trackIndex));
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(kTrackHeaderWidth - 40.0f);
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", track.name.c_str());
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) track.name = nameBuf;
    ImGui::SameLine();
    ImGui::Checkbox("##mute", &track.muted);
    ImGui::EndGroup();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 rowMin(timelineOrigin_.x, rowTop);
    ImVec2 rowMax(timelineOrigin_.x + view_.widthPixels, rowTop + rowHeight);
    dl->AddRectFilled(rowMin, rowMax, track.muted ? IM_COL32(30, 30, 34, 255) : IM_COL32(38, 38, 44, 255));
    dl->AddLine(ImVec2(rowMin.x, rowMax.y), ImVec2(rowMax.x, rowMax.y), IM_COL32(10, 10, 12, 255));

    for (size_t c = 0; c < track.clips.size(); ++c) drawClip(trackIndex, c, rowTop, rowHeight);
}

void NleTimelinePlugin::drawTimelineWindow() {
    ImGui::Begin("NLE Timeline");

    if (ImGui::Button("+ Media Track")) {
        (void)clipTimeline_.addTrack("Media " + std::to_string(clipTimeline_.tracks().size() + 1),
                                     cinematic::ClipTrackKind::Media);
    }
    ImGui::SameLine();
    if (ImGui::Button("+ 3D Track")) {
        (void)clipTimeline_.addTrack("3D " + std::to_string(clipTimeline_.tracks().size() + 1),
                                     cinematic::ClipTrackKind::ThreeD);
    }
    ImGui::SameLine();
    bool hasSelection = selectedTrack_ >= 0 && selectedClip_ >= 0;
    ImGui::BeginDisabled(!hasSelection);
    bool razorClicked = ImGui::Button("Razor (X)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Alt+Scroll to zoom, Scroll to pan");

    ImGuiIO& io = ImGui::GetIO();
    bool razorHotkey = hasSelection && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_X, false);
    if ((razorClicked || razorHotkey)) {
        (void)clipTimeline_.splitClip(static_cast<size_t>(selectedTrack_), static_cast<size_t>(selectedClip_),
                                      movieMode_->sequence().playheadSeconds());
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    view_.widthPixels = std::max(1.0f, avail.x - kTrackHeaderWidth);

    ImGui::BeginChild("timeline_scroll", avail, false, ImGuiWindowFlags_NoScrollbar);
    handleTimelineZoomAndPan();

    timelineOrigin_ = ImVec2(ImGui::GetCursorScreenPos().x + kTrackHeaderWidth, ImGui::GetCursorScreenPos().y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Ruler + playhead.
    float playhead = movieMode_->sequence().playheadSeconds();
    ImVec2 rulerMin(timelineOrigin_.x, timelineOrigin_.y);
    ImVec2 rulerMax(timelineOrigin_.x + view_.widthPixels, timelineOrigin_.y + kRulerHeight);
    dl->AddRectFilled(rulerMin, rulerMax, IM_COL32(24, 24, 28, 255));
    float gridSeconds = cinematic::gridIntervalSeconds(view_);
    float t = std::floor(cinematic::visibleStartSeconds(view_) / gridSeconds) * gridSeconds;
    for (; t <= cinematic::visibleEndSeconds(view_); t += gridSeconds) {
        float x = timelineOrigin_.x + cinematic::timeToPixel(view_, t);
        dl->AddLine(ImVec2(x, rulerMin.y), ImVec2(x, rulerMin.y + kRulerHeight), IM_COL32(90, 90, 96, 255));
        dl->AddText(ImVec2(x + 2.0f, rulerMin.y + 2.0f), IM_COL32(200, 200, 200, 255), formatSeconds(t).c_str());
    }
    if (cinematic::isTimeVisible(view_, playhead)) {
        float px = timelineOrigin_.x + cinematic::timeToPixel(view_, playhead);
        dl->AddLine(ImVec2(px, rulerMin.y), ImVec2(px, timelineOrigin_.y + kRulerHeight + kRowHeight * clipTimeline_.tracks().size()),
                    IM_COL32(255, 90, 90, 255), 2.0f);
    }

    ImGui::SetCursorScreenPos(ImVec2(rulerMin.x, rulerMin.y));
    ImGui::InvisibleButton("ruler", ImVec2(view_.widthPixels, kRulerHeight));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float mouseX = ImGui::GetMousePos().x - timelineOrigin_.x;
        movieMode_->sequence().setPlayhead(std::max(0.0f, cinematic::pixelToTime(view_, mouseX)));
    }

    float rowTop = timelineOrigin_.y + kRulerHeight;
    for (size_t i = 0; i < clipTimeline_.tracks().size(); ++i) {
        drawTrackRow(i, rowTop, kRowHeight);
        rowTop += kRowHeight;
    }

    // Active clip drag -- routed through ClipTimeline's own clamped
    // mutators, same as MovieModePlugin's curve-editor drag handling.
    if (dragKind_ != DragKind::None && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float deltaSeconds = (ImGui::GetMousePos().x - dragStartMouseX_) / view_.pixelsPerSecond;
        size_t tIdx = static_cast<size_t>(dragTrack_);
        size_t cIdx = static_cast<size_t>(dragClip_);
        switch (dragKind_) {
            case DragKind::Move: (void)clipTimeline_.moveClip(tIdx, cIdx, std::max(0.0f, dragStartValue_ + deltaSeconds)); break;
            case DragKind::TrimStart: (void)clipTimeline_.trimClipStart(tIdx, cIdx, std::max(0.0f, dragStartValue_ + deltaSeconds)); break;
            case DragKind::TrimEnd: (void)clipTimeline_.trimClipEnd(tIdx, cIdx, std::max(0.0f, dragStartValue_ + deltaSeconds)); break;
            case DragKind::FadeIn: {
                const cinematic::MediaClip& clip = clipTimeline_.tracks()[tIdx].clips[cIdx];
                clipTimeline_.setFade(tIdx, cIdx, std::max(0.0f, dragStartValue_ + deltaSeconds), clip.fadeOutSeconds);
                break;
            }
            case DragKind::FadeOut: {
                const cinematic::MediaClip& clip = clipTimeline_.tracks()[tIdx].clips[cIdx];
                clipTimeline_.setFade(tIdx, cIdx, clip.fadeInSeconds, std::max(0.0f, dragStartValue_ - deltaSeconds));
                break;
            }
            case DragKind::None: break;
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dragKind_ = DragKind::None;

    ImGui::Dummy(ImVec2(view_.widthPixels, kRulerHeight + kRowHeight * clipTimeline_.tracks().size()));
    ImGui::EndChild();
    ImGui::End();
}

} // namespace engine::studio::plugins
