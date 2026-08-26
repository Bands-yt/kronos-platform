#include "core/TextureBaker.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <stb_image.h> // declarations only -- STB_IMAGE_IMPLEMENTATION lives in Texture.cpp's own translation unit

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace engine::core {

TextureBakeResult bakeTextureMips(const std::string& path) {
    TextureBakeResult result;

    int width = 0, height = 0, sourceChannels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &sourceChannels, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        result.error = reason != nullptr ? reason : "stbi_load failed";
        return result;
    }

    std::filesystem::path srcPath(path);
    std::filesystem::path cacheDir = srcPath.parent_path() / ".kronos_mips" / srcPath.stem();
    std::error_code dirEc;
    std::filesystem::create_directories(cacheDir, dirEc);
    if (dirEc) {
        stbi_image_free(pixels);
        result.error = "could not create mip cache directory: " + dirEc.message();
        return result;
    }
    result.cacheDir = cacheDir.string();

    std::vector<unsigned char> current(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(pixels);

    int levelWidth = width;
    int levelHeight = height;
    uint32_t level = 0;
    while (true) {
        std::filesystem::path mipPath = cacheDir / ("mip_" + std::to_string(level) + ".png");
        int written = stbi_write_png(mipPath.string().c_str(), levelWidth, levelHeight, 4, current.data(), levelWidth * 4);
        if (written == 0) {
            result.error = "could not write mip level " + std::to_string(level) + " to " + mipPath.string();
            return result;
        }
        std::error_code sizeEc;
        uintmax_t mipFileSize = std::filesystem::file_size(mipPath, sizeEc);
        if (!sizeEc) result.bakedSizeBytes += mipFileSize;
        ++level;

        if (levelWidth == 1 && levelHeight == 1) break; // real bottom of the chain -- nothing smaller to bake

        int nextWidth = std::max(1, levelWidth / 2);
        int nextHeight = std::max(1, levelHeight / 2);
        std::vector<unsigned char> next(static_cast<size_t>(nextWidth) * static_cast<size_t>(nextHeight) * 4);
        unsigned char* resized = stbir_resize_uint8_linear(current.data(), levelWidth, levelHeight, 0, next.data(),
                                                             nextWidth, nextHeight, 0, STBIR_RGBA);
        if (resized == nullptr) {
            result.error = "mip downsample failed at level " + std::to_string(level);
            return result;
        }
        current = std::move(next);
        levelWidth = nextWidth;
        levelHeight = nextHeight;
    }

    result.mipLevels = level;
    result.succeeded = true;
    return result;
}

} // namespace engine::core
