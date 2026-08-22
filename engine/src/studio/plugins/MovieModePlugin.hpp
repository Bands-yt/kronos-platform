#pragma once

#include <string>
#include <vector>

#include "cinematic/CameraRail.hpp"
#include "cinematic/OfflineExport.hpp"
#include "cinematic/Sequencer.hpp"
#include "cinematic/TimelineLayout.hpp"
#include "studio/IStudioPlugin.hpp"

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
    MovieModePlugin();

    [[nodiscard]] const char* name() const override { return "Movie Mode"; }
    [[nodiscard]] const char* category() const override { return "Cinematics"; }

    // Advances the transport. Runs regardless of whether the panel is
    // open, so a sequence left playing keeps playing while the window is
    // closed -- see IStudioPlugin's class comment.
    void update(float dt, core::ECS& ecs, core::EntityId selected,
                 const std::vector<core::EntityId>& selectedEntities) override;

    void drawPanel(core::ECS& ecs, core::EntityId selected,
                    const std::vector<core::EntityId>& selectedEntities) override;

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

    cinematic::Sequence sequence_;
    cinematic::TimelineView view_;
    cinematic::CameraRail rail_;
    cinematic::ExportSettings exportSettings_;

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
