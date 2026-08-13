#include "studio/plugins/TntWarsPlugin.hpp"

#include <cstdio>

#include <imgui.h>

#include "core/Components.hpp"
#include "tntwars/MapDefinition.hpp"
#include "tntwars/MapMaterial.hpp"
#include "tntwars/MatchFlow.hpp"

namespace engine::studio::plugins {

namespace {
const char* kMapNames[] = {"Trenches", "Mantle", "Sky Platforms", "Island Sea", "Space"};
const char* kClassNames[] = {"Striker", "Deflector", "Engineer", "Interceptor", "Saboteur"};
} // namespace

TntWarsPlugin::TntWarsPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                              core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary,
                              net::NetworkSession& session)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary),
      session_(&session),
      // Sprint 16: generated once here (real GPU texture uploads, not
      // free to repeat per-piece) rather than lazily on first
      // buildMapGeometry() call -- this plugin's own textureLibrary_ is
      // already valid by construction time (StudioApp constructs it
      // before any plugin), so there's no ordering reason to defer it.
      materials_(core::ProceduralMaterialLibrary::generate(textureLibrary, allocator, device, cmdPool, queue)) {}

void TntWarsPlugin::buildMapGeometry(core::ECS& ecs) {
    clearMapGeometry(ecs);
    auto map = static_cast<tntwars::MapId>(mapIndex_);
    std::vector<tntwars::MapLayoutPiece> pieces = tntwars::buildMapLayout(map);

    for (const tntwars::MapLayoutPiece& piece : pieces) {
        // Real GPU mesh per piece -- deliberately not cached/reused across
        // pieces of different size (each piece has its own real
        // halfExtents), matching plugins::PrefabPlugin's own "one real
        // mesh per authored shape" convention rather than trying to share
        // handles across pieces that merely happen to both be boxes.
        core::Mesh mesh = piece.shape == tntwars::MapPieceShape::Box
                               ? core::Mesh::createBox(allocator_, device_, cmdPool_, queue_, piece.halfExtents)
                               : core::Mesh::createPlane(allocator_, device_, cmdPool_, queue_, piece.halfExtents.x,
                                                          piece.halfExtents.z);
        if (mesh.vertexBuffer() == VK_NULL_HANDLE) continue;
        uint32_t meshHandle = meshLibrary_->registerMesh(std::move(mesh));

        core::EntityId entity = ecs.createEntity(piece.name);
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            transform->position = piece.position;
        }
        auto& renderable = ecs.addComponent<core::Renderable>(entity);
        renderable.meshHandle = meshHandle;
        renderable.baseColor = glm::vec4(piece.color, 1.0f);
        // Sprint 16 ("Cinematic Graphics" Phase 2): real generated PBR
        // textures + material params layered on top of piece.color --
        // applyTo() is a real no-op for MapPieceMaterialKind::None (team
        // bases, water), which keep the flat baseColor exactly as before.
        materials_.applyTo(renderable, tntwars::classifyMapPieceMaterial(piece.name));

        // Real MeshSource so a scene save (SceneManager::saveScene(),
        // Sprint 13's WorldPackage capture) can rebuild this exact piece
        // on load -- same reasoning plugins::PrefabPlugin::spawnInstance()
        // already documents. Live collision bodies are deliberately not
        // attached here -- see MapLayout.hpp's own header comment on why
        // that's an existing, documented Studio-authoring scope boundary
        // (core::SceneFile.hpp), not something newly under-scoped by this
        // plugin.
        auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
        meshSource.kind = piece.shape == tntwars::MapPieceShape::Box ? core::MeshSourceKind::Box : core::MeshSourceKind::Plane;
        meshSource.params = piece.halfExtents;

        spawnedMapEntities_.push_back(entity);
    }
}

void TntWarsPlugin::clearMapGeometry(core::ECS& ecs) {
    for (core::EntityId entity : spawnedMapEntities_) {
        ecs.destroyEntity(entity);
    }
    spawnedMapEntities_.clear();
}

void TntWarsPlugin::drawMapEditingSection(core::ECS& ecs) {
    if (!ImGui::CollapsingHeader("Map Editing", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Combo("Map##edit", &mapIndex_, kMapNames, IM_ARRAYSIZE(kMapNames));
    ImGui::TextDisabled("Builds real, procedural static level geometry (ground/bases/cover/hazard markers)");
    ImGui::TextDisabled("into the live scene -- see MapLayout.hpp for the real per-map layout data.");

    if (ImGui::Button("Build Map Geometry")) buildMapGeometry(ecs);
    ImGui::SameLine();
    ImGui::BeginDisabled(spawnedMapEntities_.empty());
    if (ImGui::Button("Clear Map Geometry")) clearMapGeometry(ecs);
    ImGui::EndDisabled();
    ImGui::Text("%zu real spawned map entities", spawnedMapEntities_.size());
}

void TntWarsPlugin::drawClassTuningSection() {
    if (!ImGui::CollapsingHeader("Class Tuning", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Combo("Class##tune", &classIndex_, kClassNames, IM_ARRAYSIZE(kClassNames));
    auto classType = static_cast<tntwars::PlayerClassType>(classIndex_);
    tntwars::ClassTuningTable& tuning = session_->tntWarsMatch().classTuning();
    tntwars::ClassStats stats = tuning.statsFor(classType);

    bool changed = false;
    changed |= ImGui::SliderFloat("Max Health", &stats.maxHealth, 10.0f, 400.0f);
    changed |= ImGui::SliderFloat("Move Speed", &stats.moveSpeed, 0.5f, 20.0f);
    changed |= ImGui::SliderFloat("Primary Damage", &stats.primaryDamage, 1.0f, 100.0f);
    changed |= ImGui::SliderFloat("Primary Cooldown (s)", &stats.primaryCooldownSeconds, 0.1f, 5.0f);
    changed |= ImGui::SliderFloat("Ultimate Charge Required", &stats.ultimateChargeRequired, 10.0f, 500.0f);
    changed |= ImGui::SliderFloat("Charge / Damage Dealt", &stats.chargePerDamageDealt, 0.0f, 5.0f);
    changed |= ImGui::SliderFloat("Charge / Damage Taken", &stats.chargePerDamageTaken, 0.0f, 5.0f);
    if (changed) tuning.setStatsFor(classType, stats);

    float fireRate = stats.primaryCooldownSeconds > 0.0f ? 1.0f / stats.primaryCooldownSeconds : 0.0f;
    ImGui::TextDisabled("Real derived fire-rate cap this feeds TntWarsAntiCheat: %.2f shots/sec", fireRate);
    ImGui::Text("Ultimate: %s -- Primary: %s", tntwars::ultimateTypeName(tntwars::ultimateForClass(classType)),
                tntwars::projectileTypeName(tntwars::primaryProjectileForClass(classType)));

    if (ImGui::Button("Reset This Class To Default")) tuning.setStatsFor(classType, tntwars::classStatsFor(classType));
    ImGui::SameLine();
    if (ImGui::Button("Reset All Classes To Default")) tuning.resetToDefaults();
}

void TntWarsPlugin::drawMapTuningSection() {
    if (!ImGui::CollapsingHeader("Map Modifiers")) return;

    auto map = static_cast<tntwars::MapId>(mapIndex_);
    tntwars::MapTuningTable& tuning = session_->tntWarsMatch().mapTuning();
    tntwars::MapModifiers modifiers = tuning.modifiersFor(map);

    bool changed = false;
    changed |= ImGui::SliderFloat("Fog Density", &modifiers.fogDensity, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Heat Damage / sec", &modifiers.heatDamagePerSecond, 0.0f, 20.0f);
    changed |= ImGui::SliderFloat("Pressure Factor", &modifiers.pressureFactor, 0.25f, 1.5f);
    changed |= ImGui::Checkbox("Lava Hazard", &modifiers.hasLavaHazard);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Thruster Platforms", &modifiers.hasThrusterPlatforms);
    changed |= ImGui::Checkbox("Torpedo Stealth", &modifiers.hasTorpedoStealth);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Sonar Intercept", &modifiers.hasSonarIntercept);
    if (changed) tuning.setModifiersFor(map, modifiers);

    if (ImGui::Button("Reset This Map To Default")) tuning.setModifiersFor(map, tntwars::mapModifiersFor(map));
    ImGui::SameLine();
    if (ImGui::Button("Reset All Maps To Default")) tuning.resetToDefaults();
}

void TntWarsPlugin::drawMatchFlowSection() {
    if (!ImGui::CollapsingHeader("Match Flow (live server state)")) return;
    if (!session_->isServer()) {
        ImGui::TextDisabled("Host a server (Network Overlay) to drive real match-flow state.");
        return;
    }

    tntwars::TntWarsMatch& match = session_->tntWarsMatch();
    tntwars::MatchFlowController& flow = match.matchFlow();
    ImGui::Text("Phase: %s (%.1fs elapsed)", tntwars::matchPhaseName(flow.phase()), flow.phaseElapsedSeconds());

    if (ImGui::Button("Set Match Map")) match.setMap(static_cast<tntwars::MapId>(mapIndex_));
    ImGui::SameLine();
    ImGui::Text("Current match map: %s", tntwars::mapName(match.map()));

    static const tntwars::MatchPhase kPhaseOrder[] = {tntwars::MatchPhase::Lobby, tntwars::MatchPhase::ClassSelect,
                                                       tntwars::MatchPhase::InProgress, tntwars::MatchPhase::CinematicFinale,
                                                       tntwars::MatchPhase::Results};
    for (tntwars::MatchPhase phase : kPhaseOrder) {
        if (phase == flow.phase()) continue;
        if (!tntwars::MatchFlowController::isValidTransition(flow.phase(), phase)) continue;
        char label[64];
        std::snprintf(label, sizeof(label), "Advance -> %s", tntwars::matchPhaseName(phase));
        if (ImGui::Button(label)) flow.advanceTo(phase);
    }
}

void TntWarsPlugin::drawPanel(core::ECS& ecs, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    ImGui::TextDisabled(
        "Sprint 14 note: \"anime-style\"/\"cinematic\" here means real camera-choreography, particle-trigger,");
    ImGui::TextDisabled(
        "and network-synced-event SYSTEMS -- not hand-authored character art or shading. See CinematicSequence.hpp.");
    ImGui::Separator();

    drawMapEditingSection(ecs);
    drawClassTuningSection();
    drawMapTuningSection();
    drawMatchFlowSection();

    ImGui::End();
}

} // namespace engine::studio::plugins
