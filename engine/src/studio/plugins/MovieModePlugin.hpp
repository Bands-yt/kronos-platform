#pragma once

#include <string>
#include <vector>

#include <volk.h>

#include "cinematic/CameraRail.hpp"
#include "cinematic/OfflineExport.hpp"
#include "cinematic/Sequencer.hpp"
#include "cinematic/TimelineLayout.hpp"
#include "studio/IStudioPlugin.hpp"
#include "trailer/CaptureRig.hpp"

namespace engine::core {
class Renderer;
class MeshLibrary;
class TextureLibrary;
} // namespace engine::core

namespace engine::studio::plugins {

// Studio's cinematic authoring surface: the editor over the
// engine::cinematic module.
//
// Everything the module already provides -- curve sampling, the timeline
// hit-testing/zoom maths, the camera rail splines, the export schedule --
// stays where it is. This is deliberately a *view*: it converts pixels to
// times through cinematic::TimelineLayout and never reimplements that
// mapping inline, which is the whole reason that maths was separated from
// a draw loop in the first place (see TimelineLayout.hpp's header). Every
// interaction below routes through a tested function rather than doing
// its own arithmetic on ImGui coordinates.
//
// The 3D rail gizmo is NOT drawn here. It is drawn by ViewportPanel,
// which owns the camera matrices and the viewport image rectangle, and
// reads this plugin's rail through the accessors below -- the same shape
// ViewportPanel already uses for PhysicsPreviewPlugin's collider
// overlays.
class MovieModePlugin final : public IStudioPlugin {
public:
    MovieModePlugin(core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary);

    [[nodiscard]] const char* name() const override { return "Movie Mode"; }
    [[nodiscard]] const char* category() const override { return "Cinematics"; }

    // Advances the transport. Runs regardless of whether the panel is
    // open, so a sequence left playing keeps playing while the window is
    // closed -- see IStudioPlugin's class comment. Also drives the real
    // Offline Export pipeline: when the "Render" button has requested one
    // (see drawExporterModal()), this is where trailer::CaptureRig's real
    // exportSequence() actually runs -- see this file's own .cpp comment
    // on why here rather than renderPreview(), and on the real "this
    // blocks Studio until it's done" tradeoff that implies.
    void update(float dt, core::ECS& ecs, core::EntityId selected,
                 const std::vector<core::EntityId>& selectedEntities) override;

    void drawPanel(core::ECS& ecs, core::EntityId selected,
                    const std::vector<core::EntityId>& selectedEntities) override;

    // Real, minimal "give me a live Renderer&" hook -- the same real
    // shape avatarPreviewer_'s own renderPreview(cmd, renderer) already
    // uses in StudioApp's shared pre-pass callback (see that file's own
    // comment), just without a live preview texture of its own: this
    // plugin doesn't need one, only a real Renderer& to actually drive
    // trailer::CaptureRig with, cached for update() to use afterward --
    // the same real cachedRenderer_ shape studio::plugins::TrailerPanel
    // already established for exactly this reason (see that class's own
    // header comment).
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

    // --- read/written by ViewportPanel's rail overlay ---------------------
    [[nodiscard]] cinematic::CameraRail& rail() { return rail_; }
    [[nodiscard]] const cinematic::CameraRail& rail() const { return rail_; }
    [[nodiscard]] bool showRailGizmo() const { return showRailGizmo_; }
    [[nodiscard]] bool showLookAtLines() const { return showLookAtLines_; }
    // -1 when nothing is selected. ViewportPanel highlights this handle
    // and writes back the index the user clicks.
    [[nodiscard]] int selectedRailPoint() const { return selectedRailPoint_; }
    void setSelectedRailPoint(int index) { selectedRailPoint_ = index; }
    // Moves a control point, e.g. from a viewport handle drag. Bounds
    // checked, so a stale index from a point deleted between frames is an
    // honest no-op rather than a write past the end.
    void moveRailPoint(int index, const glm::vec3& position);
    // Normalised rail parameter for the current playhead, so the viewport
    // can mark where the camera actually is in the shot.
    [[nodiscard]] float railParameterAtPlayhead() const;

    // True while a timeline gesture owns the transport -- a playhead
    // scrub or a loop-handle drag. update() checks this; see there.
    [[nodiscard]] bool isScrubbing() const { return draggingPlayhead_ || draggingLoopHandle_ != 0; }
    // Drives that state directly, for tests that cannot run an ImGui
    // gesture.
    void setScrubbingForTest(bool scrubbing) { draggingPlayhead_ = scrubbing; }

    // Exposed for tests: the schedule the exporter last built.
    [[nodiscard]] const std::vector<cinematic::ExportFrameJob>& lastExportSchedule() const { return lastSchedule_; }
    [[nodiscard]] cinematic::Sequence& sequence() { return sequence_; }
    [[nodiscard]] cinematic::ExportSettings& exportSettings() { return exportSettings_; }
    // Runs the same validate-then-schedule the "Render Movie Sequence"
    // button runs, without the modal. Returns false and fills `outError`
    // exactly as the modal reports it.
    bool buildExport(std::string& outError);

private:
    void seedDefaultSequence();

    void drawTransport();
    // The 6-track timeline: header column, time ruler, per-track key rows,
    // playhead and loop region.
    void drawTimeline();
    void drawCurveEditor();
    void drawExporterModal();
    void drawRailEditor();

    // Currently-selected channel, or nullptr when the selection is stale
    // (a track or channel removed since it was made).
    [[nodiscard]] cinematic::TrackChannel* selectedChannel();

    // (Re-)initializes captureRig_ at `desired` if it isn't already
    // initialized at exactly that extent -- CaptureRig's own real
    // "resolution chosen once, not resized live" contract (see its own
    // header comment) means a creator changing the export resolution
    // between renders needs a real shutdown()+initialize() cycle, not a
    // live resize.
    void ensureCaptureRig(core::Renderer& renderer, VkExtent2D desired);

    cinematic::Sequence sequence_;
    cinematic::TimelineView view_;
    cinematic::CameraRail rail_;
    cinematic::ExportSettings exportSettings_;

    // --- real Offline Export GPU state -------------------------------------
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;
    trailer::CaptureRig captureRig_;
    bool captureRigInitialized_ = false;
    // Set by renderPreview(), read by update() -- see TrailerPanel's own
    // identical cachedRenderer_ field for why update() (which runs
    // regardless of this panel's open/closed state) needs its own cached
    // pointer rather than a Renderer& parameter it doesn't have.
    core::Renderer* cachedRenderer_ = nullptr;
    // Deferred-by-one-frame trigger, same real shape StudioApp's own
    // packageThumbnailCaptureRequested_ already established for "an
    // ImGui button click has no live Renderer& to act on immediately."
    bool exportRequested_ = false;

    // --- selection -------------------------------------------------------
    int selectedTrack_ = 0;
    int selectedChannelIndex_ = 0;
    int selectedKey_ = -1;

    // --- timeline interaction state --------------------------------------
    bool snapToFrames_ = true;
    bool draggingPlayhead_ = false;
    // -1 = loop start handle, +1 = loop end handle, 0 = not dragging one.
    int draggingLoopHandle_ = 0;
    // Which key a timeline drag is moving; -1 when no drag is active. Held
    // as (track, channel, key) because a drag must keep addressing the key
    // it grabbed even as its index changes when insertKeyframe() re-sorts.
    int dragTrack_ = -1;
    int dragChannel_ = -1;
    int dragKey_ = -1;

    // --- curve editor interaction state ----------------------------------
    // 0 = the key itself, -1 = its in handle, +1 = its out handle.
    int curveDragPart_ = 0;
    int curveDragKey_ = -1;
    bool curveAutoFit_ = true;
    float curveValueMin_ = -1.0f;
    float curveValueMax_ = 1.0f;

    // --- rail / export ----------------------------------------------------
    bool showRailGizmo_ = true;
    bool showLookAtLines_ = true;
    int selectedRailPoint_ = -1;
    bool exporterOpen_ = false;
    std::vector<cinematic::ExportFrameJob> lastSchedule_;
    std::string exportStatus_;
    // Events the transport crossed on the last update, shown in the
    // footer so a script trigger firing is visible while scrubbing.
    std::vector<cinematic::TrackEvent> lastFiredEvents_;
};

} // namespace engine::studio::plugins
