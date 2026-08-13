#pragma once

#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "core/Texture.hpp"
#include "net/NetworkSession.hpp"
#include "studio/IStudioPlugin.hpp"
#include "tntwars/MapLayout.hpp"

namespace engine::studio::plugins {

// Sprint 14 ("TNT-Wars Core Game Build")'s real Studio-side authoring
// tools: "map editing" (build/clear real, procedural static level
// geometry for any of the four real tntwars::MapId maps into the live
// ECS, driven by tntwars::buildMapLayout()'s real per-map layout data --
// mirroring plugins::PrefabPlugin::spawnInstance()'s exact real
// Transform+Renderable+MeshSource shape, see MapLayout.hpp's own header
// comment for why building live physics colliders here is deliberately
// out of scope, same documented boundary core::SceneFile.hpp already
// draws for Studio-authored geometry) and "class tuning" (live-edit
// every real tntwars::ClassStats field per class, and every real
// tntwars::MapModifiers field per map, both through the exact
// tntwars::TntWarsMatch& this Studio session's real net::NetworkSession
// owns -- see TntWarsMatch::classTuning()/mapTuning()'s own comment on
// why an edit here takes real effect on a running match, not just a
// display).
//
// Same "plugin holds a reference to state StudioApp already owns"
// pattern plugins::ModerationPanel/PublishingPanel already established;
// this one additionally needs the real Vulkan mesh-build handles
// plugins::PrefabPlugin already takes, for the exact same reason (real
// GPU mesh creation for its own spawned geometry).
class TntWarsPlugin final : public IStudioPlugin {
public:
    TntWarsPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                  core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary, net::NetworkSession& session);

    [[nodiscard]] const char* name() const override { return "TNT-Wars"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawMapEditingSection(core::ECS& ecs);
    void drawClassTuningSection();
    void drawMapTuningSection();
    void drawMatchFlowSection();

    void buildMapGeometry(core::ECS& ecs);
    void clearMapGeometry(core::ECS& ecs);

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    net::NetworkSession* session_;
    // Sprint 16 ("Cinematic Graphics" Phase 2): generated once at
    // construction (see the constructor's own comment), applied per-piece
    // in buildMapGeometry() below via tntwars::classifyMapPieceMaterial().
    core::ProceduralMaterialLibrary materials_;

    int mapIndex_ = 0;               // index into the real MapId enum, drives both editing sections below
    std::vector<core::EntityId> spawnedMapEntities_; // real entities from the last buildMapGeometry(), for Clear

    int classIndex_ = 0; // index into the real PlayerClassType enum, drives Class Tuning below
};

} // namespace engine::studio::plugins
