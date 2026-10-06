#include "plugin/PluginHost.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/mman.h>
#endif

#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/ResourcePaths.hpp"
#include "plugin/PluginIpc.hpp"
#include "plugin/PluginSandbox.hpp"

struct KronosHost {
    engine::plugin::PluginHost* host = nullptr;
    engine::plugin::PluginHost::Loaded* plugin = nullptr;
};

namespace engine::plugin {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxLogLines = 1000;
constexpr uint64_t kMaxChannelBytes = 256ull * 1024 * 1024;
constexpr uint64_t kMaxImportBytes = 200ull * 1024 * 1024;
constexpr uint64_t kAllCapabilities = KRONOS_CAP_SCENE_READ | KRONOS_CAP_SCENE_WRITE | KRONOS_CAP_ASSET_IMPORTERS |
                                      KRONOS_CAP_EDITOR_PANELS | KRONOS_CAP_CHANNELS;

void* openLibrary(const std::string& path, std::string& error) {
#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(path.c_str());
    if (!handle) error = "could not load " + path + " (error " + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void*>(handle);
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* message = dlerror();
        error = message ? message : "could not load " + path;
    }
    return handle;
#endif
}

void* findSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

void closeLibrary(void* handle) {
    if (!handle) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

unsigned long processId() {
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

int64_t fileStamp(const std::string& path) {
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<int64_t>(time.time_since_epoch().count()) ^ static_cast<int64_t>(fs::file_size(path, ec));
}

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::vector<std::string> splitExtensions(const std::string& list) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= list.size()) {
        const size_t end = std::min(list.find(';', start), list.size());
        std::string ext = lower(list.substr(start, end - start));
        ext.erase(std::remove_if(ext.begin(), ext.end(), [](unsigned char c) { return std::isspace(c); }), ext.end());
        if (!ext.empty()) out.push_back(ext.front() == '.' ? ext : "." + ext);
        start = end + 1;
    }
    return out;
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-' || c == '_';
    });
}

uint64_t toHandle(core::EntityId entity) { return static_cast<uint64_t>(entt::to_integral(entity)) + 1; }

std::optional<core::EntityId> fromHandle(core::ECS* ecs, KronosEntity handle) {
    if (!ecs || handle == 0 || handle - 1 > 0xFFFFFFFFull) return std::nullopt;
    const auto entity = static_cast<core::EntityId>(static_cast<uint32_t>(handle - 1));
    if (!ecs->raw().valid(entity)) return std::nullopt;
    return entity;
}

struct UiEvent {
    uint32_t widget = 0;
    uint32_t hash = 0;
    ipc::UiKind kind{};
    int32_t intValue = 0;
    float floatValue = 0.0f;
    std::string text;
};

struct VectorOutput {
    std::vector<uint8_t> bytes;
    bool overflow = false;
};

int32_t writeOutput(void* context, const void* data, uint64_t size) {
    auto& out = *static_cast<VectorOutput*>(context);
    if (size > kMaxImportBytes - out.bytes.size()) {
        out.overflow = true;
        return KRONOS_ERROR;
    }
    const auto* raw = static_cast<const uint8_t*>(data);
    out.bytes.insert(out.bytes.end(), raw, raw + size);
    return KRONOS_OK;
}

template <typename T>
bool copyStruct(const T* in, T& out, size_t minimum) {
    if (!in || in->struct_size < minimum) return false;
    out = T{};
    std::memcpy(&out, in, std::min<size_t>(in->struct_size, sizeof(T)));
    out.struct_size = sizeof(T);
    return true;
}

const char* orEmpty(const char* s) { return s ? s : ""; }

} // namespace

struct PluginHost::Loaded {
    Plugin pub;
    LoadOptions options;
    void* library = nullptr;
    std::string tempPath;
    KronosPluginLoadFn loadFn = nullptr;
    KronosPluginTickFn tickFn = nullptr;
    KronosPluginUnloadFn unloadFn = nullptr;
    void* instance = nullptr;
    KronosHost context;
    KronosHostApi api{};
    std::unique_ptr<SandboxProcess> sandbox;
    std::vector<uint64_t> entities;
    uint64_t warnedCapabilities = 0;
    int64_t fileStamp = 0;
    int64_t pendingStamp = 0;
};

struct PluginHost::ImporterEntry {
    Importer pub;
    uint32_t remoteIndex = 0;
    KronosAssetImporter local{};
};

struct PluginHost::PanelEntry {
    Panel pub;
    uint32_t remoteIndex = 0;
    KronosPanel local{};
    std::vector<UiEvent> pending;
};

struct PluginHost::ChannelEntry {
    std::string name;
    uint32_t version = 0;
    uint64_t size = 0;
    void* data = nullptr;
    int fd = -1;

    ~ChannelEntry() {
#if defined(__linux__)
        if (fd >= 0) {
            munmap(data, size);
            ::close(fd);
            return;
        }
#endif
        ::operator delete(data, std::align_val_t(64));
    }
};

std::string capabilityNames(uint64_t capabilities) {
    static const std::pair<uint64_t, const char*> names[] = {
        {KRONOS_CAP_SCENE_READ, "read the scene"},
        {KRONOS_CAP_SCENE_WRITE, "change the scene"},
        {KRONOS_CAP_ASSET_IMPORTERS, "import assets"},
        {KRONOS_CAP_EDITOR_PANELS, "editor panels"},
        {KRONOS_CAP_CHANNELS, "shared memory"},
    };
    std::string out;
    for (const auto& [bit, name] : names) {
        if (!(capabilities & bit)) continue;
        if (!out.empty()) out += ", ";
        out += name;
    }
    return out.empty() ? "nothing" : out;
}

// Every host call goes through here, whether it came straight from an
// in-process plugin or over a sandbox's socket, so capabilities are
// checked the same way for both.
struct HostCalls {
    PluginHost& host;
    PluginHost::Loaded& plugin;

    bool allowed(uint64_t capability) {
        if (plugin.pub.granted & capability) return true;
        ++plugin.pub.deniedCalls;
        if (!(plugin.warnedCapabilities & capability)) {
            plugin.warnedCapabilities |= capability;
            host.addLog(plugin.pub.id, KRONOS_LOG_WARNING,
                        "tried to " + capabilityNames(capability) + " without permission");
        }
        return false;
    }

    void log(int32_t level, std::string text) {
        if (text.size() > 4096) text.resize(4096);
        host.addLog(plugin.pub.id, std::clamp(level, KRONOS_LOG_INFO, KRONOS_LOG_ERROR), std::move(text));
    }

    int32_t registerImporter(const std::string& type, uint32_t version, const std::string& extensions,
                             const std::string& outputExtension, uint32_t remoteIndex,
                             const KronosAssetImporter* local) {
        if (!allowed(KRONOS_CAP_ASSET_IMPORTERS)) return KRONOS_DENIED;
        auto exts = splitExtensions(extensions);
        if (!validId(type) || exts.empty() || outputExtension.size() < 2 || outputExtension.front() != '.') {
            return KRONOS_INVALID;
        }
        for (const auto& entry : host.importers_) {
            if (entry->pub.type == type && (entry->pub.pluginId == plugin.pub.id || entry->pub.version == version)) {
                return KRONOS_CONFLICT;
            }
        }
        auto entry = std::make_unique<PluginHost::ImporterEntry>();
        entry->pub.type = type;
        entry->pub.version = version;
        entry->pub.pluginId = plugin.pub.id;
        entry->pub.extensions = std::move(exts);
        entry->pub.outputExtension = lower(outputExtension);
        entry->remoteIndex = remoteIndex;
        if (local) entry->local = *local;
        host.importers_.push_back(std::move(entry));
        return KRONOS_OK;
    }

    int32_t registerPanel(const std::string& id, const std::string& title, uint32_t version, uint32_t remoteIndex,
                          const KronosPanel* local) {
        if (!allowed(KRONOS_CAP_EDITOR_PANELS)) return KRONOS_DENIED;
        if (!validId(id)) return KRONOS_INVALID;
        for (const auto& entry : host.panels_) {
            if (entry->pub.id == id && (entry->pub.pluginId == plugin.pub.id || entry->pub.version == version)) {
                return KRONOS_CONFLICT;
            }
        }
        auto entry = std::make_unique<PluginHost::PanelEntry>();
        entry->pub.id = id;
        entry->pub.title = title.empty() ? id : title.substr(0, 128);
        entry->pub.version = version;
        entry->pub.pluginId = plugin.pub.id;
        entry->remoteIndex = remoteIndex;
        if (local) entry->local = *local;
        host.panels_.push_back(std::move(entry));
        return KRONOS_OK;
    }

    int32_t openChannel(const std::string& name, uint32_t version, uint64_t size, PluginHost::ChannelView& out,
                        int* fd) {
        if (!allowed(KRONOS_CAP_CHANNELS)) return KRONOS_DENIED;
        const int32_t result = host.openChannel(name, version, size, out);
        if (result == KRONOS_OK && fd) {
            for (const auto& channel : host.channels_) {
                if (channel->name == name) *fd = channel->fd;
            }
            if (*fd < 0) return KRONOS_ERROR;
        }
        return result;
    }

    uint32_t entityCount() {
        plugin.entities.clear();
        if (!allowed(KRONOS_CAP_SCENE_READ) || !host.ecs_) return 0;
        for (auto entity : host.ecs_->view<core::Transform>()) plugin.entities.push_back(toHandle(entity));
        return static_cast<uint32_t>(plugin.entities.size());
    }

    KronosEntity entityAt(uint32_t index) {
        if (!allowed(KRONOS_CAP_SCENE_READ) || index >= plugin.entities.size()) return 0;
        return fromHandle(host.ecs_, plugin.entities[index]) ? plugin.entities[index] : 0;
    }

    KronosEntity findEntity(const std::string& name) {
        if (!allowed(KRONOS_CAP_SCENE_READ) || !host.ecs_) return 0;
        for (auto [entity, entityName] : host.ecs_->view<core::Name>().each()) {
            if (entityName.value == name) return toHandle(entity);
        }
        return 0;
    }

    std::string entityName(KronosEntity handle) {
        if (!allowed(KRONOS_CAP_SCENE_READ)) return {};
        const auto entity = fromHandle(host.ecs_, handle);
        if (!entity) return {};
        const auto* name = host.ecs_->tryGetComponent<core::Name>(*entity);
        return name ? name->value : std::string();
    }

    int32_t getPosition(KronosEntity handle, float out[3]) {
        if (!allowed(KRONOS_CAP_SCENE_READ)) return KRONOS_DENIED;
        const auto entity = fromHandle(host.ecs_, handle);
        const auto* transform = entity ? host.ecs_->tryGetComponent<core::Transform>(*entity) : nullptr;
        if (!transform) return KRONOS_NOT_FOUND;
        out[0] = transform->position.x;
        out[1] = transform->position.y;
        out[2] = transform->position.z;
        return KRONOS_OK;
    }

    int32_t setPosition(KronosEntity handle, const float position[3]) {
        if (!allowed(KRONOS_CAP_SCENE_WRITE)) return KRONOS_DENIED;
        if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) {
            return KRONOS_INVALID;
        }
        const auto entity = fromHandle(host.ecs_, handle);
        auto* transform = entity ? host.ecs_->tryGetComponent<core::Transform>(*entity) : nullptr;
        if (!transform) return KRONOS_NOT_FOUND;
        transform->position = {position[0], position[1], position[2]};
        return KRONOS_OK;
    }

    // Answers one message from a sandboxed plugin.
    void dispatch(ipc::Msg type, ipc::Reader& in, ipc::Writer& out, int& passFd) {
        switch (type) {
        case ipc::Msg::Log: {
            const auto level = in.get<int32_t>();
            std::string text = in.str(1u << 16);
            if (in.ok) log(level, std::move(text));
            break;
        }
        case ipc::Msg::RegisterImporter: {
            const auto index = in.get<uint32_t>();
            const std::string type = in.str(1024);
            const auto version = in.get<uint32_t>();
            const std::string extensions = in.str(1024);
            const std::string output = in.str(64);
            out.put(in.ok ? registerImporter(type, version, extensions, output, index, nullptr) : KRONOS_INVALID);
            break;
        }
        case ipc::Msg::RegisterPanel: {
            const auto index = in.get<uint32_t>();
            const std::string id = in.str(1024);
            const std::string title = in.str(1024);
            const auto version = in.get<uint32_t>();
            out.put(in.ok ? registerPanel(id, title, version, index, nullptr) : KRONOS_INVALID);
            break;
        }
        case ipc::Msg::OpenChannel: {
            const std::string name = in.str(1024);
            const auto version = in.get<uint32_t>();
            const auto size = in.get<uint64_t>();
            PluginHost::ChannelView view;
            int fd = -1;
            const int32_t result = in.ok ? openChannel(name, version, size, view, &fd) : KRONOS_INVALID;
            out.put(result);
            if (result == KRONOS_OK) passFd = fd;
            break;
        }
        case ipc::Msg::EntityCount:
            out.put(entityCount());
            break;
        case ipc::Msg::EntityAt: {
            const auto index = in.get<uint32_t>();
            out.put(in.ok ? entityAt(index) : KronosEntity{0});
            break;
        }
        case ipc::Msg::FindEntity: {
            const std::string name = in.str(4096);
            out.put(in.ok ? findEntity(name) : KronosEntity{0});
            break;
        }
        case ipc::Msg::EntityName: {
            const auto entity = in.get<uint64_t>();
            out.str(in.ok ? entityName(entity) : std::string());
            break;
        }
        case ipc::Msg::GetPosition: {
            const auto entity = in.get<uint64_t>();
            float position[3] = {0, 0, 0};
            out.put(in.ok ? getPosition(entity, position) : KRONOS_INVALID);
            out.put(position[0]).put(position[1]).put(position[2]);
            break;
        }
        case ipc::Msg::SetPosition: {
            const auto entity = in.get<uint64_t>();
            const float position[3] = {in.get<float>(), in.get<float>(), in.get<float>()};
            out.put(in.ok ? setPosition(entity, position) : KRONOS_INVALID);
            break;
        }
        default:
            break;
        }
    }
};

namespace {

HostCalls calls(KronosHost* context) { return HostCalls{*context->host, *context->plugin}; }

void cLog(KronosHost* h, int32_t level, const char* message) { calls(h).log(level, orEmpty(message)); }

int32_t cRegisterImporter(KronosHost* h, const KronosAssetImporter* importer) {
    KronosAssetImporter copy{};
    if (!copyStruct(importer, copy, offsetof(KronosAssetImporter, import) + sizeof(void*)) || !copy.import) {
        return KRONOS_INVALID;
    }
    return calls(h).registerImporter(orEmpty(copy.type), copy.version, orEmpty(copy.extensions),
                                     orEmpty(copy.output_extension), 0, &copy);
}

int32_t cRegisterPanel(KronosHost* h, const KronosPanel* panel) {
    KronosPanel copy{};
    if (!copyStruct(panel, copy, offsetof(KronosPanel, draw) + sizeof(void*)) || !copy.draw) return KRONOS_INVALID;
    return calls(h).registerPanel(orEmpty(copy.id), orEmpty(copy.title), copy.version, 0, &copy);
}

int32_t cOpenChannel(KronosHost* h, const char* name, uint32_t version, uint64_t size, KronosChannel* out) {
    if (!name || !out) return KRONOS_INVALID;
    PluginHost::ChannelView view;
    const int32_t result = calls(h).openChannel(name, version, size, view, nullptr);
    if (result == KRONOS_OK) *out = {view.data, view.size, view.version};
    return result;
}

uint32_t cEntityCount(KronosHost* h) { return calls(h).entityCount(); }
KronosEntity cEntityAt(KronosHost* h, uint32_t index) { return calls(h).entityAt(index); }
KronosEntity cFindEntity(KronosHost* h, const char* name) { return calls(h).findEntity(orEmpty(name)); }

uint32_t cEntityName(KronosHost* h, KronosEntity entity, char* buffer, uint32_t capacity) {
    const std::string name = calls(h).entityName(entity);
    if (buffer && capacity > 0) {
        const size_t n = std::min<size_t>(name.size(), capacity - 1);
        std::memcpy(buffer, name.data(), n);
        buffer[n] = '\0';
    }
    return static_cast<uint32_t>(name.size());
}

int32_t cGetPosition(KronosHost* h, KronosEntity entity, float out[3]) {
    if (!out) return KRONOS_INVALID;
    return calls(h).getPosition(entity, out);
}

int32_t cSetPosition(KronosHost* h, KronosEntity entity, const float position[3]) {
    if (!position) return KRONOS_INVALID;
    return calls(h).setPosition(entity, position);
}

} // namespace

PluginHost::PluginHost() = default;

PluginHost::~PluginHost() {
    unloadAll();
    channels_.clear();
}

PluginHost::Loaded* PluginHost::find(const std::string& id) {
    for (auto& plugin : loaded_) {
        if (plugin->pub.id == id) return plugin.get();
    }
    return nullptr;
}

const PluginHost::Loaded* PluginHost::find(const std::string& id) const {
    for (const auto& plugin : loaded_) {
        if (plugin->pub.id == id) return plugin.get();
    }
    return nullptr;
}

void PluginHost::addLog(const std::string& pluginId, int32_t level, std::string text) {
    if (log_.size() >= kMaxLogLines) log_.erase(log_.begin(), log_.begin() + static_cast<std::ptrdiff_t>(kMaxLogLines / 4));
    log_.push_back({pluginId, level, std::move(text)});
}

void PluginHost::removeRegistrations(const std::string& pluginId) {
    std::erase_if(importers_, [&](const auto& entry) { return entry->pub.pluginId == pluginId; });
    std::erase_if(panels_, [&](const auto& entry) { return entry->pub.pluginId == pluginId; });
}

void PluginHost::stop(Loaded& plugin, const std::string& reason) {
    removeRegistrations(plugin.pub.id);
    if (plugin.sandbox) {
        if (plugin.sandbox->running() && plugin.pub.running) {
            std::vector<uint8_t> reply;
            (void)plugin.sandbox->request(ipc::Msg::Unload, {}, reply, 500, [](auto, auto&, auto&, int&) {});
        }
        plugin.sandbox->terminate();
        plugin.sandbox.reset();
    }
    if (plugin.library) {
        if (plugin.pub.running && plugin.unloadFn) plugin.unloadFn(plugin.instance);
        closeLibrary(plugin.library);
        plugin.library = nullptr;
    }
    if (!plugin.tempPath.empty()) {
        std::error_code ec;
        fs::remove(plugin.tempPath, ec);
        plugin.tempPath.clear();
    }
    plugin.instance = nullptr;
    plugin.pub.running = false;
    if (!reason.empty()) {
        plugin.pub.error = reason;
        addLog(plugin.pub.id, KRONOS_LOG_ERROR, reason);
    }
}

bool PluginHost::load(const std::string& path, const LoadOptions& options, std::string& error,
                      std::string* loadedId) {
    std::error_code ec;
    const std::string absolute = fs::absolute(path, ec).lexically_normal().string();
    if (!fs::is_regular_file(absolute, ec)) {
        error = "plugin not found: " + path;
        return false;
    }
    auto fresh = std::make_unique<Loaded>();
    fresh->options = options;
    fresh->pub.path = absolute;
    fresh->pub.isolation = options.isolation;
    fresh->fileStamp = fileStamp(absolute);
    fresh->context = {this, fresh.get()};

    KronosPluginInfo info{};
    info.struct_size = sizeof(KronosPluginInfo);
    auto readInfo = [&](const char* id, const char* name, const char* version, const char* author) {
        fresh->pub.id = orEmpty(id);
        fresh->pub.name = orEmpty(name);
        fresh->pub.version = orEmpty(version);
        fresh->pub.author = orEmpty(author);
    };
    auto discard = [&](std::string message) {
        error = std::move(message);
        stop(*fresh, {});
        return false;
    };

    if (options.isolation == Isolation::InProcess) {
        std::string temp = absolute + ".kplugin_" + std::to_string(processId()) + "_" + std::to_string(nextTempSuffix_++);
        fs::copy_file(absolute, temp, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            temp = (fs::temp_directory_path(ec) / (fs::path(absolute).filename().string() + ".kplugin_" +
                                                   std::to_string(processId()) + "_" +
                                                   std::to_string(nextTempSuffix_++))).string();
            fs::copy_file(absolute, temp, fs::copy_options::overwrite_existing, ec);
            if (ec) return discard("could not copy " + absolute + ": " + ec.message());
        }
        fresh->tempPath = temp;
        std::string openError;
        fresh->library = openLibrary(temp, openError);
        if (!fresh->library) return discard(openError);
        auto query = reinterpret_cast<KronosPluginQueryFn>(findSymbol(fresh->library, KRONOS_PLUGIN_QUERY_SYMBOL));
        fresh->loadFn = reinterpret_cast<KronosPluginLoadFn>(findSymbol(fresh->library, KRONOS_PLUGIN_LOAD_SYMBOL));
        fresh->tickFn = reinterpret_cast<KronosPluginTickFn>(findSymbol(fresh->library, KRONOS_PLUGIN_TICK_SYMBOL));
        fresh->unloadFn = reinterpret_cast<KronosPluginUnloadFn>(findSymbol(fresh->library, KRONOS_PLUGIN_UNLOAD_SYMBOL));
        if (!query || !fresh->loadFn || !fresh->unloadFn) {
            return discard(fs::path(absolute).filename().string() +
                           " doesn't export kronos_plugin_query, kronos_plugin_load and kronos_plugin_unload");
        }
        if (query(&info) != KRONOS_OK) return discard("the plugin's query function failed");
        readInfo(info.id, info.name, info.version, info.author);
    } else {
        fresh->sandbox = std::make_unique<SandboxProcess>();
        const std::string executable = options.sandboxExecutable.empty()
                                           ? (fs::path(core::executableDirectory()) / "kronos_plugin_sandbox").string()
                                           : options.sandboxExecutable;
        std::string spawnError;
        if (!fresh->sandbox->spawn(executable, absolute, spawnError)) return discard(spawnError);
        std::vector<uint8_t> reply;
        const auto result = fresh->sandbox->request(ipc::Msg::Load, {}, reply, std::max(options.callTimeoutMs, 5000),
                                                    [](auto, auto&, auto&, int&) {});
        if (result != SandboxProcess::Result::Ok) {
            return discard("the plugin process " + fresh->sandbox->exitDescription() + " while loading");
        }
        ipc::Reader in(reply);
        const auto status = in.get<int32_t>();
        const std::string message = in.str(4096);
        info.api_major = in.get<uint16_t>();
        info.api_minor = in.get<uint16_t>();
        const std::string id = in.str(256), name = in.str(256), version = in.str(256), author = in.str(256);
        info.capabilities = in.get<uint64_t>();
        if (!in.ok) return discard("the plugin process sent a malformed reply");
        if (status != KRONOS_OK) return discard(message.empty() ? "the plugin failed to load" : message);
        readInfo(id.c_str(), name.c_str(), version.c_str(), author.c_str());
    }

    fresh->pub.apiMajor = info.api_major;
    fresh->pub.apiMinor = info.api_minor;
    fresh->pub.requested = info.capabilities;
    fresh->pub.granted = info.capabilities & options.allowedCapabilities & kAllCapabilities;
    if (info.api_major != KRONOS_PLUGIN_API_MAJOR) {
        return discard(fs::path(absolute).filename().string() + " was built for plugin API " +
                       std::to_string(info.api_major) + ", this editor uses " +
                       std::to_string(KRONOS_PLUGIN_API_MAJOR));
    }
    if (!validId(fresh->pub.id)) {
        return discard(fs::path(absolute).filename().string() +
                       " has no valid id (letters, digits, '.', '-' and '_', up to 128)");
    }
    if (fresh->pub.name.empty()) fresh->pub.name = fresh->pub.id;

    if (Loaded* existing = find(fresh->pub.id)) {
        if (existing->pub.path != absolute && existing->pub.running) {
            return discard("a plugin with id " + fresh->pub.id + " is already loaded from " + existing->pub.path);
        }
        fresh->pub.reloadCount = existing->pub.reloadCount + 1;
        stop(*existing, {});
        std::erase_if(loaded_, [&](const auto& p) { return p.get() == existing; });
    }

    Loaded& plugin = *fresh;
    loaded_.push_back(std::move(fresh));
    plugin.pub.running = true;
    if (loadedId) *loadedId = plugin.pub.id;

    if (plugin.library) {
        KronosHostApi& api = plugin.api;
        api.struct_size = sizeof(KronosHostApi);
        api.api_major = KRONOS_PLUGIN_API_MAJOR;
        api.api_minor = KRONOS_PLUGIN_API_MINOR;
        api.host = &plugin.context;
        api.granted_capabilities = plugin.pub.granted;
        api.sandboxed = 0;
        api.log = cLog;
        api.register_asset_importer = cRegisterImporter;
        api.register_panel = cRegisterPanel;
        api.open_channel = cOpenChannel;
        api.entity_count = cEntityCount;
        api.entity_at = cEntityAt;
        api.find_entity = cFindEntity;
        api.entity_name = cEntityName;
        api.get_position = cGetPosition;
        api.set_position = cSetPosition;
        const int32_t result = plugin.loadFn(&api, &plugin.instance);
        if (result != KRONOS_OK) {
            plugin.pub.running = false;
            error = plugin.pub.name + " failed to start (code " + std::to_string(result) + ")";
            stop(plugin, error);
            return false;
        }
    } else {
        ipc::Writer start;
        start.put(plugin.pub.granted);
        std::vector<uint8_t> reply;
        HostCalls handler{*this, plugin};
        const auto result = plugin.sandbox->request(
            ipc::Msg::Start, start.bytes, reply, std::max(plugin.options.callTimeoutMs, 5000),
            [&](ipc::Msg type, ipc::Reader& in, ipc::Writer& out, int& fd) { handler.dispatch(type, in, out, fd); });
        ipc::Reader in(reply);
        const int32_t status = result == SandboxProcess::Result::Ok ? in.get<int32_t>() : KRONOS_ERROR;
        if (result != SandboxProcess::Result::Ok || !in.ok || status != KRONOS_OK) {
            error = result != SandboxProcess::Result::Ok
                        ? plugin.pub.name + ": the plugin process " + plugin.sandbox->exitDescription()
                        : plugin.pub.name + " failed to start (code " + std::to_string(status) + ")";
            if (result != SandboxProcess::Result::Ok) plugin.pub.running = false;
            stop(plugin, error);
            return false;
        }
    }
    plugin.pub.error.clear();
    return true;
}

bool PluginHost::reload(const std::string& id, std::string& error) {
    const Loaded* plugin = find(id);
    if (!plugin) {
        error = "no plugin " + id;
        return false;
    }
    const std::string path = plugin->pub.path;
    const LoadOptions options = plugin->options;
    std::string loadedId;
    if (load(path, options, error, &loadedId)) {
        if (loadedId == id) return true;
        unload(id);
        return true;
    }
    if (Loaded* still = find(id)) {
        still->pub.error = error;
        still->fileStamp = fileStamp(path);
    }
    return false;
}

void PluginHost::unload(const std::string& id) {
    Loaded* plugin = find(id);
    if (!plugin) return;
    stop(*plugin, {});
    std::erase_if(loaded_, [&](const auto& p) { return p.get() == plugin; });
}

void PluginHost::unloadAll() {
    while (!loaded_.empty()) unload(loaded_.back()->pub.id);
}

void PluginHost::tick(float dt) {
    for (size_t i = 0; i < loaded_.size(); ++i) {
        Loaded& plugin = *loaded_[i];
        if (!plugin.pub.running) continue;
        if (plugin.library) {
            if (plugin.tickFn) plugin.tickFn(plugin.instance, dt);
            continue;
        }
        ipc::Writer request;
        request.put(dt);
        std::vector<uint8_t> reply;
        HostCalls handler{*this, plugin};
        const auto result = plugin.sandbox->request(
            ipc::Msg::Tick, request.bytes, reply, plugin.options.callTimeoutMs,
            [&](ipc::Msg type, ipc::Reader& in, ipc::Writer& out, int& fd) { handler.dispatch(type, in, out, fd); });
        if (result != SandboxProcess::Result::Ok) stop(plugin, "the plugin " + plugin.sandbox->exitDescription());
    }
}

void PluginHost::setAutoReload(bool enabled, double pollSeconds) {
    autoReload_ = enabled;
    pollSeconds_ = std::max(0.05, pollSeconds);
}

size_t PluginHost::update() {
    if (!autoReload_) return 0;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastPoll_ < std::chrono::duration<double>(pollSeconds_)) return 0;
    lastPoll_ = now;
    std::vector<std::string> due;
    for (auto& plugin : loaded_) {
        const int64_t stamp = fileStamp(plugin->pub.path);
        if (stamp == 0 || stamp == plugin->fileStamp) {
            plugin->pendingStamp = 0;
            continue;
        }
        if (plugin->pendingStamp == stamp) {
            due.push_back(plugin->pub.id);
        } else {
            plugin->pendingStamp = stamp;
        }
    }
    size_t reloaded = 0;
    for (const std::string& id : due) {
        std::string error;
        if (reload(id, error)) ++reloaded;
    }
    return reloaded;
}

std::vector<PluginHost::Plugin> PluginHost::plugins() const {
    std::vector<Plugin> out;
    for (const auto& plugin : loaded_) out.push_back(plugin->pub);
    return out;
}

std::optional<PluginHost::Plugin> PluginHost::plugin(const std::string& id) const {
    const Loaded* plugin = find(id);
    if (!plugin) return std::nullopt;
    return plugin->pub;
}

std::vector<PluginHost::Importer> PluginHost::importers() const {
    std::vector<Importer> out;
    for (const auto& entry : importers_) {
        Importer importer = entry->pub;
        importer.active = std::none_of(importers_.begin(), importers_.end(), [&](const auto& other) {
            return other->pub.type == importer.type && other->pub.version > importer.version;
        });
        out.push_back(std::move(importer));
    }
    return out;
}

std::optional<PluginHost::Importer> PluginHost::importerFor(const std::string& path) const {
    const std::string ext = lower(fs::path(path).extension().string());
    if (ext.empty()) return std::nullopt;
    for (const Importer& importer : importers()) {
        if (importer.active && std::find(importer.extensions.begin(), importer.extensions.end(), ext) !=
                                   importer.extensions.end()) {
            return importer;
        }
    }
    return std::nullopt;
}

bool PluginHost::importAsset(const std::string& sourceName, const std::vector<uint8_t>& data,
                             std::vector<uint8_t>& output, std::string& outputExtension, std::string& error) {
    const auto importer = importerFor(sourceName);
    if (!importer) {
        error = "no plugin imports " + fs::path(sourceName).extension().string() + " files";
        return false;
    }
    ImporterEntry* entry = nullptr;
    for (auto& candidate : importers_) {
        if (candidate->pub.type == importer->type && candidate->pub.version == importer->version) entry = candidate.get();
    }
    Loaded* plugin = entry ? find(entry->pub.pluginId) : nullptr;
    if (!plugin || !plugin->pub.running) {
        error = "the plugin for " + importer->type + " isn't running";
        return false;
    }
    const std::string name = fs::path(sourceName).filename().string();
    int32_t status = KRONOS_ERROR;
    if (plugin->library) {
        VectorOutput out;
        KronosAssetOutput table{sizeof(KronosAssetOutput), &out, writeOutput};
        status = entry->local.import(entry->local.user, name.c_str(), data.data(), data.size(), &table);
        if (out.overflow) status = KRONOS_ERROR;
        output = std::move(out.bytes);
    } else {
        if (data.size() > kMaxImportBytes) {
            error = name + " is too large to import";
            return false;
        }
        ipc::Writer request;
        request.put(entry->remoteIndex).str(name).blob(data.data(), data.size());
        std::vector<uint8_t> reply;
        HostCalls handler{*this, *plugin};
        const auto result = plugin->sandbox->request(
            ipc::Msg::Import, request.bytes, reply, plugin->options.importTimeoutMs,
            [&](ipc::Msg type, ipc::Reader& in, ipc::Writer& out, int& fd) { handler.dispatch(type, in, out, fd); });
        if (result != SandboxProcess::Result::Ok) {
            error = plugin->pub.name + ": the plugin " + plugin->sandbox->exitDescription() + " while importing " + name;
            stop(*plugin, error);
            return false;
        }
        ipc::Reader in(reply);
        status = in.get<int32_t>();
        if (!in.blob(output)) status = KRONOS_ERROR;
    }
    if (status != KRONOS_OK) {
        error = importer->type + " couldn't import " + name + " (code " + std::to_string(status) + ")";
        output.clear();
        return false;
    }
    outputExtension = importer->outputExtension;
    return true;
}

bool PluginHost::importFile(const std::string& sourcePath, std::string& outputPath, std::string& error,
                            const std::string& outputDirectory) {
    std::error_code ec;
    const auto size = fs::file_size(sourcePath, ec);
    if (ec || size > kMaxImportBytes) {
        error = ec ? "could not read " + sourcePath : sourcePath + " is too large to import";
        return false;
    }
    std::ifstream in(sourcePath, std::ios::binary);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
        error = "could not read " + sourcePath;
        return false;
    }
    std::vector<uint8_t> output;
    std::string extension;
    if (!importAsset(sourcePath, data, output, extension, error)) return false;
    fs::path target = fs::path(sourcePath).replace_extension(extension);
    if (!outputDirectory.empty()) {
        fs::create_directories(outputDirectory, ec);
        target = fs::path(outputDirectory) / target.filename();
    }
    if (lower(target.string()) == lower(sourcePath)) {
        error = "the importer would overwrite " + sourcePath;
        return false;
    }
    const std::string temp = target.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
        if (!out) {
            error = "could not write " + temp;
            return false;
        }
    }
    fs::rename(temp, target, ec);
    if (ec) {
        error = "could not write " + target.string() + ": " + ec.message();
        fs::remove(temp, ec);
        return false;
    }
    outputPath = target.string();
    return true;
}

std::vector<PluginHost::Panel> PluginHost::panels() const {
    std::vector<Panel> out;
    for (const auto& entry : panels_) {
        Panel panel = entry->pub;
        panel.active = std::none_of(panels_.begin(), panels_.end(), [&](const auto& other) {
            return other->pub.id == panel.id && other->pub.version > panel.version;
        });
        out.push_back(std::move(panel));
    }
    return out;
}

bool PluginHost::drawPanel(const std::string& panelId, const KronosUi& ui) {
    PanelEntry* entry = nullptr;
    for (auto& candidate : panels_) {
        if (candidate->pub.id == panelId && (!entry || candidate->pub.version > entry->pub.version)) {
            entry = candidate.get();
        }
    }
    Loaded* plugin = entry ? find(entry->pub.pluginId) : nullptr;
    if (!plugin || !plugin->pub.running) return false;
    if (plugin->library) {
        entry->local.draw(entry->local.user, &ui);
        return true;
    }

    ipc::Writer request;
    request.put(entry->remoteIndex).put(static_cast<uint32_t>(entry->pending.size()));
    for (const UiEvent& event : entry->pending) {
        request.put(event.widget).put(event.hash).put(event.kind);
        if (event.kind == ipc::UiKind::Checkbox) request.put(event.intValue);
        if (event.kind == ipc::UiKind::Slider) request.put(event.floatValue);
        if (event.kind == ipc::UiKind::InputText) request.str(event.text);
    }
    entry->pending.clear();
    std::vector<uint8_t> reply;
    HostCalls handler{*this, *plugin};
    const auto result = plugin->sandbox->request(
        ipc::Msg::Draw, request.bytes, reply, plugin->options.callTimeoutMs,
        [&](ipc::Msg type, ipc::Reader& in, ipc::Writer& out, int& fd) { handler.dispatch(type, in, out, fd); });
    if (result != SandboxProcess::Result::Ok) {
        stop(*plugin, "the plugin " + plugin->sandbox->exitDescription());
        return false;
    }

    // The registry may have changed while the plugin was drawing.
    entry = nullptr;
    for (auto& candidate : panels_) {
        if (candidate->pub.id == panelId && candidate->pub.pluginId == plugin->pub.id) entry = candidate.get();
    }
    ipc::Reader in(reply);
    const auto count = in.get<uint32_t>();
    if (!in.ok || count > ipc::kMaxUiCommands) return true;
    std::vector<UiEvent> events;
    uint32_t widget = 0;
    void* c = ui.context;
    for (uint32_t i = 0; i < count && in.ok; ++i) {
        const auto kind = in.get<ipc::UiKind>();
        switch (kind) {
        case ipc::UiKind::Text: {
            const std::string text = in.str(1u << 16);
            if (in.ok && ui.text) ui.text(c, text.c_str());
            break;
        }
        case ipc::UiKind::Button: {
            const std::string label = in.str(1024);
            const uint32_t index = widget++;
            if (in.ok && ui.button && ui.button(c, label.c_str())) {
                events.push_back({index, ipc::labelHash(label.c_str()), kind});
            }
            break;
        }
        case ipc::UiKind::Checkbox: {
            const std::string label = in.str(1024);
            int32_t value = in.get<int32_t>();
            const uint32_t index = widget++;
            if (in.ok && ui.checkbox && ui.checkbox(c, label.c_str(), &value)) {
                events.push_back({index, ipc::labelHash(label.c_str()), kind, value});
            }
            break;
        }
        case ipc::UiKind::Slider: {
            const std::string label = in.str(1024);
            float value = in.get<float>();
            const float min = in.get<float>();
            const float max = in.get<float>();
            const uint32_t index = widget++;
            if (in.ok && ui.slider_float && ui.slider_float(c, label.c_str(), &value, min, max)) {
                events.push_back({index, ipc::labelHash(label.c_str()), kind, 0, value});
            }
            break;
        }
        case ipc::UiKind::InputText: {
            const std::string label = in.str(1024);
            const auto capacity = in.get<uint32_t>();
            const std::string value = in.str(1u << 16);
            const uint32_t index = widget++;
            if (!in.ok || capacity == 0 || capacity > (1u << 16)) break;
            std::vector<char> buffer(capacity, '\0');
            std::memcpy(buffer.data(), value.data(), std::min<size_t>(value.size(), capacity - 1));
            if (ui.input_text && ui.input_text(c, label.c_str(), buffer.data(), capacity)) {
                buffer.back() = '\0';
                events.push_back({index, ipc::labelHash(label.c_str()), kind, 0, 0.0f, buffer.data()});
            }
            break;
        }
        case ipc::UiKind::Separator:
            if (ui.separator) ui.separator(c);
            break;
        case ipc::UiKind::SameLine:
            if (ui.same_line) ui.same_line(c);
            break;
        default:
            in.ok = false;
            break;
        }
    }
    if (entry) entry->pending = std::move(events);
    return true;
}

int32_t PluginHost::openChannel(const std::string& name, uint32_t version, uint64_t size, ChannelView& out) {
    if (!validId(name) || size == 0 || size > kMaxChannelBytes) return KRONOS_INVALID;
    for (const auto& channel : channels_) {
        if (channel->name != name) continue;
        if (channel->version != version || channel->size != size) return KRONOS_CONFLICT;
        out = {channel->data, channel->size, channel->version};
        return KRONOS_OK;
    }
    auto channel = std::make_unique<ChannelEntry>();
    channel->name = name;
    channel->version = version;
    channel->size = size;
#if defined(__linux__)
    const int fd = memfd_create(("kronos-channel:" + name).c_str(), MFD_CLOEXEC);
    if (fd >= 0) {
        void* data = ftruncate(fd, static_cast<off_t>(size)) == 0
                         ? mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
                         : MAP_FAILED;
        if (data == MAP_FAILED) {
            ::close(fd);
            return KRONOS_ERROR;
        }
        channel->fd = fd;
        channel->data = data;
    }
#endif
    if (!channel->data) {
        channel->data = ::operator new(size, std::align_val_t(64), std::nothrow);
        if (!channel->data) return KRONOS_ERROR;
        std::memset(channel->data, 0, size);
    }
    out = {channel->data, channel->size, channel->version};
    channels_.push_back(std::move(channel));
    return KRONOS_OK;
}

std::optional<PluginHost::ChannelView> PluginHost::channel(const std::string& name) const {
    for (const auto& channel : channels_) {
        if (channel->name == name) return ChannelView{channel->data, channel->size, channel->version};
    }
    return std::nullopt;
}

bool PluginHost::exportsPluginApi(const std::string& libraryPath) {
    std::string error;
    void* handle = openLibrary(libraryPath, error);
    if (!handle) return false;
    const bool exported = findSymbol(handle, KRONOS_PLUGIN_QUERY_SYMBOL) != nullptr;
    closeLibrary(handle);
    return exported;
}

std::string PluginHost::thirdPartyDirectory() {
    auto env = [](const char* name) {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    };
    if (std::string dir = env("KRONOS_PLUGIN_DIR"); !dir.empty()) return dir;
#if defined(_WIN32)
    if (std::string dir = env("LOCALAPPDATA"); !dir.empty()) return (fs::path(dir) / "Kronos" / "plugins").string();
#else
    if (std::string dir = env("XDG_DATA_HOME"); !dir.empty()) return (fs::path(dir) / "kronos" / "plugins").string();
    if (std::string dir = env("HOME"); !dir.empty()) return (fs::path(dir) / ".local" / "share" / "kronos" / "plugins").string();
#endif
    return {};
}

std::vector<std::string> PluginHost::findLibraries(const std::string& directory) {
    std::vector<std::string> out;
    std::error_code ec;
    if (directory.empty() || !fs::is_directory(directory, ec)) return out;
    auto consider = [&](const fs::directory_entry& entry) {
        if (!entry.is_regular_file(ec)) return;
        const std::string name = entry.path().filename().string();
        if (name.find(".kplugin_") != std::string::npos || name.find(".hotreload_") != std::string::npos) return;
        const std::string ext = lower(entry.path().extension().string());
        if (ext == ".so" || ext == ".dll" || ext == ".dylib") out.push_back(entry.path().string());
    };
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_directory(ec)) {
            for (const auto& inner : fs::directory_iterator(entry.path(), ec)) consider(inner);
        } else {
            consider(entry);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace engine::plugin
