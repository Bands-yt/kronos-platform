#include "trailer/FrameEncoding.hpp"

#include <stb_image_write.h>

// The one translation unit that compiles tinyexr's implementation --
// same real "one real TU, every other file just gets declarations"
// convention core/VmaImpl.cpp already established for VMA (see that
// file's own header comment). No other file in this codebase includes
// tinyexr.h, so there is no ODR risk from defining this here.
//
// stb_image_write's own implementation already lives in
// core/TextureBaker.cpp's translation unit (STB_IMAGE_WRITE_IMPLEMENTATION),
// which is why this file does not define it again -- both TextureBaker.cpp
// and this file are compiled into the same engine_core static library
// (see src/CMakeLists.txt), so stbi_write_png() resolves at link time
// without a second definition.
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include "core/Logger.hpp"

namespace engine::trailer {

bool writePngRgba8(const uint8_t* pixels, uint32_t width, uint32_t height, bool swapRedBlue, const std::string& path) {
    if (pixels == nullptr || width == 0 || height == 0) {
        core::logError("FrameEncoding", "writePngRgba8: empty buffer or zero extent.");
        return false;
    }

    // stb_image_write has no "swap channels" option, so the swap is done
    // here into a fresh buffer -- same real R/B swap ThumbnailCapture.cpp
    // does per-texel for its own PPM write, just batched instead of
    // interleaved with the file write.
    const size_t texelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> rgba(texelCount * 4);
    for (size_t i = 0; i < texelCount; ++i) {
        const uint8_t* src = pixels + i * 4;
        uint8_t* dst = rgba.data() + i * 4;
        dst[0] = src[swapRedBlue ? 2 : 0];
        dst[1] = src[1];
        dst[2] = src[swapRedBlue ? 0 : 2];
        dst[3] = src[3];
    }

    const int ok = stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                                   static_cast<int>(width) * 4);
    if (ok == 0) {
        core::logError("FrameEncoding", "writePngRgba8: stbi_write_png failed for \"%s\".", path.c_str());
        return false;
    }
    return true;
}

bool writeExrDepth(const float* depth, uint32_t width, uint32_t height, const std::string& path) {
    if (depth == nullptr || width == 0 || height == 0) {
        core::logError("FrameEncoding", "writeExrDepth: empty buffer or zero extent.");
        return false;
    }

    const char* err = nullptr;
    const int ret = SaveEXR(depth, static_cast<int>(width), static_cast<int>(height), /*components=*/1,
                             /*save_as_fp16=*/0, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        core::logError("FrameEncoding", "writeExrDepth: SaveEXR failed for \"%s\": %s", path.c_str(),
                        err != nullptr ? err : "unknown error");
        if (err != nullptr) FreeEXRErrorMessage(err);
        return false;
    }
    return true;
}

std::vector<uint8_t> averageRgba8SubFrames(const std::vector<std::vector<uint8_t>>& subFrames) {
    if (subFrames.empty()) return {};
    if (subFrames.size() == 1) return subFrames.front();

    const size_t byteCount = subFrames.front().size();
    std::vector<uint32_t> accum(byteCount, 0);
    for (const std::vector<uint8_t>& frame : subFrames) {
        if (frame.size() != byteCount) continue; // see this function's own header comment
        for (size_t i = 0; i < byteCount; ++i) accum[i] += frame[i];
    }

    const uint32_t count = static_cast<uint32_t>(subFrames.size());
    std::vector<uint8_t> result(byteCount);
    for (size_t i = 0; i < byteCount; ++i) {
        result[i] = static_cast<uint8_t>((accum[i] + count / 2) / count);
    }
    return result;
}

std::vector<float> averageDepthSubFrames(const std::vector<std::vector<float>>& subFrames) {
    if (subFrames.empty()) return {};
    if (subFrames.size() == 1) return subFrames.front();

    const size_t valueCount = subFrames.front().size();
    std::vector<float> accum(valueCount, 0.0f);
    for (const std::vector<float>& frame : subFrames) {
        if (frame.size() != valueCount) continue;
        for (size_t i = 0; i < valueCount; ++i) accum[i] += frame[i];
    }

    const float count = static_cast<float>(subFrames.size());
    std::vector<float> result(valueCount);
    for (size_t i = 0; i < valueCount; ++i) result[i] = accum[i] / count;
    return result;
}

} // namespace engine::trailer
