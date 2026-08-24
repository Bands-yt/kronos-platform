#include "publishing/AssetStreamingClient.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <curl/curl.h>

#include "publishing/PackageArchive.hpp"

namespace engine::publishing {

namespace {

size_t writeToFile(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(data, static_cast<std::streamsize>(size * nmemb));
    return out->good() ? size * nmemb : 0; // returning less than the real byte count aborts the transfer
}

// Real, direct, streamed-to-disk GET -- deliberately not routed through
// KronosApi::request(): a package download URL is a real presigned S3
// (or direct CDN) URL, a different origin entirely from the Kronos
// backend's own baseUrl_, and needs no bearer auth at all (a presigned
// URL already carries its own real, scoped authorization in its query
// string).
bool downloadUrlToFile(const std::string& url, const std::string& destinationPath) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return false;

    std::ofstream out(destinationPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kronos-asset-streaming-client");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // See KronosApi.cpp's own comment on why this is safe to set
    // unconditionally on every platform (curl silently ignores it for
    // backends, like Schannel, that don't need it).
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    // A real package can legitimately be a few hundred MB -- a flat
    // total timeout would be wrong for a real, if slow, connection. A
    // low-speed cutoff catches the failure mode that actually matters
    // (a stalled/dead connection), not a merely-slow one.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L); // bytes/sec
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);    // seconds

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    out.close();

    bool ok = code == CURLE_OK && status >= 200 && status < 300;
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(destinationPath, ec); // no partial/failed download left behind to be mistaken for real content
    }
    return ok;
}

} // namespace

PackageFetchResult fetchGamePackage(core::KronosApi& api, const std::string& slug,
                                     const std::string& cacheRootDirectory) {
    PackageFetchResult result;

    core::PackageInfo info = api.fetchGamePackageInfo(slug);
    if (!info.success) {
        result.error = info.error.empty() ? "This game has no real package to fetch." : info.error;
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(cacheRootDirectory, ec);
    std::string archivePath = cacheRootDirectory + "/" + info.sha256 + ".kronos";
    std::string extractedDirectory = cacheRootDirectory + "/extracted/" + info.sha256;

    // Content-addressed skip-if-present -- but re-verified, not just
    // trusted on sight: a file sitting at this hash's own name is a
    // strong signal, not a guarantee (a prior run could have crashed
    // mid-write, or the file could be corrupt on disk). A cheap re-hash
    // of an already-local file is far cheaper than a real network
    // re-download, so this is not wasted work.
    bool haveValidCachedArchive =
        std::filesystem::exists(archivePath) && archiveSha256Hex(archivePath) == info.sha256;

    if (haveValidCachedArchive) {
        result.wasCached = true;
    } else {
        std::string tempPath = archivePath + ".downloading";
        if (!downloadUrlToFile(info.downloadUrl, tempPath)) {
            result.error = "Could not download the package from Kronos.";
            return result;
        }
        if (archiveSha256Hex(tempPath) != info.sha256) {
            std::filesystem::remove(tempPath, ec);
            result.error = "The downloaded package's real content did not match its expected hash -- refusing to use it.";
            return result;
        }
        // Atomic on every real platform this engine ships on (same
        // filesystem, POSIX rename()/Windows MoveFileEx) -- a
        // concurrent second fetch of the same hash never observes a
        // half-written archive at the final path.
        std::filesystem::rename(tempPath, archivePath, ec);
        if (ec) {
            result.error = "Could not finalize the downloaded package on disk.";
            return result;
        }
    }

    // Same content-addressed idempotent-skip for extraction: a
    // directory already sitting at this hash's own extracted/ path,
    // with a real package.json inside it, was already extracted
    // correctly -- extracting it again would be real, wasted disk I/O
    // for identical output.
    bool haveValidExtraction = std::filesystem::exists(extractedDirectory + "/package.json");
    if (!haveValidExtraction) {
        std::filesystem::remove_all(extractedDirectory, ec); // real, honest cleanup of any partial prior extraction
        if (!extractWorldPackageArchive(archivePath, extractedDirectory)) {
            result.error = "The downloaded package archive could not be extracted.";
            return result;
        }
    }

    result.archivePath = archivePath;
    result.extractedDirectory = extractedDirectory;
    result.success = true;
    return result;
}

} // namespace engine::publishing
