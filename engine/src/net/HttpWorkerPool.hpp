#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace engine::net {

struct HttpRequest {
    std::string url;
    std::string method = "POST";
    std::string body;
    std::vector<std::string> headers; // "Name: value"
    long timeoutMillis = 4000;
};

struct HttpResponse {
    long status = 0;
    std::string body;
    // Set when the request never produced an HTTP status at all (DNS
    // failure, refused connection, timeout). Distinct from a 4xx/5xx,
    // because a caller's fallback logic needs to tell "the service said
    // no" from "the service was unreachable".
    bool transportFailed = false;
    std::string error;

    [[nodiscard]] bool ok() const { return !transportFailed && status >= 200 && status < 300; }
    // 429, and the 5xx family that real APIs use for shedding load.
    [[nodiscard]] bool rateLimited() const { return status == 429 || status == 503; }
};

// A small pool of threads that perform blocking HTTP requests off the
// calling thread.
//
// Exists because the moderation calls this pool serves sit in two places
// that must never block: the Vulkan frame loop, and the server's network
// tick. A synchronous request on either stalls every player in the
// session for the duration of someone else's DNS lookup.
//
// Deliberately a fixed pool with a bounded queue rather than a thread per
// request (the pattern RuntimeShell uses for its one-off sign-in and
// catalogue fetches). Chat moderation is per-message, so thread-per-
// request would spawn one thread per chat line -- fine at three players,
// catastrophic at three hundred.
class HttpWorkerPool {
public:
    // Rejects new work past this depth rather than growing without bound.
    // A queue that grows forever under a stalled upstream converts a
    // slow dependency into an out-of-memory crash.
    static constexpr size_t kMaxQueueDepth = 512;

    explicit HttpWorkerPool(size_t workerCount = 2);
    ~HttpWorkerPool();

    HttpWorkerPool(const HttpWorkerPool&) = delete;
    HttpWorkerPool& operator=(const HttpWorkerPool&) = delete;

    // Returns a future that resolves when the request completes. A
    // rejected submission (pool shutting down, or queue full) resolves
    // IMMEDIATELY with transportFailed set, so a caller can always wait on
    // the future and never has to special-case a rejected submit.
    [[nodiscard]] std::future<HttpResponse> submit(HttpRequest request);

    // Runs `onComplete` on the worker thread once the request finishes.
    //
    // Needed because a caller that wants to TRANSFORM the response cannot
    // do it with std::async(deferred) -- a deferred future never becomes
    // ready, so a poller checking wait_for() would spin forever. Running
    // the continuation here means the caller's own future is fulfilled by
    // real work on a real thread and genuinely becomes ready.
    // `onComplete` is always invoked exactly once, including when the
    // submission is rejected.
    void submitWithCallback(HttpRequest request, std::function<void(HttpResponse)> onComplete);

    void shutdown();
    [[nodiscard]] size_t pendingCount() const;
    [[nodiscard]] size_t workerCount() const { return workers_.size(); }
    // Total requests that completed, whatever their status. Used by tests
    // and by the moderation client's own diagnostics.
    [[nodiscard]] uint64_t completedCount() const { return completed_.load(); }

private:
    struct Job {
        HttpRequest request;
        std::promise<HttpResponse> promise;
        std::function<void(HttpResponse)> onComplete;
        bool hasPromise = false;
    };

    void workerMain();
    [[nodiscard]] static HttpResponse perform(const HttpRequest& request);

    std::vector<std::thread> workers_;
    std::queue<Job> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> running_{true};
    std::atomic<uint64_t> completed_{0};
};

} // namespace engine::net
