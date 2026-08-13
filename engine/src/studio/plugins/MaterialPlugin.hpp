#pragma once

#include <array>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Mesh.hpp"
#include "core/Texture.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

// Quick material editor over core::Renderable's inline PBR parameters
// (baseColor/metallic/roughness -- see Components.hpp's note on why there
// is no separate material asset/library for this to point at yet) plus
// real texture slots (albedo/normal/metallic/roughness) backed by
// core::Texture/TextureLibrary -- the first place in Studio that loads an
// actual image file rather than generating pixels procedurally. Original
// implementation, inspired by the same "material manager" utility
// category Roblox Studio and various third-party plugins ship, not a
// copy of either -- see AnimatorPlugin.hpp's header note on why that
// distinction matters for this engine specifically.
class MaterialPlugin final : public IStudioPlugin {
public:
    MaterialPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                   core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary);

    [[nodiscard]] const char* name() const override { return "Material Editor"; }
    [[nodiscard]] const char* category() const override { return "Utility"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Sprint 10 ("Creator Tools Phase 2") task category 1's "live preview
    // sphere" -- same "raw pointer into what pluginManager_ owns, plus a
    // real renderPreview()/shutdown() pair" pattern AvatarPreviewer/
    // CataloguePanel already established for their own studio::PreviewScene.
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

private:
    enum class Slot { Albedo, Normal, Metallic, Roughness, AO };
    static constexpr size_t kSlotCount = 5;

    void drawTextureSlot(Slot slot, const char* label, core::Renderable& renderable);
    void ensurePreviewEntity();

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;

    // One path buffer per slot -- keyed by Slot's underlying value, not
    // per-entity (the text field just holds whatever was last typed,
    // same as any other single-target property editor in this Studio).
    std::array<char[256], kSlotCount> pathBuffers_{};

    // Live preview sphere -- a real studio::PreviewScene (its own tiny
    // ECS/orbit-camera/offscreen target, the same "second 3D scene"
    // system AvatarPreviewer/CataloguePanel already reuse), holding one
    // entity whose Renderable is copied from whatever's currently being
    // edited every drawPanel() call, so orbiting the sphere always shows
    // the *current* live values, not a stale snapshot. The sphere mesh
    // itself is `Mesh::createCapsule(radius, halfHeight=0.0f)` -- with no
    // cylinder body between the two hemispheres, that's a real, exact UV
    // sphere, not an approximation, reusing the existing capsule
    // generator rather than a second, near-duplicate sphere-mesh function.
    PreviewScene previewScene_;
    uint32_t previewSphereMesh_ = core::Renderable::kInvalidHandle;
    core::EntityId previewEntity_ = core::kNullEntity;
};

} // namespace engine::studio::plugins
