#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>

namespace engine::core {

// Sentinel for "unknown/unreadable write time" (file missing, permission
// error). Deliberately NOT -1: std::filesystem::file_time_type's clock
// epoch is unspecified pre-C++20 clock_cast (see fileWriteTimeSeconds()'s
// comment), and on this platform's libstdc++ it is *not* the Unix epoch
// -- a perfectly ordinary, just-written file's time_since_epoch() comes
// back as a large *negative* number of seconds. A plain "< 0 means
// error" check (this code's first version) therefore misfired on every
// real file, permanently missing the cache on every lookup. INT64_MIN is
// as far as possible from any value a real clock reading could plausibly
// produce.
inline constexpr int64_t kUnknownWriteTime = std::numeric_limits<int64_t>::min();

// Real std::filesystem::last_write_time() read, converted to a plain
// integer via its clock's time_since_epoch() (not a real Unix timestamp,
// see kUnknownWriteTime's comment on why -- this is only meaningful as a
// *relative/comparable* value between two calls on the same file, never
// displayed to a user as a date) so cache entries don't need to store a
// filesystem-library-specific type. Returns kUnknownWriteTime on any
// error (file missing, permissions) -- callers must treat that as
// "always reload", never as a value to trust.
[[nodiscard]] int64_t fileWriteTimeSeconds(const std::string& path);

// The cache's pure decision logic, factored out so it's unit-testable
// without touching a real file: given what a cache entry remembers about
// a file's write time and what the file's write time is *right now*,
// should the caller treat the cached value as stale? Also true (reload)
// whenever either time is kUnknownWriteTime -- an unreadable "current"
// state is never treated as "unchanged, keep the cache".
[[nodiscard]] bool assetNeedsReload(int64_t cachedWriteTimeSeconds, int64_t currentWriteTimeSeconds);

// Generic mtime-keyed cache: `Handle` is whatever a caller's loader
// returns (core::TextureLibrary/MeshLibrary handles are the two concrete
// uses -- see studio/plugins/TexturePreviewPlugin.cpp and
// ModelImporterPlugin.cpp). This is NOT a cache of asset *bytes* -- it's
// a cache of "did I already load this exact file, and is it still the
// same file on disk", so re-entering (or a hot-reload re-visiting) the
// same path in Studio doesn't redundantly re-upload the same texture/
// mesh to the GPU. A clean miss (never seen this path, or the file
// changed since) always means "go load it yourself and call put()" --
// this class never loads anything on its own, matching MeshLibrary/
// TextureLibrary's own "plain registry, not a general asset manager"
// shape.
template <typename Handle>
class AssetCache {
public:
    [[nodiscard]] bool tryGet(const std::string& path, Handle& outHandle) const {
        auto it = entries_.find(path);
        if (it == entries_.end()) return false;
        int64_t currentWriteTime = fileWriteTimeSeconds(path);
        if (assetNeedsReload(it->second.writeTimeSeconds, currentWriteTime)) return false;
        outHandle = it->second.handle;
        return true;
    }

    void put(const std::string& path, Handle handle) {
        entries_[path] = Entry{handle, fileWriteTimeSeconds(path)};
    }

    void clear() { entries_.clear(); }

    [[nodiscard]] size_t size() const { return entries_.size(); }

private:
    struct Entry {
        Handle handle;
        int64_t writeTimeSeconds = kUnknownWriteTime;
    };
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace engine::core
