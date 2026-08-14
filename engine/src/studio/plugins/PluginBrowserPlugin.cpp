#include "studio/plugins/PluginBrowserPlugin.hpp"

#include <filesystem>
#include <memory>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "studio/Notification.hpp"
#include "studio/PluginChrome.hpp"
#include "studio/PluginManager.hpp"
#include "studio/plugins/ScriptedPlugin.hpp"

namespace engine::studio::plugins {

void PluginBrowserPlugin::loadFromManifestPath(const std::string& manifestPath) {
    auto plugin = std::make_unique<ScriptedPlugin>();
    bool ok = plugin->load(manifestPath, *ecs_, *networkSession_);
    statusMessage_ = ok ? ("Loaded \"" + std::string(plugin->name()) + "\" -- see the Plugins menu for its window.")
                         : ("Failed to load from " + manifestPath + " -- open its window (Plugins menu) for the error.");
    notifications_->push(statusMessage_, ok ? NotificationSeverity::Success : NotificationSeverity::Error);
    // Registered either way, success or failure: a failed load still gets
    // a window showing lastError_ and a Reload button, rather than
    // silently discarding the attempt (see ScriptedPlugin's class
    // comment).
    pluginManager_->registerPlugin(std::move(plugin));
}

void PluginBrowserPlugin::scanNow() {
    discoveredPlugins_ = scanLocalPluginDirectory(pluginDirectoryBuffer_);
    hasScanned_ = true;
}

void PluginBrowserPlugin::createStarterPlugin() {
    std::error_code ec;
    std::filesystem::create_directories(pluginDirectoryBuffer_, ec);

    // Real, collision-avoiding directory name -- "MyPlugin", "MyPlugin2",
    // ... -- same "probe with exists() before picking" shape as
    // net::generateSessionId()'s own real-uniqueness reasoning elsewhere
    // in this codebase, just filesystem-flavored instead of numeric.
    std::string name = "MyPlugin";
    int suffix = 1;
    std::filesystem::path destination = std::filesystem::path(pluginDirectoryBuffer_) / name;
    while (std::filesystem::exists(destination, ec)) {
        ++suffix;
        name = "MyPlugin" + std::to_string(suffix);
        destination = std::filesystem::path(pluginDirectoryBuffer_) / name;
    }

    std::filesystem::copy("templates/plugin", destination, std::filesystem::copy_options::recursive, ec);
    if (ec) {
        statusMessage_ = "Failed to create starter plugin at " + destination.string() + ": " + ec.message();
        notifications_->push(statusMessage_, NotificationSeverity::Error);
        return;
    }
    statusMessage_ = "Created \"" + name + "\" in " + pluginDirectoryBuffer_ + " -- Scan to find it below.";
    notifications_->push(statusMessage_, NotificationSeverity::Success);
    scanNow();
}

void PluginBrowserPlugin::drawLocalPluginsSection() {
    ImGui::TextUnformatted("Local Plugins");
    ImGui::TextDisabled("Discovers *.manifest files sitting in a real local directory -- \"local only for alpha\".");

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    ImGui::InputText("##plugin_directory", &pluginDirectoryBuffer_);
    ImGui::SameLine();
    if (ImGui::Button("Scan")) {
        scanNow();
        notifications_->push(discoveredPlugins_.empty()
                                  ? ("No plugins found in \"" + pluginDirectoryBuffer_ + "\".")
                                  : ("Found " + std::to_string(discoveredPlugins_.size()) + " plugin(s) in \"" +
                                     pluginDirectoryBuffer_ + "\"."),
                              discoveredPlugins_.empty() ? NotificationSeverity::Warning : NotificationSeverity::Success);
    }
    ImGui::SameLine();
    helpMarker("A path relative to Studio's working directory (or absolute). Re-scan any time after adding/removing "
               "manifests -- this list is a real snapshot, not a live watch.");

    if (hasScanned_ && discoveredPlugins_.empty()) {
        // Kronos: real empty-state guidance (found via a live playtest --
        // a plain "no manifests found" line left a first-time creator with
        // no idea what to do next). "Kronos ships with 29 built-in native
        // plugins" -- the real, current count (see StudioApp::initialize()'s
        // registerPlugin() calls), not a stale copy-pasted number.
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.33f, 0.72f, 0.70f, 1.0f), "Kronos Plugins");
        ImGui::TextWrapped("Your plugin folder (\"%s\") is currently empty.", pluginDirectoryBuffer_.c_str());
        ImGui::TextWrapped(
            "Kronos ships with 29 built-in native plugins (Terrain, Material, Animator, Lighting, and more -- see "
            "the Plugins menu above). Your own custom plugins go in \"%s\".",
            pluginDirectoryBuffer_.c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted("To get started:");
        ImGui::BulletText("Copy a plugin from templates/plugin/");
        ImGui::BulletText("Or click \"Create Starter Plugin\" below");
        ImGui::Spacing();
        if (ImGui::Button("Open Templates")) {
            manifestPathBuffer_ = "templates/plugin/example.manifest";
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Starter Plugin")) {
            createStarterPlugin();
        }
        ImGui::SameLine();
        if (ImGui::Button("View Plugin Docs")) {
            notifications_->push("Plugin docs: docs/PLUGIN_SYSTEM.md", NotificationSeverity::Info);
        }
        ImGui::Spacing();
    }
    for (const DiscoveredPlugin& discovered : discoveredPlugins_) {
        ImGui::PushID(discovered.manifestPath.c_str());
        if (discovered.parseSucceeded) {
            ImGui::Text("%s (%s)", discovered.manifest.name.c_str(), discovered.manifest.version.c_str());
            if (!discovered.manifest.description.empty()) ImGui::TextDisabled("%s", discovered.manifest.description.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Load")) loadFromManifestPath(discovered.manifestPath);
        } else {
            ImGui::TextColored(ImVec4(0.90f, 0.30f, 0.30f, 1.0f), "%s (failed to parse)", discovered.manifestPath.c_str());
        }
        ImGui::PopID();
    }
}

void PluginBrowserPlugin::drawPanel(core::ECS& ecs, core::EntityId selected,
                                     const std::vector<core::EntityId>& selectedEntities) {
    (void)selected;
    (void)selectedEntities;
    ecs_ = &ecs; // stashed so the "Load Plugin" button below has an ECS to hand a fresh ScriptedPlugin
    if (!hasScanned_) scanNow(); // real, so the empty-state guidance below shows without a manual Scan click first

    ImGui::Begin("Plugin Browser");
    ImGui::TextWrapped(
        "Load a third-party plugin by pointing at its manifest file -- a small "
        "text file naming the plugin and its entry Luau script (see "
        "studio/PluginManifest.hpp for the exact format). The script runs in "
        "its own sandboxed Luau VM with the same memory/time budgets "
        "gameplay scripts get, plus a small ECS-only `world` table and a "
        "`network` table for real client/server RPC.");

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##manifest_path", "path/to/plugin.manifest", &manifestPathBuffer_);

    if (ImGui::Button("Load Plugin") && !manifestPathBuffer_.empty()) {
        loadFromManifestPath(manifestPathBuffer_);
    }

    if (!statusMessage_.empty()) {
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    }

    ImGui::Separator();
    drawLocalPluginsSection();

    ImGui::Separator();
    ImGui::TextUnformatted("Loaded scripted plugins:");
    ImGui::BeginChild("scripted_plugin_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    bool any = false;
    for (auto& p : pluginManager_->plugins()) {
        if (dynamic_cast<ScriptedPlugin*>(p.get()) == nullptr) continue;
        any = true;
        ImGui::BulletText("%s", p->name());
    }
    if (!any) ImGui::TextDisabled("(none loaded yet)");
    ImGui::EndChild();

    ImGui::End();
}

} // namespace engine::studio::plugins
