#include "Downloader.hpp"

#include <cstdint>
#include <cstdio>

#include <curl/curl.h>

namespace kronos_installer {

namespace {
size_t writeToFileCallback(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* file = static_cast<std::FILE*>(userdata);
    return std::fwrite(data, size, nmemb, file);
}

struct ProgressContext {
    const std::function<void(uint64_t, uint64_t)>* callback;
};

int progressCallback(void* clientp, curl_off_t downloadTotal, curl_off_t downloadNow, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgressContext*>(clientp);
    if (ctx->callback != nullptr && *ctx->callback) {
        (*ctx->callback)(downloadNow < 0 ? 0 : static_cast<uint64_t>(downloadNow),
                          downloadTotal < 0 ? 0 : static_cast<uint64_t>(downloadTotal));
    }
    return 0; // real, non-zero here would abort the real transfer -- never wanted from a pure progress observer
}
} // namespace

DownloadResult downloadFile(const std::string& url, const std::string& destinationPath,
                             const std::function<void(uint64_t, uint64_t)>& onProgress) {
    DownloadResult result;

    std::FILE* file = std::fopen(destinationPath.c_str(), "wb");
    if (file == nullptr) {
        result.error = "could not open \"" + destinationPath + "\" for writing";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        std::fclose(file);
        result.error = "curl_easy_init() failed";
        return result;
    }

    ProgressContext ctx{&onProgress};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kronos-bootstrap-installer");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // Real, no fixed CURLOPT_TIMEOUT here -- a real multi-hundred-MB
    // download over a real slow connection could legitimately take
    // several minutes; CURLOPT_LOW_SPEED_* below is the real, correct
    // way to bound this (abort only if the transfer genuinely stalls,
    // not just because it's large).
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L); // bytes/sec
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);    // real, honest stall detection window (seconds)
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

    CURLcode code = curl_easy_perform(curl);
    long httpStatus = 0;
    if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_easy_cleanup(curl);
    std::fclose(file);

    if (code != CURLE_OK) {
        result.error = std::string("download failed: ") + curl_easy_strerror(code);
        return result;
    }
    if (httpStatus != 200) {
        result.error = "download failed: HTTP " + std::to_string(httpStatus);
        return result;
    }

    result.success = true;
    return result;
}

} // namespace kronos_installer
