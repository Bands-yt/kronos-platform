#include "cinematic/ExportRunner.hpp"

#include "cinematic/SequenceApplier.hpp"

namespace engine::cinematic {

bool runExportSchedule(Sequence& sequence, CameraRail& rail, const ExportSettings& settings, core::ECS& ecs,
                        const ExportCaptureFn& capture, std::string& outError, const PostFxSample& postFxFallback) {
    if (!validateExportSettings(settings, outError)) return false;

    std::vector<ExportFrameJob> jobs;
    buildExportSchedule(settings, sequence.durationSeconds(), jobs);
    if (jobs.empty()) {
        outError = "Nothing to export: the schedule produced zero frames.";
        return false;
    }

    const float savedPlayhead = sequence.playheadSeconds();
    // A fresh export starts un-damped, like a cut to its first frame
    // rather than a camera move that was already in flight.
    rail.resetDamping();

    bool ok = true;
    for (const ExportFrameJob& job : jobs) {
        for (size_t sampleIndex = 0; sampleIndex < job.sampleTimesSeconds.size(); ++sampleIndex) {
            const float sampleTime = job.sampleTimesSeconds[sampleIndex];
            sequence.setPlayhead(sampleTime);
            applySequenceToScene(sequence, sequence.playheadSeconds(), ecs);

            // Real, authored easing -- the same real source of truth
            // studio::plugins::MovieModePlugin::railParameterAtPlayhead()
            // uses for the live viewport, so an export actually renders
            // the camera timing a creator authored on the timeline
            // instead of a raw linear scrub (see railParameterAtTime()'s
            // own comment).
            const float railT = railParameterAtTime(sequence, sequence.playheadSeconds());

            // 0 deltaSeconds: an export sample is a specific requested
            // instant, not real playback time passing -- damping would
            // smear it toward wherever the previous sample happened to
            // land instead of landing exactly where it was asked to.
            ExportSampleRequest request;
            request.job = &job;
            request.sampleIndex = sampleIndex;
            request.sampleTimeSeconds = sampleTime;
            request.cameraSample = rail.sample(railT, 0.0f);
            // Real, authored post-FX -- see postFxAtTime()'s own comment.
            // Sampled at the same instant as everything else above, so a
            // bloom-intensity ramp lands on the exact frame it was
            // authored to.
            request.postFx = postFxAtTime(sequence, sequence.playheadSeconds(), postFxFallback);

            if (!capture(request)) {
                outError = "Capture failed at frame " + std::to_string(job.frameIndex) + ".";
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }

    sequence.setPlayhead(savedPlayhead);
    rail.resetDamping();
    return ok;
}

} // namespace engine::cinematic
