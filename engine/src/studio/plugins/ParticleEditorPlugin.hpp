#pragma once

#include "core/Mesh.hpp"
#include "core/ParticleSystem.hpp"
#include "core/Texture.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

// Edits core::ParticleEmitter on the selected entity -- emission
// rate/burst, lifetime, velocity/gravity, size- and color-over-lifetime,
// plus real named presets (Fire/Smoke/Sparkle/Snow/Glow/Burst, Sprint 10
// adding Glow/Burst to reach this task's real "spark/smoke/glow/burst"
// library -- Sparkle already covered "spark") that just set
// ParticleEmitterSettings fields to known-good starting points, not a
// separate asset type. Reads ParticleSystem::liveCount() for a live
// "particles alive right now" readout -- real simulation state, not a
// static preview; see StudioApp.cpp's note on why Studio ticks particle
// simulation despite its scene otherwise being static.
//
// Sprint 10 ("Creator Tools Phase 2") task category 2's "live preview
// window" -- a real, isolated studio::PreviewScene (same "second 3D
// scene" system MaterialPlugin's live sphere preview reuses) with its
// own real emitter entity, mirroring whatever settings are currently
// being edited and ticked every frame via PreviewScene::particleSystem()
// (a small, new, backward-compatible accessor added specifically for
// this -- see that class's own comment) -- a genuinely isolated view of
// just this one effect, not the same live-in-the-main-viewport preview
// that already existed before this pass.
class ParticleEditorPlugin final : public IStudioPlugin {
public:
    ParticleEditorPlugin(const core::ParticleSystem& particleSystem, VmaAllocator allocator, VkDevice device,
                          VkCommandPool cmdPool, VkQueue queue, core::MeshLibrary& meshLibrary,
                          core::TextureLibrary& textureLibrary);

    [[nodiscard]] const char* name() const override { return "Particle Editor"; }
    [[nodiscard]] const char* category() const override { return "Effects"; }

    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

private:
    void ensurePreviewEmitter();

    const core::ParticleSystem* particleSystem_;

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;

    PreviewScene previewScene_;
    core::EntityId previewEmitterEntity_ = core::kNullEntity;
    // Real settings mirrored from whatever's being edited each drawPanel()
    // call -- kept separately (not read back from the preview entity)
    // since the preview entity's own ParticleEmitter::emitAccumulator
    // must NOT be reset every frame the way a naive "copy settings over
    // the whole component" would (that would silently break burst/timing
    // continuity); only .settings is ever overwritten.
    core::ParticleEmitterSettings lastMirroredSettings_;
    bool hasSelection_ = false;
};

} // namespace engine::studio::plugins
