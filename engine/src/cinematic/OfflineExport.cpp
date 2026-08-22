#include "cinematic/OfflineExport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine::cinematic {

namespace {
constexpr uint32_t kMaxDimension = 7680; // 8K wide
constexpr int kMaxSubFrameSamples = 64;
} // namespace

const char* channelName(ExportChannel channel) {
    switch (channel) {
        case ExportChannel::Depth: return "depth";
        case ExportChannel::MotionVectors: return "motion";
        case ExportChannel::Color:
        default: return "color";
    }
}

ExportImageFormat formatForChannel(const ExportSettings& settings, ExportChannel channel) {
    // Depth and motion vectors are float data. Writing them as 8-bit PNG
    // would quantise away exactly the precision a compositor wants, so
    // the choice is not offered.
    if (channel == ExportChannel::Depth || channel == ExportChannel::MotionVectors) return ExportImageFormat::Exr;
    return settings.colorFormat;
}

bool validateExportSettings(const ExportSettings& settings, std::string& outError) {
    if (settings.outputDirectory.empty()) {
        outError = "Choose an output directory.";
        return false;
    }
    if (settings.resolution.width == 0 || settings.resolution.height == 0) {
        outError = "Resolution must be non-zero.";
        return false;
    }
    if (settings.resolution.width > kMaxDimension || settings.resolution.height > kMaxDimension) {
        outError = "Resolution exceeds the supported maximum of 7680x4320 (8K).";
        return false;
    }
    if (settings.channels.empty()) {
        outError = "Select at least one channel to export.";
        return false;
    }
    if (settings.motionBlur.enabled) {
        if (settings.motionBlur.subFrameSamples < 1 || settings.motionBlur.subFrameSamples > kMaxSubFrameSamples) {
            outError = "Motion blur samples must be between 1 and 64.";
            return false;
        }
        if (settings.motionBlur.shutterAngleFraction <= 0.0f || settings.motionBlur.shutterAngleFraction > 1.0f) {
            outError = "Shutter fraction must be greater than 0 and at most 1.";
            return false;
        }
    }
    if (settings.endSeconds > 0.0f && settings.endSeconds <= settings.startSeconds) {
        outError = "The export end time must be after the start time.";
        return false;
    }
    if (settings.startSeconds < 0.0f) {
        outError = "The export start time cannot be negative.";
        return false;
    }
    outError.clear();
    return true;
}

int exportFrameCount(const ExportSettings& settings, float sequenceDurationSeconds) {
    const float end = settings.endSeconds > 0.0f ? settings.endSeconds : sequenceDurationSeconds;
    const float span = end - settings.startSeconds;
    if (span <= 0.0f) return 0;

    const int fps = framesPerSecond(settings.frameRate);
    // Round rather than truncate: a 1.0s shot at 24fps is 24 frames, and
    // floating-point error must not silently drop the last one.
    return static_cast<int>(std::floor(span * static_cast<float>(fps) + 0.5f));
}

void buildExportSchedule(const ExportSettings& settings, float sequenceDurationSeconds,
                          std::vector<ExportFrameJob>& outJobs) {
    outJobs.clear();

    const int frameCount = exportFrameCount(settings, sequenceDurationSeconds);
    if (frameCount <= 0) return;

    const int fps = framesPerSecond(settings.frameRate);
    const float frameDuration = 1.0f / static_cast<float>(fps);

    const bool blur = settings.motionBlur.enabled && settings.motionBlur.subFrameSamples > 1;
    const int sampleCount = blur ? settings.motionBlur.subFrameSamples : 1;

    outJobs.reserve(static_cast<size_t>(frameCount));
    for (int frame = 0; frame < frameCount; ++frame) {
        ExportFrameJob job;
        job.frameIndex = frame;
        job.frameStartSeconds = settings.startSeconds + static_cast<float>(frame) * frameDuration;
        job.sampleTimesSeconds.reserve(static_cast<size_t>(sampleCount));

        if (!blur) {
            job.sampleTimesSeconds.push_back(job.frameStartSeconds);
        } else {
            // Centre the shutter on the frame so blur smears symmetrically
            // around it rather than trailing behind, which is what a real
            // rotary shutter does.
            const float shutterSpan = frameDuration * settings.motionBlur.shutterAngleFraction;
            const float shutterStart = job.frameStartSeconds - shutterSpan * 0.5f;
            for (int sample = 0; sample < sampleCount; ++sample) {
                // Sample at bin centres, not edges: edge sampling
                // double-weights the ends of the shutter.
                const float fraction = (static_cast<float>(sample) + 0.5f) / static_cast<float>(sampleCount);
                job.sampleTimesSeconds.push_back(std::max(shutterStart + shutterSpan * fraction, 0.0f));
            }
        }
        outJobs.push_back(std::move(job));
    }
}

std::string exportFrameFilename(const ExportSettings& settings, int frameIndex, ExportChannel channel) {
    const char* extension = formatForChannel(settings, channel) == ExportImageFormat::Exr ? "exr" : "png";
    char buffer[512];
    // Six digits: a long render's directory listing then sorts lexically
    // in the same order it rendered.
    std::snprintf(buffer, sizeof(buffer), "%s_%s_%06d.%s", settings.filePrefix.c_str(), channelName(channel),
                  std::max(frameIndex, 0), extension);
    return buffer;
}

uint64_t estimatedBytesPerFrame(const ExportSettings& settings) {
    const uint64_t pixels = static_cast<uint64_t>(settings.resolution.width) *
                             static_cast<uint64_t>(settings.resolution.height);
    uint64_t total = 0;
    for (ExportChannel channel : settings.channels) {
        if (formatForChannel(settings, channel) == ExportImageFormat::Exr) {
            // Half-float RGB, roughly, before compression.
            total += pixels * 6;
        } else {
            total += pixels * 4; // RGBA8
        }
    }
    return total;
}

} // namespace engine::cinematic
