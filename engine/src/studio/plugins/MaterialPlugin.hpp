#pragma once

#include <array>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ComputePbrPainter.hpp"
#include "core/EditableMesh.hpp"
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
    // Kronos ("Vulkan Compute PBR Painter" -- v0.4.0 Creator Suite): the
    // real "Compute Paint" section -- see core::ComputePbrPainter's own
    // class comment for why a real storage-capable texture is required
    // per slot (created here via "New Paintable Texture", not every
    // Texture::loadFromFile() result). The UV center is still shown and
    // adjustable numerically here (a live-viewport click, wired below in
    // drawPanel() via previewScene_.consumeClickRay(), writes into this
    // same paintUv_ and immediately stamps -- see handleViewportPickPaint()),
    // so the slider stays the way to fine-tune or replay a stamp without
    // needing to click again. core::pickTriangleUv() (the ray-triangle-UV
    // half of this feature) is real, wired, and independently proven
    // end-to-end against a real GPU stamp in tests/test_main.cpp's own
    // testComputePbrPainterUsesRealRayTriangleUvPickToLocateTheStamp().
    void drawComputePaintSection(core::Renderable& renderable);
    // The real stamp dispatch -- one core::ComputePbrPainter::stamp()
    // call per existing paintable slot at `uv`, using this plugin's own
    // current paint*_ color/radius/softness settings. Shared by both the
    // "Compute Paint" section's own Stamp button (uv = paintUv_, unchanged)
    // and handleViewportPickPaint()'s live click-to-paint (uv = the real
    // pickTriangleUv() hit), so a click paints exactly the same four real
    // PBR channels a manual Stamp click always has.
    void stampAllSlots(core::Renderable& renderable, glm::vec2 uv);
    // Kronos ("Vulkan Compute PBR Painter" -- live viewport picking):
    // consumes previewScene_'s pending click ray (if any -- see
    // studio::PreviewScene::consumeClickRay()'s own comment), ray-tests
    // it against previewPickMesh_ (the CPU-retained twin of
    // previewSphereMesh_ -- same radius/halfHeight, see that member's
    // comment), and on a real hit updates paintUv_ to the picked UV and
    // stamps immediately via stampAllSlots(). previewEntity_ carries no
    // Transform component (see ensurePreviewEntity()), so the sphere sits
    // at the identity transform and the ray from consumeClickRay() (already
    // in the preview scene's own world space) needs no further
    // world-to-local transform before testing against previewPickMesh_'s
    // local-space vertices.
    void handleViewportPickPaint(core::Renderable& renderable);

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
    // Kronos ("Vulkan Compute PBR Painter" -- live viewport picking): the
    // CPU-retained twin of previewSphereMesh_ above -- same
    // radius=0.5/halfHeight=0.0 real exact-UV-sphere geometry (via the
    // shared core::generateCapsuleGeometry() both Mesh::createCapsule()
    // and EditableMesh::createCapsule() call), kept here purely so
    // handleViewportPickPaint() has real triangle/UV data to ray-test
    // against -- previewSphereMesh_ itself is GPU-only once uploaded
    // (see core::Mesh's own class comment on why it retains no host-side
    // vertex data).
    core::EditableMesh previewPickMesh_ = core::EditableMesh::createCapsule(0.5f, 0.0f);

    // Real, on-demand compute painter -- lazily initialize()'d on first
    // real use (paintPainterReady_ tracks whether that real init
    // succeeded), not in the constructor: every other MaterialPlugin
    // call site (drawTextureSlot's plain Load/Clear) needs no live
    // compute pipeline at all, so a device that somehow can't build one
    // (see ComputePbrPainter::initialize()'s own real failure modes)
    // shouldn't block the rest of this plugin from working.
    core::ComputePbrPainter painter_;
    bool painterReady_ = false;
    std::string paintStatusMessage_;
    glm::vec2 paintUv_{0.5f, 0.5f};
    float paintRadius_ = 0.15f;
    float paintSoftness_ = 0.35f;
    glm::vec4 paintAlbedoColor_{1.0f, 1.0f, 1.0f, 1.0f};
    float paintRoughnessValue_ = 0.5f;
    float paintMetallicValue_ = 0.0f;
};

} // namespace engine::studio::plugins
