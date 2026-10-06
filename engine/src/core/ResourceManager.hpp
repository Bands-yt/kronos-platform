#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace engine::core {

enum class ResourceKind : uint8_t { Mesh, Texture, DataTexture, Audio, Scene };
inline constexpr int kResourceKindCount = 5;

enum class ResourceState : uint8_t { Loading, Ready, Failed };

[[nodiscard]] const char* resourceKindName(ResourceKind kind);
[[nodiscard]] const char* resourceStateName(ResourceState state);

inline constexpr uint32_t kNoResourceHandle = ~0u;

// Decoded host-side data, produced on a worker thread and consumed by the
// loader's commit on the main thread.
struct ResourcePayload {
    virtual ~ResourcePayload() = default;
};

// How one ResourceKind is loaded. reserve/commit/release run on the
// thread that calls ResourceManager::update(); decode runs on a worker.
//
// reserve() hands out the library handle up front (an empty mesh slot, an
// invalid texture, a silent sound), so a handle is usable the moment it is
// acquired and every later load or hot reload replaces it in place.
struct ResourceLoader {
    std::function<uint32_t()> reserve;
    std::function<std::unique_ptr<ResourcePayload>(const std::string& path, std::string& error)> decode;
    std::function<bool(ResourcePayload& payload, uint32_t handle, bool reloading, std::string& error)> commit;
    std::function<void(uint32_t handle)> release;
};

class ResourceManager;

// Reference-counted handle to one loaded file. Copying adds a reference;
// the resource unloads a few updates after the last handle goes away.
// Handles belong to the thread that runs ResourceManager::update().
class ResourceHandle {
public:
    ResourceHandle() = default;
    ResourceHandle(const ResourceHandle& other);
    ResourceHandle(ResourceHandle&& other) noexcept;
    ResourceHandle& operator=(const ResourceHandle& other);
    ResourceHandle& operator=(ResourceHandle&& other) noexcept;
    ~ResourceHandle();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] uint32_t get() const; // the library handle (MeshLibrary, TextureLibrary, Audio)
    [[nodiscard]] ResourceState state() const;
    [[nodiscard]] bool ready() const { return state() == ResourceState::Ready; }
    [[nodiscard]] ResourceKind kind() const;
    [[nodiscard]] const std::string& path() const;
    [[nodiscard]] const std::string& error() const;
    [[nodiscard]] uint32_t id() const { return slot_; }
    void reset();

    friend bool operator==(const ResourceHandle& a, const ResourceHandle& b) {
        return a.manager_ == b.manager_ && a.slot_ == b.slot_ && a.generation_ == b.generation_;
    }

private:
    friend class ResourceManager;
    ResourceHandle(ResourceManager* manager, uint32_t slot, uint32_t generation);

    ResourceManager* manager_ = nullptr;
    uint32_t slot_ = 0;
    uint32_t generation_ = 0;
};

// A manifest of resources that load and unload together (a level, a
// biome, a character). Text format, paths relative to the manifest:
//
//   kronos-bundle 1
//   requires ../shared/common.kbundle
//   mesh models/tree.glb
//   texture textures/bark.png
//   data-texture textures/bark_normal.png
//   audio sounds/wind.ogg
//   depends models/tree.glb textures/bark.png
struct BundleManifest {
    struct Entry {
        ResourceKind kind = ResourceKind::Mesh;
        std::string path;
    };
    std::vector<std::string> requires_;
    std::vector<Entry> resources;
    std::vector<std::pair<std::string, std::string>> dependencies; // dependent, dependency
};

[[nodiscard]] bool parseBundleManifest(const std::string& text, const std::string& baseDirectory, BundleManifest& out,
                                       std::string& error);
[[nodiscard]] std::string serializeBundleManifest(const BundleManifest& manifest, const std::string& baseDirectory);

class BundleHandle {
public:
    [[nodiscard]] bool valid() const { return state_ != nullptr; }
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool failed() const;
    [[nodiscard]] float progress() const; // 0..1 over every resource including required bundles
    [[nodiscard]] const std::string& path() const;
    [[nodiscard]] const std::string& error() const;
    [[nodiscard]] std::vector<ResourceHandle> resources() const;
    [[nodiscard]] ResourceHandle find(const std::string& path) const;
    void reset() { state_.reset(); }

private:
    friend class ResourceManager;
    struct State {
        std::string path;
        std::string error;
        std::vector<ResourceHandle> resources;
        std::vector<BundleHandle> required;
    };
    std::shared_ptr<State> state_;
};

struct ResourceInfo {
    uint32_t id = 0;
    ResourceKind kind = ResourceKind::Mesh;
    std::string path;
    ResourceState state = ResourceState::Loading;
    uint32_t handle = kNoResourceHandle;
    uint32_t references = 0;
    uint32_t reloadCount = 0;
    bool reloading = false;
    std::string error;
    std::vector<uint32_t> dependencies;
    double loadMilliseconds = 0.0;
};

struct ResourceStats {
    size_t resident = 0;
    size_t loading = 0;
    size_t failed = 0;
    size_t pendingUnload = 0;
    size_t queuedDecodes = 0;
    uint64_t loadsCompleted = 0;
    uint64_t reloadsCompleted = 0;
    uint64_t unloads = 0;
    uint64_t cacheHits = 0;
};

// ECS component: keeps an entity's resources loaded for as long as the
// entity exists.
struct ResourceRefs {
    std::vector<ResourceHandle> handles;
};

class ResourceManager {
public:
    explicit ResourceManager(unsigned workerThreads = 2);
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    void setLoader(ResourceKind kind, ResourceLoader loader);
    [[nodiscard]] bool hasLoader(ResourceKind kind) const;

    // Same file and kind returns the same resource. Loading starts on a
    // worker immediately; get() is already a usable handle.
    [[nodiscard]] ResourceHandle acquire(ResourceKind kind, const std::string& path);
    [[nodiscard]] ResourceHandle find(ResourceKind kind, const std::string& path) const;

    // `dependent` keeps `dependency` alive and is reported as changed
    // whenever `dependency` hot-reloads. Refuses cycles.
    bool addDependency(const ResourceHandle& dependent, const ResourceHandle& dependency, std::string* error = nullptr);
    [[nodiscard]] std::vector<ResourceHandle> dependentsOf(const ResourceHandle& resource) const;

    // Loads a .kbundle and everything it requires. Bundles are shared:
    // loading the same manifest twice returns the same bundle.
    [[nodiscard]] BundleHandle loadBundle(const std::string& manifestPath);
    [[nodiscard]] BundleHandle loadBundle(const BundleManifest& manifest, const std::string& name);

    // Main-thread pump: commits finished decodes (up to the commit
    // budget), unloads resources unreferenced for the unload delay, and
    // polls watched files when hot reload is on.
    void update();
    // Unloads every unreferenced resource of `kind` now, skipping the unload delay.
    size_t unloadUnreferenced(ResourceKind kind);
    // Pumps update() until nothing is loading or the timeout passes.
    bool waitUntilIdle(double timeoutSeconds = 10.0);

    void setCommitBudget(size_t commitsPerUpdate) { commitBudget_ = commitsPerUpdate == 0 ? 1 : commitsPerUpdate; }
    void setUnloadDelay(uint32_t updates) { unloadDelayUpdates_ = updates; }
    void setHotReload(bool enabled, double pollSeconds = 0.5);
    [[nodiscard]] bool hotReloadEnabled() const { return hotReload_; }

    // Re-decodes every resource whose file changed; returns how many.
    size_t checkForChanges();
    bool reload(const ResourceHandle& resource);

    using ReloadListener = std::function<void(const ResourceHandle& resource)>;
    int addReloadListener(ReloadListener listener);
    void removeReloadListener(int id);

    [[nodiscard]] std::vector<ResourceInfo> snapshot() const;
    [[nodiscard]] ResourceStats stats() const;

    // Releases every resource through its loader. Outstanding handles
    // become invalid.
    void shutdown();

    [[nodiscard]] static std::string normalizePath(const std::string& path);

private:
    friend class ResourceHandle;

    struct Record {
        uint32_t generation = 1;
        bool live = false;
        ResourceKind kind = ResourceKind::Mesh;
        std::string path;
        ResourceState state = ResourceState::Loading;
        uint32_t handle = kNoResourceHandle;
        uint32_t references = 0;
        uint64_t unreferencedSince = 0;
        bool reloading = false;
        bool decodeInFlight = false;
        bool reloadAgain = false;
        int64_t writeTime = 0;
        uint32_t reloadCount = 0;
        std::string error;
        std::vector<ResourceHandle> dependencies;
        std::vector<uint32_t> dependents;
        std::chrono::steady_clock::time_point requestedAt;
        double loadMilliseconds = 0.0;
    };

    struct Job {
        uint32_t slot = 0;
        uint32_t generation = 0;
        std::string path;
        std::function<std::unique_ptr<ResourcePayload>(const std::string&, std::string&)> decode;
    };

    struct Completion {
        uint32_t slot = 0;
        uint32_t generation = 0;
        std::unique_ptr<ResourcePayload> payload;
        std::string error;
        int64_t writeTime = 0;
    };

    void addRef(uint32_t slot, uint32_t generation);
    void releaseRef(uint32_t slot, uint32_t generation);
    [[nodiscard]] const Record* record(uint32_t slot, uint32_t generation) const;
    [[nodiscard]] Record* record(uint32_t slot, uint32_t generation);
    void queueDecode(uint32_t slot);
    void commitCompletion(Completion& completion);
    void unloadSlot(uint32_t slot);
    void notifyReloaded(uint32_t slot, std::vector<uint32_t>& visited);
    [[nodiscard]] bool reaches(uint32_t from, uint32_t target) const;
    void workerMain();
    [[nodiscard]] BundleHandle loadBundleRecursive(const std::string& manifestPath, std::vector<std::string>& stack);

    ResourceLoader loaders_[kResourceKindCount];
    std::vector<Record> records_;
    std::vector<uint32_t> freeSlots_;
    std::unordered_map<std::string, uint32_t> lookup_;
    std::unordered_map<std::string, std::weak_ptr<BundleHandle::State>> bundles_;
    std::unordered_map<int, ReloadListener> listeners_;
    int nextListenerId_ = 1;

    uint64_t updateCounter_ = 0;
    size_t commitBudget_ = 8;
    uint32_t unloadDelayUpdates_ = 3;
    bool hotReload_ = false;
    double pollSeconds_ = 0.5;
    std::chrono::steady_clock::time_point lastPoll_{};
    ResourceStats stats_;

    std::vector<std::thread> workers_;
    mutable std::mutex jobMutex_;
    std::condition_variable jobReady_;
    std::deque<Job> jobs_;
    bool stopping_ = false;
    std::atomic<int> activeDecodes_{0};
    std::mutex completionMutex_;
    std::vector<Completion> completions_;
};

} // namespace engine::core
