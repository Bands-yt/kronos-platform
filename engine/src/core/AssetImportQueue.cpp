#include "core/AssetImportQueue.hpp"

#include <algorithm>

namespace engine::core {

AssetImportQueue::AssetImportQueue(size_t workerCount) {
    for (size_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

AssetImportQueue::~AssetImportQueue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

void AssetImportQueue::submit(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(inFlight_.begin(), inFlight_.end(), path) != inFlight_.end()) {
        return; // already pending/running -- real, honest no-op, see this method's own header comment
    }
    inFlight_.push_back(path);
    pending_.push(path);
    condition_.notify_one();
}

std::vector<AssetImportQueue::Result> AssetImportQueue::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Result> out = std::move(completed_);
    completed_.clear(); // completed_ is in a real, valid-but-unspecified state after the move above; explicit clear() is honest, not redundant
    return out;
}

size_t AssetImportQueue::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inFlight_.size();
}

void AssetImportQueue::workerLoop() {
    while (true) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            // Real, immediate shutdown -- a worker that wakes up while
            // stopping exits right away rather than draining whatever
            // is still queued (only a job it had *already started*
            // before stopping_ was set gets to finish naturally, since
            // that work happens outside this lock below). A slow
            // Studio-close shouldn't wait for every queued-but-not-yet-
            // started import to actually run.
            if (stopping_) return;
            path = pending_.front();
            pending_.pop();
        }

        // The real, potentially slow work -- runs off the lock, so
        // other workers/submit()/poll() aren't blocked while this one
        // parses a large file.
        AssetMetadata metadata = extractAssetMetadata(path);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.push_back(Result{path, std::move(metadata)});
            inFlight_.erase(std::remove(inFlight_.begin(), inFlight_.end(), path), inFlight_.end());
        }
    }
}

} // namespace engine::core
