#include "studio/plugins/ResourcesPlugin.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "core/NativeFileDialog.hpp"

namespace engine::studio::plugins {

namespace {

bool containsIgnoringCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != haystack.end();
}

ImVec4 stateColor(core::ResourceState state) {
    switch (state) {
        case core::ResourceState::Loading: return {0.95f, 0.78f, 0.35f, 1.0f};
        case core::ResourceState::Ready: return {0.42f, 0.86f, 0.52f, 1.0f};
        case core::ResourceState::Failed: return {0.95f, 0.42f, 0.42f, 1.0f};
    }
    return {1, 1, 1, 1};
}

} // namespace

ResourcesPlugin::ResourcesPlugin(core::ResourceManager& resources) : resources_(&resources) {
    listenerId_ = resources_->addReloadListener([this](const core::ResourceHandle& handle) {
        recentReloads_.insert(recentReloads_.begin(), std::filesystem::path(handle.path()).filename().string());
        if (recentReloads_.size() > 6) recentReloads_.pop_back();
    });
}

ResourcesPlugin::~ResourcesPlugin() { resources_->removeReloadListener(listenerId_); }

void ResourcesPlugin::drawNativePlugins(core::ECS& ecs) {
    const auto& plugins = nativePlugins_->listLoadedPlugins();
    const std::string header = "Native plugins (" + std::to_string(plugins.size()) + ")###nativePlugins";
    if (!ImGui::CollapsingHeader(header.c_str())) return;

    bool autoReload = nativePlugins_->autoReloadEnabled();
    if (ImGui::Checkbox("Reload on rebuild", &autoReload)) nativePlugins_->setAutoReload(autoReload, pollSeconds_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Swap in a plugin's new build as soon as its library file changes.\n"
                          "State declared with KRONOS_HOT_RELOAD_STATE stays in place when its layout is unchanged.");
    }
    if (plugins.empty()) {
        ImGui::TextDisabled("No native plugins loaded. Put .so/.dll files in native_plugins next to Studio.");
        return;
    }

    std::string reloadName;
    for (const auto& plugin : plugins) {
        ImGui::PushID(plugin.name.c_str());
        ImGui::BulletText("%s", plugin.name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", plugin.libraryPath.c_str());
        if (const auto status = nativePlugins_->status(plugin.name)) {
            ImGui::SameLine();
            if (status->hasState) {
                ImGui::TextDisabled("state v%llu, %llu bytes, %s  |  %u reload%s",
                                    static_cast<unsigned long long>(status->stateVersion),
                                    static_cast<unsigned long long>(status->stateBytes),
                                    core::hotReloadStateTransferName(status->transfer), status->reloadCount,
                                    status->reloadCount == 1 ? "" : "s");
            } else {
                ImGui::TextDisabled("no state  |  %u reload%s", status->reloadCount, status->reloadCount == 1 ? "" : "s");
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload")) reloadName = plugin.name;
        if (!plugin.lastError.empty()) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_Text, stateColor(core::ResourceState::Failed));
            ImGui::TextWrapped("Last rebuild failed, still running the previous one: %s", plugin.lastError.c_str());
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    if (!reloadName.empty()) {
        std::string error;
        status_ = nativePlugins_->reloadPlugin(reloadName, ecs, error) ? "Reloaded " + reloadName
                                                                        : "Reloading " + reloadName + " failed: " + error;
    }
}

void ResourcesPlugin::drawPluginApi() {
    const auto plugins = pluginApi_->plugins();
    const std::string header = "Plugins (" + std::to_string(plugins.size()) + ")###pluginApi";
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (plugins.empty()) {
        ImGui::TextDisabled("No plugins. Your own go in native_plugins next to Studio; plugins from others go in");
        ImGui::TextDisabled("%s and run sandboxed.", plugin::PluginHost::thirdPartyDirectory().c_str());
        return;
    }
    std::string reloadId;
    std::string unloadId;
    for (const auto& plugin : plugins) {
        ImGui::PushID(plugin.id.c_str());
        ImGui::BulletText("%s %s", plugin.name.c_str(), plugin.version.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s%s%s", plugin.id.c_str(), plugin.path.c_str(),
                              plugin.author.empty() ? "" : "\nby ", plugin.author.c_str());
        }
        ImGui::SameLine();
        const bool sandboxed = plugin.isolation == plugin::Isolation::Sandboxed;
        ImGui::TextDisabled(sandboxed ? "sandboxed" : "trusted");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(sandboxed ? "Runs in its own process with no network, no file writes and no access\n"
                                          "outside its folder. If it crashes, Studio keeps going."
                                        : "Loaded straight into Studio from native_plugins. Only put plugins you\n"
                                          "trust there.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(plugin.running ? "Reload" : "Restart")) reloadId = plugin.id;
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) unloadId = plugin.id;
        ImGui::Indent();
        ImGui::TextDisabled("Can: %s", plugin::capabilityNames(plugin.granted).c_str());
        if (plugin.requested & ~plugin.granted) {
            ImGui::TextDisabled("Asked for but not allowed: %s",
                                plugin::capabilityNames(plugin.requested & ~plugin.granted).c_str());
        }
        if (plugin.deniedCalls > 0) ImGui::TextDisabled("Blocked %u call(s) it had no permission for", plugin.deniedCalls);
        if (!plugin.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, stateColor(core::ResourceState::Failed));
            ImGui::TextWrapped("%s%s", plugin.running ? "Last reload failed, still running the previous build: " : "Stopped: ",
                               plugin.error.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Unindent();
        ImGui::PopID();
    }
    for (const auto& importer : pluginApi_->importers()) {
        if (!importer.active) continue;
        std::string extensions;
        for (const auto& ext : importer.extensions) extensions += (extensions.empty() ? "" : " ") + ext;
        ImGui::TextDisabled("Drop %s files to convert them to %s (%s)", extensions.c_str(), importer.outputExtension.c_str(),
                            importer.type.c_str());
    }
    if (!pluginApi_->log().empty() && ImGui::TreeNode("Plugin messages")) {
        const auto& log = pluginApi_->log();
        for (size_t i = log.size() > 50 ? log.size() - 50 : 0; i < log.size(); ++i) {
            const auto& line = log[i];
            const ImVec4 color = line.level == KRONOS_LOG_ERROR     ? stateColor(core::ResourceState::Failed)
                                 : line.level == KRONOS_LOG_WARNING ? stateColor(core::ResourceState::Loading)
                                                                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::TextColored(color, "[%s] %s", line.pluginId.c_str(), line.text.c_str());
        }
        if (ImGui::SmallButton("Clear")) pluginApi_->clearLog();
        ImGui::TreePop();
    }
    if (!reloadId.empty()) {
        std::string error;
        status_ = pluginApi_->reload(reloadId, error) ? "Reloaded " + reloadId : "Reloading " + reloadId + " failed: " + error;
    }
    if (!unloadId.empty()) {
        pluginApi_->unload(unloadId);
        status_ = "Removed " + unloadId + " until Studio restarts";
    }
}

void ResourcesPlugin::drawPanel(core::ECS& ecs, core::EntityId /*selected*/,
                                const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::SetNextWindowSize(ImVec2(820.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(name());

    const core::ResourceStats stats = resources_->stats();
    ImGui::Text("%zu resident  |  %zu loading  |  %zu failed  |  %zu waiting to unload", stats.resident, stats.loading,
                stats.failed, stats.pendingUnload);
    ImGui::TextDisabled("%llu loads, %llu hot reloads, %llu unloads, %llu shared hits",
                        static_cast<unsigned long long>(stats.loadsCompleted),
                        static_cast<unsigned long long>(stats.reloadsCompleted),
                        static_cast<unsigned long long>(stats.unloads), static_cast<unsigned long long>(stats.cacheHits));

    bool hotReload = resources_->hotReloadEnabled();
    if (ImGui::Checkbox("Hot reload", &hotReload)) resources_->setHotReload(hotReload, pollSeconds_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Watch every loaded file and swap in changes while Studio runs.");
    ImGui::SameLine();
    if (ImGui::Button("Check now")) {
        const size_t changed = resources_->checkForChanges();
        status_ = changed == 0 ? "No files changed." : std::to_string(changed) + " file(s) reloading.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(core::fileDialogOpen());
    if (ImGui::Button("Load Bundle...")) {
        core::FileDialogOptions options{"Load Resource Bundle", {"*.kbundle"}, "Kronos bundles"};
        if (!core::openFileDialogAsync(options, [this](const std::string& path) {
                core::BundleHandle bundle = resources_->loadBundle(path);
                status_ = bundle.error().empty() ? "Loading bundle " + std::filesystem::path(path).filename().string()
                                                 : "Bundle error: " + bundle.error();
                bundles_.push_back(std::move(bundle));
            })) {
            status_ = "Could not open a file dialog: " + core::fileDialogError();
        }
    }
    ImGui::EndDisabled();
    if (!recentReloads_.empty()) {
        ImGui::SameLine();
        std::string joined = recentReloads_.front();
        for (size_t i = 1; i < recentReloads_.size(); ++i) joined += ", " + recentReloads_[i];
        ImGui::TextDisabled("Reloaded: %s", joined.c_str());
    }
    if (!status_.empty()) ImGui::TextUnformatted(status_.c_str());
    if (pluginApi_ != nullptr) drawPluginApi();
    if (nativePlugins_ != nullptr) drawNativePlugins(ecs);

    for (size_t i = 0; i < bundles_.size();) {
        core::BundleHandle& bundle = bundles_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::ProgressBar(bundle.progress(), ImVec2(160.0f, 0.0f));
        ImGui::SameLine();
        ImGui::Text("%s%s", std::filesystem::path(bundle.path()).filename().string().c_str(),
                    bundle.failed() ? "  (failed)" : bundle.ready() ? "  (ready)" : "");
        ImGui::SameLine();
        const bool unload = ImGui::SmallButton("Unload");
        ImGui::PopID();
        if (unload) {
            bundles_.erase(bundles_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##filter", "Filter by path", &filter_);

    const std::vector<core::ResourceInfo> infos = resources_->snapshot();
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                                       ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##resources", 7, kFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Load ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Reloads", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();
        for (const core::ResourceInfo& info : infos) {
            if (!containsIgnoringCase(info.path, filter_)) continue;
            ImGui::PushID(static_cast<int>(info.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::filesystem::path(info.path).filename().string().c_str());
            if (ImGui::IsItemHovered()) {
                std::string tip = info.path;
                if (!info.dependencies.empty()) {
                    tip += "\nDepends on:";
                    for (uint32_t dependency : info.dependencies) {
                        for (const core::ResourceInfo& other : infos) {
                            if (other.id == dependency) tip += "\n  " + std::filesystem::path(other.path).filename().string();
                        }
                    }
                }
                if (!info.error.empty()) tip += "\n" + info.error;
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(core::resourceKindName(info.kind));
            ImGui::TableNextColumn();
            ImGui::TextColored(stateColor(info.state), "%s%s", info.reloading ? "Reloading" : core::resourceStateName(info.state),
                               info.state == core::ResourceState::Ready && !info.error.empty() ? " (last reload failed)" : "");
            ImGui::TableNextColumn();
            if (info.references == 0) ImGui::TextDisabled("unloading");
            else ImGui::Text("%u", info.references);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", info.loadMilliseconds);
            ImGui::TableNextColumn();
            ImGui::Text("%u", info.reloadCount);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Reload")) {
                if (core::ResourceHandle handle = resources_->find(info.kind, info.path); handle.valid()) resources_->reload(handle);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace engine::studio::plugins
