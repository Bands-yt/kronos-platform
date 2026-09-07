#include "core/VideoPlaneComponent.hpp"

#include <cmath>
#include <vector>

#include "core/Texture.hpp"

namespace engine::core {

bool stepVideoPlane(VideoPlaneComponent& plane, double sourceTimeSeconds, TextureLibrary& textureLibrary,
                     VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                     std::string& outError) {
    if (!plane.decoder.isOpen() && !plane.decoder.open(plane.sourcePath, outError)) return false;

    // Real dedup: a paused playhead (or a caller ticking faster than the
    // source's own real frame rate warrants) shouldn't re-decode/re-
    // upload a frame this plane's texture already shows.
    constexpr double kDedupEpsilonSeconds = 1.0 / 240.0;
    if (plane.textureHandle != TextureLibrary::kInvalidHandle &&
        std::fabs(sourceTimeSeconds - plane.lastDecodedSourceSeconds) < kDedupEpsilonSeconds) {
        outError.clear();
        return true;
    }

    std::vector<uint8_t> rgba;
    if (!plane.decoder.decodeFrameAt(sourceTimeSeconds, rgba, outError)) return false;

    if (plane.textureHandle == TextureLibrary::kInvalidHandle) {
        Texture texture = Texture::createFromPixels(rgba.data(), plane.decoder.width(), plane.decoder.height(),
                                                      /*srgb=*/true, allocator, device, cmdPool, queue);
        if (!texture.isValid()) {
            outError = "failed to create the real GPU texture for this video plane's first frame";
            return false;
        }
        plane.textureHandle = textureLibrary.registerTexture(std::move(texture));
    } else {
        Texture* existing = textureLibrary.get(plane.textureHandle);
        if (existing == nullptr || !existing->updatePixels(rgba.data(), rgba.size(), allocator, device, cmdPool, queue)) {
            outError = "failed to update the real GPU texture with this video plane's new frame";
            return false;
        }
    }

    plane.lastDecodedSourceSeconds = sourceTimeSeconds;
    outError.clear();
    return true;
}

} // namespace engine::core
