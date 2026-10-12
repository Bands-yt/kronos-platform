#include "tntwars/MapMaterialApply.hpp"

#include <glm/glm.hpp>

namespace engine::tntwars {

void applyMapMaterial(const core::ProceduralMaterialLibrary& library, core::Renderable& renderable, MapPieceMaterialKind kind) {
    const core::PbrTextureSet* set = nullptr;
    if (kind != MapPieceMaterialKind::None) renderable.baseColor = glm::vec4(1.0f);
    switch (kind) {
        case MapPieceMaterialKind::Stone: set = &library.stone; renderable.metallic = 0.02f; renderable.roughness = 0.9f; break;
        case MapPieceMaterialKind::Metal: set = &library.metal; renderable.metallic = 0.9f; renderable.roughness = 0.35f; break;
        case MapPieceMaterialKind::Ground: set = &library.ground; renderable.metallic = 0.0f; renderable.roughness = 0.88f; break;
        case MapPieceMaterialKind::Lava:
            set = &library.lava;
            renderable.metallic = 0.0f;
            renderable.roughness = 0.55f;
            renderable.emissiveColor = glm::vec3(1.0f, 0.45f, 0.12f);
            renderable.emissiveIntensity = 0.15f;
            break;
        case MapPieceMaterialKind::Mud: set = &library.mud; renderable.metallic = 0.0f; renderable.roughness = 0.75f; break;
        case MapPieceMaterialKind::Wood: set = &library.wood; renderable.metallic = 0.0f; renderable.roughness = 0.65f; break;
        case MapPieceMaterialKind::Coral: set = &library.coral; renderable.metallic = 0.0f; renderable.roughness = 0.75f; break;
        case MapPieceMaterialKind::Sand: set = &library.sand; renderable.metallic = 0.0f; renderable.roughness = 0.9f; break;
        case MapPieceMaterialKind::None: return;
    }
    renderable.albedoTexture = set->albedo;
    renderable.normalTexture = set->normal;
    renderable.metallicTexture = set->metallic;
    renderable.roughnessTexture = set->roughness;
    renderable.aoTexture = set->ao;
}

} // namespace engine::tntwars
