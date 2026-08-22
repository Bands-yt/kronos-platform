#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cinematic/Sequencer.hpp"

namespace engine::cinematic {

// Offline sequence export.
//
// "Offline" is the important word: this runs unthrottled and decoupled
// from real time. A 10-second shot at 60fps is exactly 600 frames whether
// each takes 2ms or 2 minutes to render, which is the entire difference
// between a preview capture and a master render.
//
// This header owns the schedule and the naming; the actual GPU readback
// is core::trailer::CaptureRig's job. Keeping them apart is what lets the
// schedule be tested without a device.

enum class ExportChannel : uint8_t {
    Color = 0,
    Depth = 1,
    MotionVectors = 2,
};

enum class ExportImageFormat : uint8_t {
    // 8-bit, fine for a preview or a final colour pass.
    Png = 0,
    // Float, which Depth and MotionVectors genuinely need -- quantising
    // a depth buffer to 8 bits destroys exactly the precision a
    // compositor came for.
    Exr = 1,
};

struct ExportResolution {
    uint32_t width = 1920;
    uint32_t height = 1080;
};

namespace resolution_presets {
inline constexpr ExportResolution k1080p{1920, 1080};
inline constexpr ExportResolution k1440p{2560, 1440};
inline constexpr ExportResolution k4K{3840, 2160};
inline constexpr ExportResolution k8K{7680, 4320};
} // namespace resolution_presets

struct MotionBlurSettings {
    bool enabled = false;
    // Sub-frame samples accumulated per output frame. 1 disables blur;
    // higher is smoother and linearly more expensive.
    int subFrameSamples = 8;
    // Fraction of the frame interval the "shutter" is open. 0.5 is a
    // 180-degree shutter, the film convention.
    float shutterAngleFraction = 0.5f;
};

struct ExportSettings {
    std::string outputDirectory = "export";
    std::string filePrefix = "frame";
    ExportResolution resolution = resolution_presets::k1080p;
    SequenceFrameRate frameRate = SequenceFrameRate::Fps24;
    ExportImageFormat colorFormat = ExportImageFormat::Png;
    // Depth and motion vectors are always float; only the colour pass has
    // a meaningful choice.
    std::vector<ExportChannel> channels{ExportChannel::Color};
    MotionBlurSettings motionBlur;
    // Inclusive start, exclusive end. Zero end means "to the end of the
    // sequence".
    float startSeconds = 0.0f;
    float endSeconds = 0.0f;
};

// One unit of work: a single output frame, and the sub-frame times whose
// renders accumulate into it.
struct ExportFrameJob {
    int frameIndex = 0;
    float frameStartSeconds = 0.0f;
    // Times to render and average. Exactly one entry when motion blur is
    // off.
    std::vector<float> sampleTimesSeconds;
};

// Validates settings, returning a human-readable reason when they cannot
// produce a render. Refusing up front beats discovering it 4000 frames in.
[[nodiscard]] bool validateExportSettings(const ExportSettings& settings, std::string& outError);

// Total output frames for a sequence of `sequenceDurationSeconds`.
[[nodiscard]] int exportFrameCount(const ExportSettings& settings, float sequenceDurationSeconds);

// Builds the full schedule. `outJobs` is cleared and filled.
//
// Sub-frame sample times are spread across the open shutter and CENTRED
// on the frame, so motion blur smears symmetrically around the frame
// rather than trailing behind it, which is what a real shutter does.
void buildExportSchedule(const ExportSettings& settings, float sequenceDurationSeconds,
                          std::vector<ExportFrameJob>& outJobs);

// Zero-padded filename for a frame and channel, e.g.
// "frame_color_000042.png". Six digits, so a long render's directory
// listing sorts lexically in the same order it renders.
[[nodiscard]] std::string exportFrameFilename(const ExportSettings& settings, int frameIndex, ExportChannel channel);

[[nodiscard]] const char* channelName(ExportChannel channel);
// Depth and motion vectors are forced to EXR regardless of the colour
// setting -- see ExportImageFormat.
[[nodiscard]] ExportImageFormat formatForChannel(const ExportSettings& settings, ExportChannel channel);

// Rough bytes for one fully-exported frame across every enabled channel.
// Surfaced so an 8K multi-channel render can warn about disk use before
// it starts rather than filling the drive.
[[nodiscard]] uint64_t estimatedBytesPerFrame(const ExportSettings& settings);

} // namespace engine::cinematic
