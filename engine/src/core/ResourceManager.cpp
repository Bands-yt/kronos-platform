#include "core/ResourceManager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine::core {

namespace {

constexpr int64_t kMissingFile = INT64_MIN;

int64_t fileStamp(const std::string& path) {
    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) return kMissingFile;
    const auto size = std::filesystem::file_size(path, ec);
    const int64_t nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(writeTime.time_since_epoch()).count();
    return nanos ^ (static_cast<int64_t>(ec ? 0 : size) * 0x9E3779B97F4A7C15ll);
}

std::string lookupKey(ResourceKind kind, const std::string& normalizedPath) {
    return std::to_string(static_cast<int>(kind)) + ':' + normalizedPath;
}

bool kindFromKeyword(const std::string& keyword, ResourceKind& out) {
    if (keyword == "mesh") out = ResourceKind::Mesh;
    else if (keyword == "texture") out = ResourceKind::Texture;
    else if (keyword == "data-texture") out = ResourceKind::DataTexture;
    else if (keyword == "audio") out = ResourceKind::Audio;
    else if (keyword == "scene") out = ResourceKind::Scene;
    else return false;
    return true;
}

const char* keywordForKind(ResourceKind kind) {
    switch (kind) {
        case ResourceKind::Mesh: return "mesh";
        case ResourceKind::Texture: return "texture";
        case ResourceKind::DataTexture: return "data-texture";
        case ResourceKind::Audio: return "audio";
        case ResourceKind::Scene: return "scene";
    }
    return "mesh";
}

std::string resolveRelative(const std::string& baseDirectory, const std::string& path) {
    std::filesystem::path candidate(path);
    if (candidate.is_absolute() || baseDirectory.empty()) return ResourceManager::normalizePath(path);
    return ResourceManager::normalizePath((std::filesystem::path(baseDirectory) / candidate).string());
}

std::string relativeTo(const std::string& baseDirectory, const std::string& path) {
    if (baseDirectory.empty()) return path;
    std::error_code ec;
    const auto relative = std::filesystem::relative(path, baseDirectory, ec);
    if (ec || relative.empty()) return path;
    return relative.generic_string();
}

std::string readToken(std::istringstream& line) {
    line >> std::ws;
    if (line.peek() == '"') {
        line.get();
        std::string token;
        std::getline(line, token, '"');
        return token;
    }
    std::string token;
    line >> token;
    return token;
}

std::string quoted(const std::string& path) {
    return path.find(' ') == std::string::npos ? path : '"' + path + '"';
}

} // namespace

const char* resourceKindName(ResourceKind kind) {
    switch (kind) {
        case ResourceKind::Mesh: return "Mesh";
        case ResourceKind::Texture: return "Texture";
        case ResourceKind::DataTexture: return "Data texture";
        case ResourceKind::Audio: return "Audio";
        case ResourceKind::Scene: return "Scene";
    }
    return "?";
}

const char* resourceStateName(ResourceState state) {
    switch (state) {
        case ResourceState::Loading: return "Loading";
        case ResourceState::Ready: return "Ready";
        case ResourceState::Failed: return "Failed";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// ResourceHandle

ResourceHandle::ResourceHandle(ResourceManager* manager, uint32_t slot, uint32_t generation)
    : manager_(manager), slot_(slot), generation_(generation) {
    if (manager_ != nullptr) manager_->addRef(slot_, generation_);
}

ResourceHandle::ResourceHandle(const ResourceHandle& other)
    : manager_(other.manager_), slot_(other.slot_), generation_(other.generation_) {
    if (manager_ != nullptr) manager_->addRef(slot_, generation_);
}

ResourceHandle::ResourceHandle(ResourceHandle&& other) noexcept
    : manager_(other.manager_), slot_(other.slot_), generation_(other.generation_) {
    other.manager_ = nullptr;
}

ResourceHandle& ResourceHandle::operator=(const ResourceHandle& other) {
    if (this == &other) return *this;
    ResourceHandle copy(other);
    *this = std::move(copy);
    return *this;
}

ResourceHandle& ResourceHandle::operator=(ResourceHandle&& other) noexcept {
    if (this == &other) return *this;
    reset();
    manager_ = other.manager_;
    slot_ = other.slot_;
    generation_ = other.generation_;
    other.manager_ = nullptr;
    return *this;
}

ResourceHandle::~ResourceHandle() { reset(); }

void ResourceHandle::reset() {
    if (manager_ != nullptr) manager_->releaseRef(slot_, generation_);
    manager_ = nullptr;
}

bool ResourceHandle::valid() const { return manager_ != nullptr && manager_->record(slot_, generation_) != nullptr; }

uint32_t ResourceHandle::get() const {
    const auto* entry = manager_ ? manager_->record(slot_, generation_) : nullptr;
    return entry ? entry->handle : kNoResourceHandle;
}

ResourceState ResourceHandle::state() const {
    const auto* entry = manager_ ? manager_->record(slot_, generation_) : nullptr;
    return entry ? entry->state : ResourceState::Failed;
}

ResourceKind ResourceHandle::kind() const {
    const auto* entry = manager_ ? manager_->record(slot_, generation_) : nullptr;
    return entry ? entry->kind : ResourceKind::Mesh;
}

const std::string& ResourceHandle::path() const {
    static const std::string empty;
    const auto* entry = manager_ ? manager_->record(slot_, generation_) : nullptr;
    return entry ? entry->path : empty;
}

const std::string& ResourceHandle::error() const {
    static const std::string invalid = "invalid resource handle";
    const auto* entry = manager_ ? manager_->record(slot_, generation_) : nullptr;
    return entry ? entry->error : invalid;
}

// ---------------------------------------------------------------------------
// Bundles

bool parseBundleManifest(const std::string& text, const std::string& baseDirectory, BundleManifest& out,
                         std::string& error) {
    out = BundleManifest{};
    std::istringstream input(text);
    std::string line;
    int lineNumber = 0;
    bool sawHeader = false;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream fields(line);
        std::string keyword;
        fields >> keyword;
        if (!sawHeader) {
            int version = 0;
            fields >> version;
            if (keyword != "kronos-bundle" || version != 1) {
                error = "not a kronos-bundle 1 manifest";
                return false;
            }
            sawHeader = true;
            continue;
        }
        const auto fail = [&](const std::string& message) {
            error = "line " + std::to_string(lineNumber) + ": " + message;
            return false;
        };
        if (keyword == "requires") {
            const std::string path = readToken(fields);
            if (path.empty()) return fail("requires needs a manifest path");
            out.requires_.push_back(resolveRelative(baseDirectory, path));
            continue;
        }
        if (keyword == "depends") {
            const std::string dependent = readToken(fields);
            const std::string dependency = readToken(fields);
            if (dependent.empty() || dependency.empty()) return fail("depends needs two paths");
            out.dependencies.emplace_back(resolveRelative(baseDirectory, dependent),
                                          resolveRelative(baseDirectory, dependency));
            continue;
        }
        ResourceKind kind{};
        if (!kindFromKeyword(keyword, kind)) return fail("unknown entry \"" + keyword + "\"");
        const std::string path = readToken(fields);
        if (path.empty()) return fail(keyword + " needs a path");
        out.resources.push_back({kind, resolveRelative(baseDirectory, path)});
    }
    if (!sawHeader) {
        error = "empty manifest";
        return false;
    }
    for (const auto& [dependent, dependency] : out.dependencies) {
        const auto listed = [&](const std::string& path) {
            return std::any_of(out.resources.begin(), out.resources.end(),
                               [&](const BundleManifest::Entry& entry) { return entry.path == path; });
        };
        if (!listed(dependent) || !listed(dependency)) {
            error = "depends names a file the bundle does not list: " + (listed(dependent) ? dependency : dependent);
            return false;
        }
    }
    return true;
}

std::string serializeBundleManifest(const BundleManifest& manifest, const std::string& baseDirectory) {
    std::ostringstream out;
    out << "kronos-bundle 1\n";
    for (const std::string& path : manifest.requires_) out << "requires " << quoted(relativeTo(baseDirectory, path)) << '\n';
    for (const auto& entry : manifest.resources) {
        out << keywordForKind(entry.kind) << ' ' << quoted(relativeTo(baseDirectory, entry.path)) << '\n';
    }
    for (const auto& [dependent, dependency] : manifest.dependencies) {
        out << "depends " << quoted(relativeTo(baseDirectory, dependent)) << ' '
            << quoted(relativeTo(baseDirectory, dependency)) << '\n';
    }
    return out.str();
}

bool BundleHandle::ready() const {
    if (!state_ || !state_->error.empty()) return false;
    for (const auto& resource : state_->resources) {
        if (!resource.ready()) return false;
    }
    return std::all_of(state_->required.begin(), state_->required.end(), [](const BundleHandle& b) { return b.ready(); });
}

bool BundleHandle::failed() const {
    if (!state_) return true;
    if (!state_->error.empty()) return true;
    for (const auto& resource : state_->resources) {
        if (resource.state() == ResourceState::Failed) return true;
    }
    return std::any_of(state_->required.begin(), state_->required.end(), [](const BundleHandle& b) { return b.failed(); });
}

float BundleHandle::progress() const {
    if (!state_) return 0.0f;
    size_t total = 0;
    size_t done = 0;
    std::vector<const State*> pending{state_.get()};
    std::vector<const State*> seen;
    while (!pending.empty()) {
        const State* state = pending.back();
        pending.pop_back();
        if (std::find(seen.begin(), seen.end(), state) != seen.end()) continue;
        seen.push_back(state);
        for (const auto& resource : state->resources) {
            ++total;
            if (resource.state() != ResourceState::Loading) ++done;
        }
        for (const auto& required : state->required) {
            if (required.state_) pending.push_back(required.state_.get());
        }
    }
    return total == 0 ? 1.0f : static_cast<float>(done) / static_cast<float>(total);
}

const std::string& BundleHandle::path() const {
    static const std::string empty;
    return state_ ? state_->path : empty;
}

const std::string& BundleHandle::error() const {
    static const std::string empty;
    return state_ ? state_->error : empty;
}

std::vector<ResourceHandle> BundleHandle::resources() const { return state_ ? state_->resources : std::vector<ResourceHandle>{}; }

ResourceHandle BundleHandle::find(const std::string& path) const {
    if (!state_) return {};
    const std::string normalized = ResourceManager::normalizePath(path);
    for (const auto& resource : state_->resources) {
        if (resource.path() == normalized) return resource;
    }
    for (const auto& required : state_->required) {
        if (ResourceHandle found = required.find(path); found.valid()) return found;
    }
    return {};
}

// ---------------------------------------------------------------------------
// ResourceManager

ResourceManager::ResourceManager(unsigned workerThreads) {
    const unsigned count = std::max(1u, workerThreads);
    for (unsigned i = 0; i < count; ++i) workers_.emplace_back([this] { workerMain(); });
}

ResourceManager::~ResourceManager() {
    {
        std::lock_guard lock(jobMutex_);
        stopping_ = true;
        jobs_.clear();
    }
    jobReady_.notify_all();
    for (auto& worker : workers_) worker.join();
}

std::string ResourceManager::normalizePath(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec) absolute = path;
    return absolute.lexically_normal().generic_string();
}

void ResourceManager::setLoader(ResourceKind kind, ResourceLoader loader) { loaders_[static_cast<int>(kind)] = std::move(loader); }

bool ResourceManager::hasLoader(ResourceKind kind) const {
    const auto& loader = loaders_[static_cast<int>(kind)];
    return loader.reserve && loader.decode && loader.commit;
}

const ResourceManager::Record* ResourceManager::record(uint32_t slot, uint32_t generation) const {
    if (slot >= records_.size()) return nullptr;
    const Record& entry = records_[slot];
    return entry.live && entry.generation == generation ? &entry : nullptr;
}

ResourceManager::Record* ResourceManager::record(uint32_t slot, uint32_t generation) {
    return const_cast<Record*>(static_cast<const ResourceManager*>(this)->record(slot, generation));
}

void ResourceManager::addRef(uint32_t slot, uint32_t generation) {
    if (Record* entry = record(slot, generation)) ++entry->references;
}

void ResourceManager::releaseRef(uint32_t slot, uint32_t generation) {
    Record* entry = record(slot, generation);
    if (entry == nullptr || entry->references == 0) return;
    if (--entry->references == 0) entry->unreferencedSince = updateCounter_;
}

ResourceHandle ResourceManager::acquire(ResourceKind kind, const std::string& path) {
    const std::string normalized = normalizePath(path);
    const std::string key = lookupKey(kind, normalized);
    if (auto it = lookup_.find(key); it != lookup_.end()) {
        ++stats_.cacheHits;
        return ResourceHandle(this, it->second, records_[it->second].generation);
    }

    uint32_t slot = 0;
    if (!freeSlots_.empty()) {
        slot = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        slot = static_cast<uint32_t>(records_.size());
        records_.emplace_back();
    }
    Record& entry = records_[slot];
    const uint32_t generation = entry.generation;
    entry = Record{};
    entry.generation = generation;
    entry.live = true;
    entry.kind = kind;
    entry.path = normalized;
    entry.requestedAt = std::chrono::steady_clock::now();
    lookup_[key] = slot;

    if (!hasLoader(kind)) {
        entry.state = ResourceState::Failed;
        entry.error = std::string("no loader installed for ") + resourceKindName(kind);
    } else if (normalized.empty()) {
        entry.state = ResourceState::Failed;
        entry.error = "empty path";
    } else {
        entry.handle = loaders_[static_cast<int>(kind)].reserve();
        queueDecode(slot);
    }
    return ResourceHandle(this, slot, entry.generation);
}

ResourceHandle ResourceManager::find(ResourceKind kind, const std::string& path) const {
    auto it = lookup_.find(lookupKey(kind, normalizePath(path)));
    if (it == lookup_.end()) return {};
    return ResourceHandle(const_cast<ResourceManager*>(this), it->second, records_[it->second].generation);
}

void ResourceManager::queueDecode(uint32_t slot) {
    Record& entry = records_[slot];
    if (entry.decodeInFlight) {
        entry.reloadAgain = true;
        return;
    }
    entry.decodeInFlight = true;
    entry.requestedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(jobMutex_);
        jobs_.push_back(Job{slot, entry.generation, entry.path, loaders_[static_cast<int>(entry.kind)].decode});
    }
    jobReady_.notify_one();
}

void ResourceManager::workerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(jobMutex_);
            jobReady_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++activeDecodes_;
        }
        Completion completion;
        completion.slot = job.slot;
        completion.generation = job.generation;
        completion.writeTime = fileStamp(job.path);
        try {
            completion.payload = job.decode(job.path, completion.error);
        } catch (const std::exception& e) {
            completion.payload.reset();
            completion.error = std::string("decoder threw: ") + e.what();
        }
        if (!completion.payload && completion.error.empty()) completion.error = "decode failed";
        {
            std::lock_guard lock(completionMutex_);
            completions_.push_back(std::move(completion));
        }
        --activeDecodes_;
    }
}

void ResourceManager::commitCompletion(Completion& completion) {
    Record* entry = record(completion.slot, completion.generation);
    if (entry == nullptr) return;
    entry->decodeInFlight = false;
    const bool reloading = entry->reloading;
    entry->writeTime = completion.writeTime;
    entry->loadMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - entry->requestedAt).count();

    if (completion.payload) {
        std::string error;
        const ResourceLoader& loader = loaders_[static_cast<int>(entry->kind)];
        if (loader.commit(*completion.payload, entry->handle, reloading && entry->state == ResourceState::Ready, error)) {
            const bool wasReady = entry->state == ResourceState::Ready;
            entry->state = ResourceState::Ready;
            entry->error.clear();
            if (wasReady || reloading) {
                ++entry->reloadCount;
                ++stats_.reloadsCompleted;
            } else {
                ++stats_.loadsCompleted;
            }
            entry->reloading = false;
            if (wasReady || reloading) {
                std::vector<uint32_t> visited;
                notifyReloaded(completion.slot, visited);
            }
        } else {
            if (entry->state != ResourceState::Ready) entry->state = ResourceState::Failed;
            entry->error = error.empty() ? "commit failed" : error;
            entry->reloading = false;
        }
    } else {
        // A failed hot reload keeps serving the last good version.
        if (entry->state != ResourceState::Ready) entry->state = ResourceState::Failed;
        entry->error = completion.error;
        entry->reloading = false;
    }

    if (entry->reloadAgain) {
        entry->reloadAgain = false;
        entry->reloading = true;
        queueDecode(completion.slot);
    }
}

void ResourceManager::notifyReloaded(uint32_t slot, std::vector<uint32_t>& visited) {
    if (std::find(visited.begin(), visited.end(), slot) != visited.end()) return;
    visited.push_back(slot);
    const Record& entry = records_[slot];
    const ResourceHandle handle(this, slot, entry.generation);
    std::vector<ReloadListener> listeners;
    listeners.reserve(listeners_.size());
    for (const auto& [id, listener] : listeners_) listeners.push_back(listener);
    for (const auto& listener : listeners) listener(handle);
    const std::vector<uint32_t> dependents = records_[slot].dependents;
    for (uint32_t dependent : dependents) {
        if (records_[dependent].live) notifyReloaded(dependent, visited);
    }
}

void ResourceManager::unloadSlot(uint32_t slot) {
    Record& entry = records_[slot];
    for (const ResourceHandle& dependency : entry.dependencies) {
        if (Record* target = record(dependency.slot_, dependency.generation_)) {
            target->dependents.erase(std::remove(target->dependents.begin(), target->dependents.end(), slot),
                                     target->dependents.end());
        }
    }
    std::vector<ResourceHandle> dependencies = std::move(entry.dependencies);
    entry.dependencies.clear();
    const ResourceLoader& loader = loaders_[static_cast<int>(entry.kind)];
    if (entry.handle != kNoResourceHandle && loader.release) loader.release(entry.handle);
    lookup_.erase(lookupKey(entry.kind, entry.path));
    entry.live = false;
    ++entry.generation;
    entry.handle = kNoResourceHandle;
    entry.path.clear();
    entry.dependents.clear();
    freeSlots_.push_back(slot);
    ++stats_.unloads;
    dependencies.clear(); // may queue further unloads, picked up on a later update
}

bool ResourceManager::reaches(uint32_t from, uint32_t target) const {
    if (from == target) return true;
    std::vector<uint32_t> pending{from};
    std::vector<uint32_t> seen;
    while (!pending.empty()) {
        const uint32_t current = pending.back();
        pending.pop_back();
        if (current == target) return true;
        if (std::find(seen.begin(), seen.end(), current) != seen.end()) continue;
        seen.push_back(current);
        for (const ResourceHandle& dependency : records_[current].dependencies) pending.push_back(dependency.slot_);
    }
    return false;
}

bool ResourceManager::addDependency(const ResourceHandle& dependent, const ResourceHandle& dependency, std::string* error) {
    Record* from = dependent.manager_ == this ? record(dependent.slot_, dependent.generation_) : nullptr;
    Record* to = dependency.manager_ == this ? record(dependency.slot_, dependency.generation_) : nullptr;
    if (from == nullptr || to == nullptr) {
        if (error) *error = "invalid resource handle";
        return false;
    }
    for (const ResourceHandle& existing : from->dependencies) {
        if (existing == dependency) return true;
    }
    if (reaches(dependency.slot_, dependent.slot_)) {
        if (error) *error = "dependency cycle: " + to->path + " already depends on " + from->path;
        return false;
    }
    from->dependencies.push_back(dependency);
    record(dependency.slot_, dependency.generation_)->dependents.push_back(dependent.slot_);
    return true;
}

std::vector<ResourceHandle> ResourceManager::dependentsOf(const ResourceHandle& resource) const {
    std::vector<ResourceHandle> result;
    const Record* entry = resource.manager_ == this ? record(resource.slot_, resource.generation_) : nullptr;
    if (entry == nullptr) return result;
    for (uint32_t slot : entry->dependents) {
        if (records_[slot].live) result.push_back(ResourceHandle(const_cast<ResourceManager*>(this), slot, records_[slot].generation));
    }
    return result;
}

BundleHandle ResourceManager::loadBundle(const std::string& manifestPath) {
    std::vector<std::string> stack;
    return loadBundleRecursive(normalizePath(manifestPath), stack);
}

BundleHandle ResourceManager::loadBundleRecursive(const std::string& manifestPath, std::vector<std::string>& stack) {
    if (auto it = bundles_.find(manifestPath); it != bundles_.end()) {
        if (auto existing = it->second.lock()) {
            BundleHandle handle;
            handle.state_ = existing;
            return handle;
        }
    }
    BundleHandle handle;
    handle.state_ = std::make_shared<BundleHandle::State>();
    handle.state_->path = manifestPath;

    if (std::find(stack.begin(), stack.end(), manifestPath) != stack.end()) {
        handle.state_->error = "bundle requires itself through " + stack.back();
        return handle;
    }
    std::ifstream file(manifestPath, std::ios::binary);
    if (!file) {
        handle.state_->error = "could not open " + manifestPath;
        return handle;
    }
    std::ostringstream text;
    text << file.rdbuf();
    BundleManifest manifest;
    std::string error;
    if (!parseBundleManifest(text.str(), std::filesystem::path(manifestPath).parent_path().string(), manifest, error)) {
        handle.state_->error = manifestPath + ": " + error;
        return handle;
    }

    stack.push_back(manifestPath);
    for (const std::string& required : manifest.requires_) {
        BundleHandle child = loadBundleRecursive(required, stack);
        if (!child.error().empty() && handle.state_->error.empty()) handle.state_->error = child.error();
        handle.state_->required.push_back(std::move(child));
    }
    stack.pop_back();

    BundleHandle body = loadBundle(manifest, manifestPath);
    handle.state_->resources = std::move(body.state_->resources);
    if (handle.state_->error.empty()) handle.state_->error = body.state_->error;
    bundles_[manifestPath] = handle.state_;
    return handle;
}

BundleHandle ResourceManager::loadBundle(const BundleManifest& manifest, const std::string& name) {
    BundleHandle handle;
    handle.state_ = std::make_shared<BundleHandle::State>();
    handle.state_->path = name;
    for (const auto& entry : manifest.resources) handle.state_->resources.push_back(acquire(entry.kind, entry.path));
    for (const auto& [dependent, dependency] : manifest.dependencies) {
        ResourceHandle from = handle.find(dependent);
        ResourceHandle to = handle.find(dependency);
        std::string error;
        if (!addDependency(from, to, &error) && handle.state_->error.empty()) handle.state_->error = error;
    }
    return handle;
}

void ResourceManager::update() {
    ++updateCounter_;

    std::vector<Completion> finished;
    {
        std::lock_guard lock(completionMutex_);
        const size_t take = std::min(commitBudget_, completions_.size());
        finished.reserve(take);
        for (size_t i = 0; i < take; ++i) finished.push_back(std::move(completions_[i]));
        completions_.erase(completions_.begin(), completions_.begin() + static_cast<std::ptrdiff_t>(take));
    }
    for (Completion& completion : finished) commitCompletion(completion);

    for (uint32_t slot = 0; slot < records_.size(); ++slot) {
        Record& entry = records_[slot];
        if (!entry.live || entry.references != 0 || entry.decodeInFlight) continue;
        if (updateCounter_ - entry.unreferencedSince >= unloadDelayUpdates_) unloadSlot(slot);
    }

    if (hotReload_) {
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastPoll_).count() >= pollSeconds_) {
            lastPoll_ = now;
            checkForChanges();
        }
    }
}

size_t ResourceManager::unloadUnreferenced(ResourceKind kind) {
    size_t unloaded = 0;
    for (uint32_t slot = 0; slot < records_.size(); ++slot) {
        const Record& entry = records_[slot];
        if (!entry.live || entry.kind != kind || entry.references != 0) continue;
        unloadSlot(slot);
        ++unloaded;
    }
    return unloaded;
}

bool ResourceManager::waitUntilIdle(double timeoutSeconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);
    for (;;) {
        update();
        bool busy = false;
        for (const Record& entry : records_) {
            if (entry.live && entry.decodeInFlight) busy = true;
        }
        if (!busy) {
            std::lock_guard lock(completionMutex_);
            busy = !completions_.empty();
        }
        if (!busy) return true;
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ResourceManager::setHotReload(bool enabled, double pollSeconds) {
    hotReload_ = enabled;
    pollSeconds_ = std::max(0.0, pollSeconds);
    lastPoll_ = std::chrono::steady_clock::now();
}

size_t ResourceManager::checkForChanges() {
    size_t queued = 0;
    for (uint32_t slot = 0; slot < records_.size(); ++slot) {
        Record& entry = records_[slot];
        if (!entry.live || entry.decodeInFlight || entry.state == ResourceState::Loading) continue;
        const int64_t stamp = fileStamp(entry.path);
        if (stamp == kMissingFile || stamp == entry.writeTime) continue;
        entry.writeTime = stamp;
        entry.reloading = true;
        queueDecode(slot);
        ++queued;
    }
    return queued;
}

bool ResourceManager::reload(const ResourceHandle& resource) {
    Record* entry = resource.manager_ == this ? record(resource.slot_, resource.generation_) : nullptr;
    if (entry == nullptr || !hasLoader(entry->kind)) return false;
    entry->reloading = true;
    queueDecode(resource.slot_);
    return true;
}

int ResourceManager::addReloadListener(ReloadListener listener) {
    const int id = nextListenerId_++;
    listeners_[id] = std::move(listener);
    return id;
}

void ResourceManager::removeReloadListener(int id) { listeners_.erase(id); }

std::vector<ResourceInfo> ResourceManager::snapshot() const {
    std::vector<ResourceInfo> result;
    for (uint32_t slot = 0; slot < records_.size(); ++slot) {
        const Record& entry = records_[slot];
        if (!entry.live) continue;
        ResourceInfo info;
        info.id = slot;
        info.kind = entry.kind;
        info.path = entry.path;
        info.state = entry.state;
        info.handle = entry.handle;
        info.references = entry.references;
        info.reloadCount = entry.reloadCount;
        info.reloading = entry.reloading;
        info.error = entry.error;
        info.loadMilliseconds = entry.loadMilliseconds;
        for (const ResourceHandle& dependency : entry.dependencies) info.dependencies.push_back(dependency.slot_);
        result.push_back(std::move(info));
    }
    return result;
}

ResourceStats ResourceManager::stats() const {
    ResourceStats result = stats_;
    for (const Record& entry : records_) {
        if (!entry.live) continue;
        if (entry.state == ResourceState::Ready) ++result.resident;
        if (entry.state == ResourceState::Loading) ++result.loading;
        if (entry.state == ResourceState::Failed) ++result.failed;
        if (entry.references == 0) ++result.pendingUnload;
    }
    std::lock_guard lock(jobMutex_);
    result.queuedDecodes = jobs_.size();
    return result;
}

void ResourceManager::shutdown() {
    {
        std::lock_guard lock(jobMutex_);
        jobs_.clear();
    }
    while (activeDecodes_.load() != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    {
        std::lock_guard lock(completionMutex_);
        completions_.clear();
    }
    for (Record& entry : records_) entry.dependencies.clear();
    for (uint32_t slot = 0; slot < records_.size(); ++slot) {
        Record& entry = records_[slot];
        if (!entry.live) continue;
        const ResourceLoader& loader = loaders_[static_cast<int>(entry.kind)];
        if (entry.handle != kNoResourceHandle && loader.release) loader.release(entry.handle);
        entry = Record{.generation = entry.generation + 1};
        freeSlots_.push_back(slot);
    }
    lookup_.clear();
    bundles_.clear();
}

} // namespace engine::core
