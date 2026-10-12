#pragma once

#include "core/Components.hpp"
#include "core/ProceduralMaterials.hpp"
#include "tntwars/MapMaterial.hpp"

namespace engine::tntwars {

// Gives a map piece its generated PBR textures and surface feel.
void applyMapMaterial(const core::ProceduralMaterialLibrary& library, core::Renderable& renderable,
                      MapPieceMaterialKind kind);

} // namespace engine::tntwars
