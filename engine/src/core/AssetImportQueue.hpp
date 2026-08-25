#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "core/AssetMetadata.hpp"

namespace engine::core {

// Kronos (Alpha Roadmap Phase 8, "Asset Pipeline" -- "Automated Asset
// Hot-Import Pipeline", Phase 1): a real, small, purpose-built
// background worker pool for asset import -- deliberately NOT a
// generalized engine-wide job system. This codebase's own established
// concurrency idiom (see e.g. RuntimeShell.hpp's own updateCheckThread_/
// std::atomic<bool> updateCheckInProgress_/std::mutex
// updateCheckResultMutex_ triple, repeated 6+ times already for
// single-in-flight background operations like sign-in/update-check/
// catalogue-fetch) is one dedicated std::thread per operation -- real
// and proven, but it doesn't scale to a bulk drag-and-drop of many
// files at once without spawning an unbounded number of threads. This
// class is the one, real, new piece of infrastructure that gap
// justifies: a small, FIXED-size worker pool draining a real queue,
// using the exact same atomic/mutex-guarded-result shape, just shared
// across a bounded pool instead of one thread per call site.
//
// Each worker calls the already-real extractAssetMetadata()
// (AssetMetadata.hpp) -- this is the "hot-import" seam. That function
// already dispatches per real, existing kind (Mesh/.obj, Texture/PNG/
// JPEG via stb_image, Audio via miniaudio); adding a new mesh format
// (glTF, FBX, ...) means extending detectAssetKind()/
// extractAssetMetadata() themselves, not this queue -- every kind they
// already recognize, and every kind added later, runs through this
// same background path for free, with zero further changes here.
class AssetImportQueue {
public:
    // `workerCount` is real, fixed, and chosen once at construction --
    // not auto-scaled per submission (see this class's own comment on
    // why an unbounded thread-per-file approach is exactly what this
    // class exists to avoid). Defaults to a small, real, conservative
    // count rather than std::thread::hardware_concurrency() -- asset
    // import competes with Studio's own render/main thread and Jolt's
    // own physics worker pool; a small, fixed pool is deliberately
    // modest, not tuned for maximum throughput.
    explicit AssetImportQueue(size_t workerCount = 2);
    ~AssetImportQueue();

    AssetImportQueue(const AssetImportQueue&) = delete;
    AssetImportQueue& operator=(const AssetImportQueue&) = delete;

    // Real, non-blocking -- returns immediately; a worker thread picks
    // `path` up and runs the real (potentially slow) extractAssetMetadata()
    // probe on it off the calling thread. Real de-dup: a path already
    // pending or currently running is not queued a second time --
    // re-submitting mid-flight is a real, honest no-op, not a second
    // concurrent probe of the same file.
    void submit(const std::string& path);

    struct Result {
        std::string path;
        AssetMetadata metadata; // the real extractAssetMetadata() result -- check .succeeded
    };

    // Real, non-blocking snapshot, meant to be called once per frame
    // from the UI thread -- returns every job that finished since the
    // last poll() call and clears them from internal tracking. A caller
    // that doesn't poll for a while doesn't lose results; they arrive
    // batched on the next call.
    [[nodiscard]] std::vector<Result> poll();

    // Pending + currently-executing jobs -- real, live count a caller
    // can show as "3 assets importing..." rather than a fire-and-forget
    // submit() with no feedback.
    [[nodiscard]] size_t pendingCount() const;

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::string> pending_;
    // Real, explicit in-flight tracking (queued AND currently-executing
    // paths) -- what submit()'s own de-dup check reads, so a path
    // mid-probe on a worker (already popped off `pending_`) still
    // correctly blocks a second submit() of the same path.
    std::vector<std::string> inFlight_;
    std::vector<Result> completed_;
    bool stopping_ = false;
};

} // namespace engine::core
