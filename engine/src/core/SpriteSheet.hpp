#pragma once

#include <string>
#include <vector>

namespace engine::core {

struct SpriteRect {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;
};

// Cuts a sheet into a grid of cells, left to right then top to bottom.
// Cells that would run past the image edge are dropped. Returns {} for bad input.
inline std::vector<SpriteRect> sliceSpriteSheet(int imageW, int imageH, int cellW, int cellH, const std::string& baseName,
                                                 int marginX = 0, int marginY = 0, int spacingX = 0, int spacingY = 0) {
    std::vector<SpriteRect> out;
    if (imageW <= 0 || imageH <= 0 || cellW <= 0 || cellH <= 0 || marginX < 0 || marginY < 0 || spacingX < 0 ||
        spacingY < 0) {
        return out;
    }
    int index = 0;
    for (int y = marginY; y + cellH <= imageH; y += cellH + spacingY) {
        for (int x = marginX; x + cellW <= imageW; x += cellW + spacingX) {
            out.push_back({baseName + "_" + std::to_string(index++), x, y, cellW, cellH});
        }
    }
    return out;
}

// Same, but from a column/row count.
inline std::vector<SpriteRect> sliceSpriteSheetByCount(int imageW, int imageH, int columns, int rows,
                                                       const std::string& baseName) {
    if (columns <= 0 || rows <= 0) return {};
    return sliceSpriteSheet(imageW, imageH, imageW / columns, imageH / rows, baseName);
}

} // namespace engine::core
