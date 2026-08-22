#include "net/HttpWorkerPool.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <mutex>

namespace engine::net {
namespace {

size_t writeToString(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

// libcurl's implicit initialisation (on the first curl_easy_init) is NOT
// thread-safe. Nothing in this codebase called curl_global_init before,
// which was survivable only because every previous caller happened to be
// one thread at a time. A worker pool makes that a real race, so init is
// forced exactly once here, before any worker can construct a handle.
void ensureCurlGlobalInit() {
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

} // namespace

HttpWorkerPool::HttpWorkerPool(size_t workerCount) {
    ensureCurlGlobalInit();
    workerCount = std::max<size_t>(workerCount, 1);
    workers_.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this]() { workerMain(); });
    }
}

HttpWorkerPool::~HttpWorkerPool() { shutdown(); }

void HttpWorkerPool::shutdown() {
    if (!running_.exchange(false)) {
        // Already shut down; still join, because a second shutdown() from
        // the destructor after an explicit one must not leave threads
        // running.
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();
        return;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();

    // Anything still queued resolves as a transport failure rather than
    // being dropped: a caller blocked on one of these futures would
    // otherwise wait forever on a promise nobody will ever set.
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        HttpResponse response;
        response.transportFailed = true;
        response.error = "HTTP worker pool shut down before this request ran";
        Job job = std::move(queue_.front());
        queue_.pop();
        if (job.onComplete) {
            job.onComplete(std::move(response));
        } else if (job.hasPromise) {
            job.promise.set_value(std::move(response));
        }
    }
}

std::future<HttpResponse> HttpWorkerPool::submit(HttpRequest request) {
    std::promise<HttpResponse> promise;
    std::future<HttpResponse> future = promise.get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load() || queue_.size() >= kMaxQueueDepth) {
            HttpResponse response;
            response.transportFailed = true;
            response.error = running_.load() ? "HTTP worker queue is full" : "HTTP worker pool is shut down";
            promise.set_value(std::move(response));
            return future;
        }
        Job job;
        job.request = std::move(request);
        job.promise = std::move(promise);
        job.hasPromise = true;
        queue_.push(std::move(job));
    }
    condition_.notify_one();
    return future;
}


void HttpWorkerPool::submitWithCallback(HttpRequest request, std::function<void(HttpResponse)> onComplete) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_.load() && queue_.size() < kMaxQueueDepth) {
            Job job;
            job.request = std::move(request);
            job.onComplete = std::move(onComplete);
            queue_.push(std::move(job));
            condition_.notify_one();
            return;
        }
    }
    // Rejected -- still invoked, so a caller waiting on a promise the
    // callback fulfils is never left waiting forever.
    HttpResponse response;
    response.transportFailed = true;
    response.error = running_.load() ? "HTTP worker queue is full" : "HTTP worker pool is shut down";
    if (onComplete) onComplete(std::move(response));
}

size_t HttpWorkerPool::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void HttpWorkerPool::workerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return !running_.load() || !queue_.empty(); });
            if (!running_.load() && queue_.empty()) return;
            if (queue_.empty()) continue;
            job = std::move(queue_.front());
            queue_.pop();
        }
        HttpResponse response = perform(job.request);
        ++completed_;
        if (job.onComplete) {
            job.onComplete(std::move(response));
        } else if (job.hasPromise) {
            job.promise.set_value(std::move(response));
        }
    }
}

HttpResponse HttpWorkerPool::perform(const HttpRequest& request) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.transportFailed = true;
        response.error = "curl_easy_init failed";
        return response;
    }

    struct curl_slist* headers = nullptr;
    for (const std::string& header : request.headers) {
        headers = curl_slist_append(headers, header.c_str());
    }
    // curl adds "Expect: 100-continue" for bodies over ~1KB and then waits
    // up to a second for a 100 response that many servers never send. Every
    // moderation request carries a schema and so clears that threshold, so
    // leaving this on would add a stall to every single call.
    headers = curl_slist_append(headers, "Expect:");

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request.timeoutMillis);
    // Bounded separately from the total timeout: a dead host should fail
    // fast rather than consuming the whole budget in connect.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                      static_cast<long>(std::min<long>(request.timeoutMillis, 2000)));
    // Signals are not safe to use from a worker thread; without this
    // libcurl's default DNS timeout implementation uses alarm/longjmp.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (headers != nullptr) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    } else if (request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        }
    }

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        response.transportFailed = true;
        response.error = curl_easy_strerror(code);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    }

    if (headers != nullptr) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

} // namespace engine::net
