#include "tntwars/MapMaterial.hpp"

namespace engine::tntwars {

MapPieceMaterialKind classifyMapPieceMaterial(const std::string& pieceName) {
    // Order matters only where a name could plausibly match more than one
    // substring -- none currently do (see MapLayout.cpp's real piece
    // names), but Lava/Stone are checked before the broader Metal/Ground
    // buckets on principle, so a future, more specifically-named piece
    // fails toward the more specific real material, not the catch-all.
    if (pieceName.find("Lava") != std::string::npos) return MapPieceMaterialKind::Lava;
    // Kronos Phase 3: checked before the broader Rock/Island->Stone bucket
    // below on the same "more specific real material wins" principle this
    // function's own header comment already establishes.
    if (pieceName.find("Coral") != std::string::npos || pieceName.find("Reef") != std::string::npos) {
        return MapPieceMaterialKind::Coral;
    }
    if (pieceName.find("Sand") != std::string::npos || pieceName.find("Beach") != std::string::npos) {
        return MapPieceMaterialKind::Sand;
    }
    if (pieceName.find("Mud") != std::string::npos || pieceName.find("Trench") != std::string::npos) {
        return MapPieceMaterialKind::Mud;
    }
    if (pieceName.find("Wood") != std::string::npos || pieceName.find("Bunker") != std::string::npos ||
        pieceName.find("Fort") != std::string::npos) {
        return MapPieceMaterialKind::Wood;
    }
    if (pieceName.find("Rock") != std::string::npos || pieceName.find("Island") != std::string::npos) {
        return MapPieceMaterialKind::Stone;
    }
    if (pieceName.find("Ground") != std::string::npos) return MapPieceMaterialKind::Ground;
    // Cover (Trenches), Platform (Sky Platforms) -- real built structures,
    // not natural terrain, so brushed metal rather than stone/ground.
    if (pieceName.find("Cover") != std::string::npos || pieceName.find("Platform") != std::string::npos) {
        return MapPieceMaterialKind::Metal;
    }
    // Team bases and Water: no generated material fits either (a base is
    // a flat team-colored marker, water needs real refraction/reflection
    // this engine doesn't have -- see core::ProceduralMaterialLibrary's
    // own header comment) -- real, honest "leave it alone."
    return MapPieceMaterialKind::None;
}

} // namespace engine::tntwars
