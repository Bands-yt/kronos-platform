#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <imgui.h>

#include "cinematic/ClipTimeline.hpp"
#include "cinematic/MediaBin.hpp"
#include "cinematic/TimelineLayout.hpp"
#include "core/Audio.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::core {
class TextureLibrary;
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
// Real, working this pass: dynamic Media/3D track creation, Alt+Scroll
// zoom (plain scroll pans), drag-move/trim/razor-split on clips, drag
// fade handles with a live envelope preview, and a Media Bin that
// really imports images (core::Texture) and audio (core::Audio) via
// native drag-and-drop onto a track. Deliberately NOT built this pass:
// MP4/video import (no video decoder is vendored anywhere in this
// engine -- see cinematic::MediaBin's own header) and a drag-onto-clip
// Effects Library (real post-FX knobs exist on core::Renderer, but
// wiring one to animate per-clip rather than globally is a separate,
// larger design decision -- see this plugin's own .cpp file comment).
class NleTimelinePlugin final : public IStudioPlugin {
public:
    NleTimelinePlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                       core::TextureLibrary& textureLibrary, MovieModePlugin& movieMode);

    [[nodiscard]] const char* name() const override { return "NLE Timeline"; }
    [[nodiscard]] const char* category() const override { return "Cinematics"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawMediaBinWindow();
    void drawTimelineWindow();
    void drawTrackRow(size_t trackIndex, float rowTop, float rowHeight);
    void drawClip(size_t trackIndex, size_t clipIndex, float rowTop, float rowHeight);
    void handleTimelineZoomAndPan();
    void handleMediaDrop(size_t trackIndex);
    void importFromDialog();

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::TextureLibrary* textureLibrary_;
    MovieModePlugin* movieMode_;

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
