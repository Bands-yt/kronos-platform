#include "studio/plugins/NleTimelinePlugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "core/NativeFileDialog.hpp"
#include "core/Texture.hpp"
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
} // namespace

NleTimelinePlugin::NleTimelinePlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                     core::TextureLibrary& textureLibrary, MovieModePlugin& movieMode)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      textureLibrary_(&textureLibrary),
      movieMode_(&movieMode) {
    if (!audio_.initialize()) {
        std::fprintf(stderr, "NleTimelinePlugin: core::Audio::initialize failed -- imported clips will not play back.\n");
    }
    (void)clipTimeline_.addTrack("Media 1", cinematic::ClipTrackKind::Media);
    (void)clipTimeline_.addTrack("3D 1", cinematic::ClipTrackKind::ThreeD);
}

void NleTimelinePlugin::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    drawMediaBinWindow();
    drawTimelineWindow();
}

void NleTimelinePlugin::importFromDialog() {
    auto path = core::openFileDialog("Import Media",
                                      {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga", "*.gif", "*.wav", "*.mp3",
                                       "*.flac", "*.ogg"});
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
        if (asset.kind == cinematic::MediaAssetKind::Audio) label += "  (" + formatSeconds(asset.durationSeconds) + ")";
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
            float duration = asset.kind == cinematic::MediaAssetKind::Audio && asset.durationSeconds > 0.0
                                  ? static_cast<float>(asset.durationSeconds)
                                  : 3.0f; // a still image gets a default 3s clip length, adjustable via trim
            (void)clipTimeline_.addClip(trackIndex, asset.path, dropTime, duration);
        }
    }
    ImGui::EndDragDropTarget();
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
