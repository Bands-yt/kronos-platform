#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "plugin/kronos_plugin.h"

namespace engine::core {
class ECS;
}

namespace engine::plugin {

enum class Isolation : uint8_t {
    InProcess, // trusted: your own plugins, loaded straight into the editor
    Sandboxed, // third-party: its own locked-down process
};

[[nodiscard]] std::string capabilityNames(uint64_t capabilities);

// Loads plugins built against kronos_plugin.h and keeps the registries
// they add to: asset importers, editor panels and shared memory channels.
// Registrations are versioned by name, so a newer importer or panel
// replaces an older one and the older one comes back if the newer plugin
// is removed. Everything a plugin registered goes away when it unloads.
class PluginHost {
public:
    struct LoadOptions {
        Isolation isolation = Isolation::InProcess;
        uint64_t allowedCapabilities = ~0ull;
        std::string sandboxExecutable; // default: kronos_plugin_sandbox next to this executable
        int callTimeoutMs = 2000;      // sandboxed plugins taking longer are stopped
        int importTimeoutMs = 30000;
    };

    struct Plugin {
        std::string id;
        std::string name;
        std::string version;
        std::string author;
        std::string path;
        uint16_t apiMajor = 0;
        uint16_t apiMinor = 0;
        uint64_t requested = 0;
        uint64_t granted = 0;
        Isolation isolation = Isolation::InProcess;
        bool running = false;
        std::string error; // why it stopped (crash, timeout) or why a reload was refused
        uint32_t deniedCalls = 0;
        uint32_t reloadCount = 0;
    };

    struct Importer {
        std::string type;
        uint32_t version = 0;
        std::string pluginId;
        std::vector<std::string> extensions;
        std::string outputExtension;
        bool active = false; // the highest version of its type
    };

    struct Panel {
        std::string id;
        std::string title;
        uint32_t version = 0;
        std::string pluginId;
        bool active = false;
    };

    struct ChannelView {
        void* data = nullptr;
        uint64_t size = 0;
        uint32_t version = 0;
    };

    struct LogLine {
        std::string pluginId;
        int32_t level = KRONOS_LOG_INFO;
        std::string text;
    };

    PluginHost();
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    void setScene(core::ECS* ecs) { ecs_ = ecs; }

    // Loads a plugin, or reloads it if a plugin with the same id is already
    // loaded. A plugin that fails its checks (missing exports, another API
    // major version) leaves the running one alone.
    bool load(const std::string& path, const LoadOptions& options, std::string& error, std::string* loadedId = nullptr);
    bool reload(const std::string& id, std::string& error);
    void unload(const std::string& id);
    void unloadAll();

    void tick(float dt);
    void setAutoReload(bool enabled, double pollSeconds = 0.5);
    [[nodiscard]] bool autoReload() const { return autoReload_; }
    // Reloads plugins whose library changed and has stayed unchanged for
    // one poll. Returns how many were reloaded.
    size_t update();

    [[nodiscard]] std::vector<Plugin> plugins() const;
    [[nodiscard]] std::optional<Plugin> plugin(const std::string& id) const;

    [[nodiscard]] std::vector<Importer> importers() const;
    [[nodiscard]] std::optional<Importer> importerFor(const std::string& path) const;
    bool importAsset(const std::string& sourceName, const std::vector<uint8_t>& data, std::vector<uint8_t>& output,
                     std::string& outputExtension, std::string& error);
    // Converts a file: model.hgt -> model.png, next to the source unless
    // `outputDirectory` is given. Returns the new path.
    bool importFile(const std::string& sourcePath, std::string& outputPath, std::string& error,
                    const std::string& outputDirectory = {});

    [[nodiscard]] std::vector<Panel> panels() const;
    // Draws the active panel with that id. False if no plugin provides it.
    bool drawPanel(const std::string& panelId, const KronosUi& ui);

    int32_t openChannel(const std::string& name, uint32_t version, uint64_t size, ChannelView& out);
    [[nodiscard]] std::optional<ChannelView> channel(const std::string& name) const;

    [[nodiscard]] const std::vector<LogLine>& log() const { return log_; }
    void clearLog() { log_.clear(); }

    // Whether a trusted library uses this API (as opposed to the C++
    // hot-reload module interface). Loads the library to check, so only
    // call it on code you trust.
    [[nodiscard]] static bool exportsPluginApi(const std::string& libraryPath);
    // Where third-party plugins are installed: KRONOS_PLUGIN_DIR, or the
    // user's data folder (~/.local/share/kronos/plugins on Linux).
    [[nodiscard]] static std::string thirdPartyDirectory();
    // Shared libraries in `directory` and its immediate subfolders.
    [[nodiscard]] static std::vector<std::string> findLibraries(const std::string& directory);

    struct Loaded;
    struct HostContext;

private:
    friend struct HostCalls;

    Loaded* find(const std::string& id);
    const Loaded* find(const std::string& id) const;
    void stop(Loaded& plugin, const std::string& reason);
    void removeRegistrations(const std::string& pluginId);
    void addLog(const std::string& pluginId, int32_t level, std::string text);

    core::ECS* ecs_ = nullptr;
    std::vector<std::unique_ptr<Loaded>> loaded_;
    struct ImporterEntry;
    struct PanelEntry;
    struct ChannelEntry;
    std::vector<std::unique_ptr<ImporterEntry>> importers_;
    std::vector<std::unique_ptr<PanelEntry>> panels_;
    std::vector<std::unique_ptr<ChannelEntry>> channels_;
    std::vector<LogLine> log_;
    bool autoReload_ = false;
    double pollSeconds_ = 0.5;
    std::chrono::steady_clock::time_point lastPoll_{};
    int nextTempSuffix_ = 0;
};

} // namespace engine::plugin
