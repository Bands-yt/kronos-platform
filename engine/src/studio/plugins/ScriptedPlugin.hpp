#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/ECS.hpp"
#include "core/ScriptNetworkApi.hpp"
#include "core/Scripting.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PluginManifest.hpp"

namespace engine::net {
class NetworkSession;
}

namespace engine::studio::plugins {

// A real third-party plugin: loads a PluginManifest (PluginManifest.hpp)
// plus its entry Luau script into a fresh, real, sandboxed
// core::Scripting VM -- the exact same per-script memory/time-budget/
// interrupt-watchdog sandboxing gameplay scripts get
// (core::Scripting::initialize()), not a separate, weaker "plugin
// sandbox" built from scratch. The script gets print/engine.log/task.*
// (core::Scripting's own bindings) plus a small ECS-only `world` table
// (studio::registerStudioEcsBindings(), same one
// studio::panels::DebugConsolePanel uses) -- no ImGui/UI-drawing access
// of its own; a scripted plugin observes/manipulates the ECS, it doesn't
// paint custom widgets (that would need a whole declarative-UI-from-Luau
// surface, a real, separate, larger feature this doesn't attempt to
// half-build).
//
// load()/reload() are real, not stubs: reload() re-reads the manifest
// and script from disk and starts a fresh VM, discarding whatever state
// the old one had. drawPanel() surfaces real compile/runtime errors
// (forwarded through core::Scripting::setOutputCallback(), the same
// mechanism DebugConsolePanel's error display already uses) and a
// "changed on disk" notice (via core::fileWriteTimeSeconds(), AssetCache
// .hpp's mtime probe) rather than silently auto-reloading -- swapping a
// running plugin's state out from under it without being asked is a
// worse default than making the user click Reload.
//
// Kronos (Alpha Roadmap Phase 5, "Plugin System Expansion"): a scripted
// plugin now also gets the real `network` table (core::ScriptNetworkApi,
// wrapping Studio's own live net::NetworkSession -- see
// StudioApp.hpp's networkSession_), the exact same binding gameplay
// scripts get -- "Plugin networking access" from the roadmap. Every
// reload() fires events.onUnload() first (Scripting::shutdown()'s own
// real hook, see Scripting.hpp) -- "Plugin lifecycle events... onUnload"
// -- giving a plugin a real chance to clean up (stop a running effect,
// release a resource) before its old VM state is discarded. Plugin UI
// access (a script drawing its own ImGui widgets) and plugin asset access
// remain real, deliberately out-of-scope gaps -- see this phase's own
// docs/PLUGIN_SYSTEM.md for why.
class ScriptedPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] bool load(const std::string& manifestPath, core::ECS& ecs, net::NetworkSession& networkSession);
    [[nodiscard]] bool reload();

    [[nodiscard]] const char* name() const override { return manifest_.name.c_str(); }
    [[nodiscard]] const char* category() const override { return "Third-Party"; }

    // Read-only introspection -- exists so plugin-loader tests can verify
    // load()/reload() outcomes directly instead of only through
    // drawPanel()'s ImGui calls, and so drawPanel() itself has a single
    // real implementation of "did the script change on disk" to call
    // rather than inlining the check (same "extract the pure decision
    // logic" shape as core::assetNeedsReload/core::rayIntersectsAabb).
    [[nodiscard]] bool isLoaded() const { return loaded_; }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }
    [[nodiscard]] const std::vector<std::string>& outputLog() const { return outputLog_; }
    [[nodiscard]] const PluginManifest& manifest() const { return manifest_; }
    [[nodiscard]] bool scriptChangedOnDisk() const;

    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    [[nodiscard]] bool loadInternal();
    void appendLine(const std::string& line);
    [[nodiscard]] std::string resolvedScriptPath() const;

    core::ECS* ecs_ = nullptr;
    net::NetworkSession* networkSession_ = nullptr;
    std::string manifestPath_;
    PluginManifest manifest_;

    core::Scripting scripting_;
    // Constructed once, on first load() -- reused across reload()s, same
    // "one C++ object, re-registered into each fresh VM" shape
    // core::Application's own scriptNetworkApi_ uses. unique_ptr since it
    // holds a reference to scripting_ (this object's own member, address-
    // stable) bound at construction, so it can't be default-constructed
    // before networkSession_ is known.
    std::unique_ptr<core::ScriptNetworkApi> scriptNetworkApi_;
    bool loaded_ = false;
    std::vector<std::string> outputLog_;
    std::string lastError_;
    int64_t scriptWriteTimeAtLoad_ = 0;
};

} // namespace engine::studio::plugins
