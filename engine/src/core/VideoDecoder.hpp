#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::core {

// Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real MP4 video decoding):
// a real libavformat/libavcodec/libswscale decoder, closing the gap
// MediaBin.hpp's own header comment previously documented plainly ("Video
// import isn't supported yet -- this engine has no video decoder
// vendored"). Opaque-pointer'd (see Impl below) so this header stays
// libav-free -- MediaBin.hpp only needs to *hold* a VideoDecoder per
// video asset, not touch libav types directly.
//
// Scope, stated plainly: this is a real seek-and-decode-one-frame API
// for scrubbing/playback in an editor (the NLE timeline calling this
// once per playhead move/tick), not a full realtime playback engine with
// audio/video sync, B-frame reordering guarantees beyond what libavcodec
// itself provides, or hardware decode acceleration. Every frame it
// returns is a real decoded frame from the real file, converted to RGBA8
// via a real libswscale conversion -- not a placeholder/checkerboard.
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;
    VideoDecoder(VideoDecoder&& other) noexcept;
    VideoDecoder& operator=(VideoDecoder&& other) noexcept;

    // Opens `path` and locates its first real video stream. Returns
    // false (with `outError` describing what libav itself rejected --
    // missing file, no video stream, unsupported codec) on failure;
    // `outError` is left empty on success.
    [[nodiscard]] bool open(const std::string& path, std::string& outError);
    void close();
    [[nodiscard]] bool isOpen() const;

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] double frameRate() const;

    // Real seek-and-decode: seeks the underlying stream to the keyframe
    // at-or-before `timeSeconds`, then decodes forward until the decoded
    // frame's own presentation timestamp reaches (or passes) it -- so
    // scrubbing the NLE timeline lands on the actual nearest real frame,
    // not just whatever keyframe libav seeks to. `outRgba` is resized to
    // exactly width()*height()*4 bytes and filled via a real sws_scale()
    // conversion from the source's own pixel format (typically YUV420P
    // for real-world H.264 MP4s) to tightly-packed RGBA8. Returns false
    // (outRgba left untouched) on real EOF/decode failure/an unopened
    // decoder.
    [[nodiscard]] bool decodeFrameAt(double timeSeconds, std::vector<uint8_t>& outRgba, std::string& outError);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::core
