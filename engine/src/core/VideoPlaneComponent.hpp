#pragma once

#include <cstdint>
#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/VideoDecoder.hpp"

namespace engine::core {

class TextureLibrary;

// Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real 3D-over-2D
// compositing): attaches live, timeline-driven video playback to a real
// scene entity (a Transform+Renderable+MeshSource::Quad, spawned by
// studio::plugins::NleTimelinePlugin's own real playback driver -- see
// that plugin's own comment). The entity needs no special renderer
// support at all: it's drawn by the exact same real scene pass as any
// other entity, so a 3D entity sharing the viewport composites with it
// "for free" through ordinary depth-tested rendering, rather than a
// separate compositing pass invented for this feature.
struct VideoPlaneComponent {
    std::string sourcePath; // MediaBin's own MediaAsset::path
    VideoDecoder decoder;   // this plane's own, independent decode/seek state
    uint32_t textureHandle = ~0u; // into TextureLibrary; ~0u until the first real frame decodes
    // Real dedup -- avoids re-decoding/re-uploading a source time this
    // plane's texture already shows (e.g. the playhead is paused).
    double lastDecodedSourceSeconds = -1.0;
};

// Real decode-and-upload step. Opens `plane.decoder` from
// `plane.sourcePath` on first call if not already open. Skips real work
// (returns true immediately) when `sourceTimeSeconds` is within
// kDedupEpsilonSeconds of the last real frame this plane already
// uploaded. Returns false (with `outError` set, `plane.textureHandle`
// left at its previous, still-valid value) on a real open/decode/GPU
// failure -- a stale frame beats a torn or missing one.
[[nodiscard]] bool stepVideoPlane(VideoPlaneComponent& plane, double sourceTimeSeconds, TextureLibrary& textureLibrary,
                                   VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                   std::string& outError);

} // namespace engine::core
