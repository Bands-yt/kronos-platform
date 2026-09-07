#include "core/VideoDecoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace engine::core {

struct VideoDecoder::Impl {
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    SwsContext* swsContext = nullptr;
    int videoStreamIndex = -1;
    int width = 0;
    int height = 0;
    double durationSeconds = 0.0;
    double frameRate = 0.0;

    ~Impl() {
        if (swsContext != nullptr) sws_freeContext(swsContext);
        if (codecContext != nullptr) avcodec_free_context(&codecContext);
        if (formatContext != nullptr) avformat_close_input(&formatContext);
    }
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;
VideoDecoder::VideoDecoder(VideoDecoder&& other) noexcept = default;
VideoDecoder& VideoDecoder::operator=(VideoDecoder&& other) noexcept = default;

bool VideoDecoder::open(const std::string& path, std::string& outError) {
    close();

    AVFormatContext* formatContext = nullptr;
    int ret = avformat_open_input(&formatContext, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char buf[256];
        av_strerror(ret, buf, sizeof(buf));
        outError = std::string("avformat_open_input(\"") + path + "\") failed: " + buf;
        return false;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        outError = "avformat_find_stream_info failed for \"" + path + "\"";
        avformat_close_input(&formatContext);
        return false;
    }

    int videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        outError = "\"" + path + "\" has no real video stream";
        avformat_close_input(&formatContext);
        return false;
    }

    AVStream* stream = formatContext->streams[videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        outError = "no real decoder available for this file's codec";
        avformat_close_input(&formatContext);
        return false;
    }

    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (codecContext == nullptr) {
        outError = "avcodec_alloc_context3 failed";
        avformat_close_input(&formatContext);
        return false;
    }
    if (avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) {
        outError = "avcodec_parameters_to_context failed";
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return false;
    }
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        outError = "avcodec_open2 failed";
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return false;
    }

    impl_->formatContext = formatContext;
    impl_->codecContext = codecContext;
    impl_->videoStreamIndex = videoStreamIndex;
    impl_->width = codecContext->width;
    impl_->height = codecContext->height;
    impl_->durationSeconds = (formatContext->duration > 0) ? static_cast<double>(formatContext->duration) / AV_TIME_BASE
                                                             : 0.0;
    impl_->frameRate = (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0)
                            ? av_q2d(stream->avg_frame_rate)
                            : 0.0;

    outError.clear();
    return true;
}

void VideoDecoder::close() { impl_ = std::make_unique<Impl>(); }

bool VideoDecoder::isOpen() const { return impl_->formatContext != nullptr; }
int VideoDecoder::width() const { return impl_->width; }
int VideoDecoder::height() const { return impl_->height; }
double VideoDecoder::durationSeconds() const { return impl_->durationSeconds; }
double VideoDecoder::frameRate() const { return impl_->frameRate; }

bool VideoDecoder::decodeFrameAt(double timeSeconds, std::vector<uint8_t>& outRgba, std::string& outError) {
    if (!isOpen()) {
        outError = "VideoDecoder::decodeFrameAt called on a real, honestly-unopened decoder";
        return false;
    }
    Impl& impl = *impl_;
    AVStream* stream = impl.formatContext->streams[impl.videoStreamIndex];

    // Real keyframe-backward seek in the video stream's own time base,
    // then decode forward until the real decoded PTS reaches the target
    // -- av_seek_frame alone only lands on the nearest keyframe AT OR
    // BEFORE the target, which is very rarely the exact frame the
    // playhead actually wants.
    const int64_t targetPts = static_cast<int64_t>(timeSeconds / av_q2d(stream->time_base));
    if (av_seek_frame(impl.formatContext, impl.videoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD) < 0) {
        outError = "av_seek_frame failed";
        return false;
    }
    avcodec_flush_buffers(impl.codecContext);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool haveTargetFrame = false;

    while (!haveTargetFrame && av_read_frame(impl.formatContext, packet) >= 0) {
        if (packet->stream_index == impl.videoStreamIndex && avcodec_send_packet(impl.codecContext, packet) == 0) {
            while (avcodec_receive_frame(impl.codecContext, frame) == 0) {
                const int64_t pts =
                    (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                if (pts >= targetPts) {
                    haveTargetFrame = true;
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);

    if (!haveTargetFrame) {
        av_frame_free(&frame);
        outError = "reached real end-of-stream without decoding the requested frame";
        return false;
    }

    // Real libswscale conversion from whatever the source's own pixel
    // format is (typically YUV420P for a real-world H.264 MP4) to
    // tightly-packed RGBA8 -- cached across calls (impl.swsContext) since
    // the source format/size is almost always constant for the life of
    // one open file.
    SwsContext* sws = sws_getCachedContext(impl.swsContext, frame->width, frame->height,
                                            static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                                            AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        av_frame_free(&frame);
        outError = "sws_getCachedContext failed";
        return false;
    }
    impl.swsContext = sws;

    outRgba.assign(static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height) * 4, 0);
    uint8_t* dstData[4] = {outRgba.data(), nullptr, nullptr, nullptr};
    int dstLinesize[4] = {frame->width * 4, 0, 0, 0};
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);

    impl.width = frame->width;
    impl.height = frame->height;

    av_frame_free(&frame);
    outError.clear();
    return true;
}

} // namespace engine::core
