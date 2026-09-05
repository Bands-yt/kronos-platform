#pragma once

#include <functional>
#include <string>

#include "cinematic/CameraRail.hpp"
#include "cinematic/OfflineExport.hpp"
#include "cinematic/Sequencer.hpp"
#include "core/ECS.hpp"

namespace engine::cinematic {

// One real render request: exactly one sub-frame sample of one export
// frame job, with the scene already driven to that instant (playhead set
// and applySequenceToScene() already applied -- see SequenceApplier.hpp)
// and the camera rail already sampled. The capture callback's only job
// is to actually render and read back pixels; everything about *when*
// and *what pose* is decided here, which is what keeps this half of the
// pipeline real and headless-testable.
struct ExportSampleRequest {
    const ExportFrameJob* job = nullptr;
    size_t sampleIndex = 0; // index into job->sampleTimesSeconds
    float sampleTimeSeconds = 0.0f;
    RailSample cameraSample;
    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline"):
    // real, authored post-FX for this instant -- see
    // cinematic::postFxAtTime()'s own comment. The capture callback
    // (trailer::CaptureRig) applies this to the live core::Renderer;
    // this struct itself has no Renderer dependency, keeping this file
    // Vulkan-free like the rest of the Offline Export pipeline.
    PostFxSample postFx;
};

// Returns false to abort the whole export -- a real capture failure
// (disk full, a GPU error) corrupting the rest of an in-progress
// sequence on disk is worse than stopping as soon as it happens.
using ExportCaptureFn = std::function<bool(const ExportSampleRequest& request)>;

// Walks buildExportSchedule()'s job list in order. For each sub-frame
// sample: sets the sequence playhead, applies it to `ecs`
// (cinematic::applySequenceToScene() -- the same real per-frame path
// studio::plugins::MovieModePlugin::update() already drives, see that
// file's own comment), evaluates the camera rail at that instant, and
// invokes `capture`. Restores the sequence's original playhead and
// resets the rail's aim damping before returning, so running an export
// doesn't leave the editor's own live view mid-scrub.
//
// Real, pure orchestration: no Vulkan, no ImGui. A test can pass a fake
// `capture` that just records which (frame, sample) it was asked to
// render, proving the schedule is actually walked in order with the
// right playhead per sample -- the single highest-value headless test in
// the Offline Export pipeline, since it is the one thing that can't be
// proven by testing OfflineExport.hpp's schedule maths alone.
// `postFxFallback`: the real current renderer post-FX state (see
// PostFxSample's own comment) -- defaults to PostFxSample{}'s own
// class-default values so existing callers that don't care about post-FX
// (every current test) keep compiling unchanged.
[[nodiscard]] bool runExportSchedule(Sequence& sequence, CameraRail& rail, const ExportSettings& settings,
                                      core::ECS& ecs, const ExportCaptureFn& capture, std::string& outError,
                                      const PostFxSample& postFxFallback = PostFxSample{});

} // namespace engine::cinematic
