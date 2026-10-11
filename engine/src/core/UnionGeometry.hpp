#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/ECS.hpp"

// A UnionOperation's pieces, drawn and collided by hidden helper entities so
// scripts still see one part.
namespace engine::core::unions {

// In the union's local space, as fractions of its size.
struct Piece {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 size{1.0f};
    glm::vec3 color{0.64f};
    int shape = 1; // Roblox PartType: 0 Ball, 1 Block, 2 Cylinder
};

// On each helper entity. Helpers are never saved; sync() rebuilds them.
struct UnionPiece {
    EntityId owner = kNullEntity;
    glm::vec3 color{0.64f};
    int shape = 1;
};

// Saved on the union's InstanceInfo.
inline constexpr const char* kPiecesProperty = "@UnionPieces";

[[nodiscard]] std::string encodePieces(const std::vector<Piece>& pieces);
[[nodiscard]] std::vector<Piece> decodePieces(const std::string& text);

[[nodiscard]] bool hasPieces(ECS& ecs, EntityId e);
[[nodiscard]] bool isPiece(ECS& ecs, EntityId e);
// The union a helper belongs to, or `e` itself.
[[nodiscard]] EntityId owner(ECS& ecs, EntityId e);

// Builds missing helpers, removes stale ones, and copies the union's
// transparency and visibility onto them. Cheap enough to call every frame.
void sync(ECS& ecs);

} // namespace engine::core::unions
