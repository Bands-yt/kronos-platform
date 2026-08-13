#include "miningsim/Dungeon.hpp"

#include <algorithm>

namespace engine::miningsim {

bool dungeonRoomsOverlap(const DungeonRoom& a, const DungeonRoom& b) {
    bool separateX = a.gridPosition.x + a.gridSize.x <= b.gridPosition.x || b.gridPosition.x + b.gridSize.x <= a.gridPosition.x;
    bool separateY = a.gridPosition.y + a.gridSize.y <= b.gridPosition.y || b.gridPosition.y + b.gridSize.y <= a.gridPosition.y;
    return !(separateX || separateY);
}

DungeonLayout generateDungeonLayout(RarityTier rarity, std::mt19937& rng) {
    DungeonLayout layout;
    layout.rarity = rarity;

    std::uniform_int_distribution<int> roomCountDist(4, kMaxDungeonRooms);
    int targetRoomCount = roomCountDist(rng);
    std::uniform_int_distribution<int> sizeDist(kMinDungeonRoomSize, kMaxDungeonRoomSize);

    for (int i = 0; i < targetRoomCount; ++i) {
        for (int attempt = 0; attempt < kMaxRoomPlacementAttempts; ++attempt) {
            DungeonRoom candidate;
            candidate.gridSize = glm::ivec2(sizeDist(rng), sizeDist(rng));
            int maxX = std::max(0, kDungeonGridWidth - candidate.gridSize.x);
            int maxY = std::max(0, kDungeonGridHeight - candidate.gridSize.y);
            std::uniform_int_distribution<int> xDist(0, maxX);
            std::uniform_int_distribution<int> yDist(0, maxY);
            candidate.gridPosition = glm::ivec2(xDist(rng), yDist(rng));

            bool collides = false;
            for (const DungeonRoom& existing : layout.rooms) {
                if (dungeonRoomsOverlap(candidate, existing)) {
                    collides = true;
                    break;
                }
            }
            if (!collides) {
                layout.rooms.push_back(candidate);
                break;
            }
        }
    }

    // Real, sequential connection -- every room after the first
    // real-connects back to its immediate predecessor's own center.
    for (size_t i = 1; i < layout.rooms.size(); ++i) {
        glm::ivec2 fromCenter = layout.rooms[i - 1].gridPosition + layout.rooms[i - 1].gridSize / 2;
        glm::ivec2 toCenter = layout.rooms[i].gridPosition + layout.rooms[i].gridSize / 2;
        layout.corridors.push_back({fromCenter, toCenter});
    }

    return layout;
}

bool canGenerateDungeonInStartingZone(RarityTier rarity) { return isDungeonRarityAllowedInStartingZone(rarity); }

} // namespace engine::miningsim
