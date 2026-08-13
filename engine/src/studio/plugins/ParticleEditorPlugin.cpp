#include "studio/plugins/ParticleEditorPlugin.hpp"

#include <imgui.h>

#include "core/Components.hpp"
#include "studio/ParticlePresets.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

ParticleEditorPlugin::ParticleEditorPlugin(const core::ParticleSystem& particleSystem, VmaAllocator allocator,
                                            VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                            core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary)
    : particleSystem_(&particleSystem), allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue),
      meshLibrary_(&meshLibrary), textureLibrary_(&textureLibrary) {}

void ParticleEditorPlugin::ensurePreviewEmitter() {
    if (previewEmitterEntity_ != core::kNullEntity) return;
    previewEmitterEntity_ = previewScene_.ecs().createEntity("ParticlePreview");
    previewScene_.ecs().addComponent<core::ParticleEmitter>(previewEmitterEntity_);
}

void ParticleEditorPlugin::update(float dt, core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    // Only meaningful while this panel is open and actually previewing
    // something -- ticking an emitter nobody can see would just be
    // wasted work, the same "no-op when not applicable" discipline
    // PreviewScene::render() itself already documents.
    if (!isOpen() || !hasSelection_) return;
    previewScene_.particleSystem().update(dt, previewScene_.ecs());
}

void ParticleEditorPlugin::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    previewScene_.render(cmd, renderer, *meshLibrary_, *textureLibrary_);
}

void ParticleEditorPlugin::shutdown(core::Renderer& renderer) { previewScene_.destroy(renderer, allocator_, device_); }

void ParticleEditorPlugin::drawPanel(core::ECS& ecs, core::EntityId selected,
                                      const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Particle Editor");
    drawPluginHeader("Particle Editor");

    ImGui::Text("Live particles: %zu / %zu", particleSystem_->liveCount(), core::ParticleSystem::kMaxParticles);
    ImGui::Separator();

    if (selected == core::kNullEntity) {
        hasSelection_ = false;
        ImGui::TextDisabled("Select an entity to add/edit a particle emitter.");
        drawPluginFooter();
        ImGui::End();
        return;
    }

    auto* emitter = ecs.tryGetComponent<core::ParticleEmitter>(selected);
    if (emitter == nullptr) {
        hasSelection_ = false;
        if (ImGui::Button("Add Particle Emitter To Selection")) {
            ecs.addComponent<core::ParticleEmitter>(selected);
        }
        drawPluginFooter();
        ImGui::End();
        return;
    }

    hasSelection_ = true;
    core::ParticleEmitterSettings& settings = emitter->settings;

    ImGui::Checkbox("Enabled", &settings.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Looping", &settings.looping);

    ImGui::TextUnformatted(settings.looping ? "Emission Rate (particles/sec)" : "Burst Count (one-shot, auto-disables after firing)");
    ImGui::DragFloat("##rate", &settings.emissionRate, 0.5f, 0.0f, 1000.0f);

    ImGui::SeparatorText("Lifetime");
    ImGui::DragFloat("Lifetime (s)", &settings.particleLifetime, 0.05f, 0.05f, 30.0f);
    ImGui::DragFloat("Lifetime Variance", &settings.particleLifetimeVariance, 0.02f, 0.0f, 10.0f);

    ImGui::SeparatorText("Velocity & Forces");
    ImGui::DragFloat3("Velocity Min", &settings.velocityMin.x, 0.05f);
    ImGui::DragFloat3("Velocity Max", &settings.velocityMax.x, 0.05f);
    ImGui::DragFloat3("Gravity", &settings.gravity.x, 0.05f);

    ImGui::SeparatorText("Size Over Lifetime");
    ImGui::DragFloat("Size Start", &settings.sizeStart, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Size End", &settings.sizeEnd, 0.01f, 0.0f, 10.0f);

    ImGui::SeparatorText("Color Over Lifetime");
    ImGui::ColorEdit4("Color Start", &settings.colorStart.x);
    ImGui::ColorEdit4("Color End", &settings.colorEnd.x);

    ImGui::SeparatorText("Presets");
    constexpr ParticlePresetId kPresetIds[] = {ParticlePresetId::Fire,   ParticlePresetId::Smoke, ParticlePresetId::Sparkle,
                                                ParticlePresetId::Snow,  ParticlePresetId::Glow,  ParticlePresetId::Burst};
    for (size_t i = 0; i < 6; ++i) {
        if (i % 3 != 0) ImGui::SameLine();
        if (ImGui::Button(particlePresetName(kPresetIds[i]))) applyParticlePreset(kPresetIds[i], settings);
    }

    // Live preview window (Sprint 10 task category 2) -- a real,
    // isolated preview scene mirroring the settings above onto its own
    // real emitter entity, ticked in update() every frame this panel is
    // open. Only .settings is copied (never the whole component) so the
    // preview entity's own emitAccumulator/burst-fired state keeps
    // evolving continuously rather than resetting every frame.
    ImGui::SeparatorText("Live Preview");
    ensurePreviewEmitter();
    if (auto* previewEmitter = previewScene_.ecs().tryGetComponent<core::ParticleEmitter>(previewEmitterEntity_)) {
        previewEmitter->settings = settings;
    }
    ImGui::BeginChild("##particle_preview", ImVec2(0.0f, 220.0f), true);
    previewScene_.drawAndHandleOrbit();
    ImGui::EndChild();

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
