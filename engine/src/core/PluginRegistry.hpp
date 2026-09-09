#pragma once

#include <memory>
#include <string>
#include <vector>

namespace engine::core {

// Kronos ("100 Essential Plugins" scaffolding): the real, stated scope of
// this file is a REGISTRY, not 100 real features -- every entry
// registerAll() creates today is a StubPluginModule (metadata only, does
// nothing on initialize()/shutdown()). The point is a single, real place
// that already knows every planned plugin's id/category/description
// before a single one is actually implemented, so later work replaces one
// StubPluginModule at a time with a real IPluginModule subclass without
// ever touching this registry's own shape or any other plugin's entry.
enum class PluginCategory {
    CoreEditorUx,
    ScriptingLogic,
    ModelingMeshEditing,
    MaterialsShading,
    AnimationCinematics,
    WorldBuildingEnvironment,
    LightingRendering,
    PhysicsSimulation,
    AudioSoundscapes,
    BuildNetworkPipeline,
};

// `enabled` defaults false -- these are catalogue stubs, not live
// features a user could accidentally flip on and get nothing from.
struct PluginMetadata {
    std::string id;
    std::string displayName;
    std::string description;
    PluginCategory category = PluginCategory::CoreEditorUx;
    bool enabled = false;
};

// The one real interface every future plugin (stub or genuinely
// implemented) shares -- PluginRegistry only ever talks to modules
// through this, so swapping a StubPluginModule for a real
// implementation is a one-line change at its registerAll() call site,
// nothing else in the registry or any caller needs to know.
class IPluginModule {
public:
    virtual ~IPluginModule() = default;
    [[nodiscard]] virtual const PluginMetadata& metadata() const = 0;
    virtual void initialize() {}
    virtual void shutdown() {}
};

// What every one of the 100 registerAll() entries actually is today --
// holds its own real metadata, does nothing else. Not a placeholder to
// delete later; a real implementation replaces the StubPluginModule
// instance for its id with a real IPluginModule subclass, it doesn't
// need this class to change.
class StubPluginModule final : public IPluginModule {
public:
    explicit StubPluginModule(PluginMetadata metadata) : metadata_(std::move(metadata)) {}
    [[nodiscard]] const PluginMetadata& metadata() const override { return metadata_; }

private:
    PluginMetadata metadata_;
};

// Owns every registered module and answers the 3 real questions a caller
// (a future PluginBrowserPlugin-style catalogue UI, or a test asserting
// "100 plugins are registered") needs: how many, which ones are in a
// given category, and look one up by id. Real, intentionally NOT the
// same type as studio::PluginManager (that one owns live IStudioPlugin
// windows with ImGui draw calls); this is the pre-implementation
// catalogue those 100 real plugins eventually graduate into.
class PluginRegistry {
public:
    // Real, idempotent-in-intent population of exactly 100 StubPluginModule
    // entries (see PluginRegistry.cpp for the full list) -- called once by
    // an owner at startup. Calling it twice on the same instance would
    // duplicate every entry; it doesn't guard against that itself, same
    // "caller's responsibility, not this method's" convention
    // PluginManager::registerPlugin() already uses.
    void registerAll();

    [[nodiscard]] const std::vector<std::unique_ptr<IPluginModule>>& modules() const { return modules_; }
    [[nodiscard]] size_t count() const { return modules_.size(); }
    [[nodiscard]] std::vector<const PluginMetadata*> byCategory(PluginCategory category) const;
    [[nodiscard]] const IPluginModule* find(const std::string& id) const;

private:
    std::vector<std::unique_ptr<IPluginModule>> modules_;
};

} // namespace engine::core
