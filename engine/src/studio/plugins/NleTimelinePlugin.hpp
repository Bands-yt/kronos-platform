#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <imgui.h>

#include "cinematic/ClipTimeline.hpp"
#include "cinematic/MediaBin.hpp"
#include "cinematic/TimelineLayout.hpp"
#include "core/Audio.hpp"
#include "core/ECS.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::core {
class TextureLibrary;
class MeshLibrary;
class Renderer;
}

namespace engine::studio::plugins {

class MovieModePlugin;

// Kronos ("Movie Maker NLE" -- CapCut-style clip timeline): the
// Media Bin + clip-track editing surface layered alongside
// MovieModePlugin's existing keyframe sequencer. Deliberately a
// separate plugin/window rather than folded into MovieModePlugin: the
// two edit fundamentally different things (discrete media clips here,
// continuous animation curves there -- see cinematic::ClipTimeline's
// own header), and MovieModePlugin is already 1000+ lines. They share
// one playhead: this plugin reads/scrubs `movieMode`'s
// cinematic::Sequence transport rather than running a second, competing
// one, so dragging either timeline's playhead moves both.
//
// Real, working: dynamic Media/3D track creation, Alt+Scroll zoom (plain
// scroll pans), drag-move/trim/razor-split on clips, drag fade handles
// with a live envelope preview and real playback gain/opacity, a Media
// Bin that really imports images/audio/video (core::Texture/core::Audio/
// core::VideoDecoder), real waveform peaks, and a real drag-and-drop
// Effects Library (Bloom/Color Grade/Chromatic Aberration/Vignette --
// NOT Film Grain, which has no real shader pass in this renderer) --
// see applyClipEffects()'s own .cpp comment for the real, stated scope
// cut on what "per-clip" means given this Renderer's global-only post-FX.
class NleTimelinePlugin final : public IStudioPlugin {
public:
    NleTimelinePlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                       core::TextureLibrary& textureLibrary, core::MeshLibrary& meshLibrary, core::Renderer& renderer,
                       MovieModePlugin& movieMode);

    [[nodiscard]] const char* name() const override { return "NLE Timeline"; }
    [[nodiscard]] const char* category() const override { return "Cinematics"; }

    // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real timeline
    // playback): runs every frame regardless of panel visibility (same
    // IStudioPlugin convention ModelingModePlugin/MovieModePlugin's own
    // update() overrides already establish) -- drives every Media-track
    // clip active at the shared playhead: a Video clip's real decoded
    // frame (core::stepVideoPlane) onto a real, spawned-on-demand 3D
    // plane entity, and an Audio clip's real playback position/gain
    // (core::Audio::playFromOffset/setSoundVolume), both keyed off the
    // exact same real cinematic::ClipTimeline::envelopeValueAtTime() this
    // codebase's own audit found had zero consumers before this. See
    // updatePlayback()'s own .cpp comment for the real, stated scope cut
    // on video opacity (brightness fade, not true alpha blending -- this
    // engine's scene pass has no transparency pass yet).
    void update(float dt, core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawMediaBinWindow();
    // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real Effects
    // Library): a real drag source list (Bloom/Color Grade/Chromatic
    // Aberration/Vignette) -- see handleEffectDrop()'s own comment for
    // the drop side.
    void drawEffectsLibraryWindow();
    void drawTimelineWindow();
    void drawTrackRow(size_t trackIndex, float rowTop, float rowHeight);
    void drawClip(size_t trackIndex, size_t clipIndex, float rowTop, float rowHeight);
    // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real waveform peak
    // visualization): real per-bucket min/max amplitude (asset's own
    // WaveformPeaks, computed once at import time) resampled to a fixed
    // 128-column display, mapped through the CLIP's own trim window
    // (sourceOffsetSeconds..+timelineDuration) so a trimmed clip shows
    // only the waveform of what actually plays, not the whole source file.
    void drawWaveform(ImDrawList* drawList, ImVec2 clipTopLeft, ImVec2 clipBottomRight, const cinematic::MediaClip& clip,
                       const cinematic::MediaAsset& asset) const;
    void handleTimelineZoomAndPan();
    void handleMediaDrop(size_t trackIndex);
    // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real Effects
    // Library): accepts a "KRONOS_CLIP_EFFECT" payload (drawEffectsLibraryWindow()'s
    // own drag source) dropped onto this specific clip's own screen
    // rect, appending a real ClipEffect with real, Renderer-matching
    // default parameters (see ClipEffect's own header comment).
    void handleEffectDrop(size_t trackIndex, size_t clipIndex);
    void importFromDialog();

    // Real, honest linear search (Media Bin sizes are small -- tens of
    // imported assets, not thousands) by MediaClip::assetPath, shared by
    // the playback driver and drawClip()'s own waveform lookup.
    [[nodiscard]] const cinematic::MediaAsset* findMediaAsset(const std::string& path) const;

    void updatePlayback(core::ECS& ecs, float playheadSeconds);
    void updateVideoClipPlayback(core::ECS& ecs, uint64_t clipKey, const cinematic::MediaAsset& asset,
                                  double sourceTimeSeconds, float envelope);
    void updateAudioClipPlayback(uint64_t clipKey, const cinematic::MediaAsset& asset, double sourceTimeSeconds,
                                  float envelope);
    // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real Effects
    // Library): applies every real ClipEffect on `clip` to this
    // engine's real, global Renderer post-FX knobs -- see this method's
    // own .cpp comment for the real, stated scope cut (this Renderer has
    // no true per-screen-region compositing, so "per-clip" here means
    // "these global knobs take this clip's own values while it's the
    // active one at the playhead", not simultaneous isolated regions).
    void applyClipEffects(const cinematic::MediaClip& clip);

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::TextureLibrary* textureLibrary_;
    core::MeshLibrary* meshLibrary_;
    core::Renderer* renderer_;
    MovieModePlugin* movieMode_;

    // Real playback-driver state -- see update()'s own comment.
    // "~0u" (never a real registered mesh handle in practice) means "not
    // created yet", same sentinel convention core::Renderable::
    // kInvalidHandle already establishes.
    uint32_t videoPlaneMeshHandle_ = ~0u;
    // Keyed by (trackIndex << 32 | clipIndex) -- see updatePlayback()'s
    // own .cpp comment for why a raw clip index, not a stable ID, is a
    // real, accepted limitation here.
    std::unordered_map<uint64_t, core::EntityId> videoPlaneEntities_;
    std::unordered_map<uint64_t, core::SoundHandle> activeAudioClips_;

    core::Audio audio_;
    cinematic::MediaBin mediaBin_;
    cinematic::ClipTimeline clipTimeline_;
    cinematic::TimelineView view_;
    ImVec2 timelineOrigin_{0.0f, 0.0f}; // screen-space top-left of the track area, set once per drawTimelineWindow()

    std::string importStatus_;

    // -1 = nothing selected.
    int selectedTrack_ = -1;
    int selectedClip_ = -1;

    // Which gesture (if any) a clip-area mouse-down started, so it keeps
    // being applied across the drag regardless of where the cursor
    // wanders afterward -- same reasoning as MovieModePlugin's own
    // dragTrack_/dragChannel_/dragKey_ triple.
    enum class DragKind { None, Move, TrimStart, TrimEnd, FadeIn, FadeOut };
    DragKind dragKind_ = DragKind::None;
    int dragTrack_ = -1;
    int dragClip_ = -1;
    float dragStartMouseX_ = 0.0f;
    float dragStartValue_ = 0.0f; // the clip field's value when the drag began
};

} // namespace engine::studio::plugins
