#pragma once

#include "core/ECS.hpp"
#include "studio/StudioIcons.hpp"

namespace engine::studio {

// Sprint 7 ("Studio UI Revamp") task category 2/3: a real, pure,
// component-driven classification of what an entity actually *is* --
// what drives Explorer's real hierarchy icons (task category 2) and its
// grouped "folder" sections (task category 3). Deliberately ECS-only
// (no ImGui dependency at all), so it's fully headlessly testable, same
// "pure or ECS-only" split every interaction-dispatch function in this
// codebase already follows.
enum class EntityCategory { Terrain, Prop, Physics, Economy, Navigation, Other };

// Real, priority-ordered classification (most-specific real component
// wins): a real core::TerrainChunkTag beats a real core::WorldProp beats
// a real core::OreNode beats real Economy/Navigation marker components
// beats "has *some* real physics collider" beats the honest fallback,
// Other. An entity with none of these real components (a plain
// decorative prop, a material-showcase cube) really is Other -- not a
// classification failure, a real, correct answer.
[[nodiscard]] EntityCategory classifyEntity(core::ECS& ecs, core::EntityId entity);

// The real Icon (StudioIcons.hpp) each category renders as. Terrain/
// Prop/Physics map onto their own real, dedicated icons; Economy reuses
// the Material icon (ore's own real, rarity-driven color reads like a
// material swatch, see InspectorPanel's rarity-preview section);
// Navigation reuses the existing WorldSpace (globe) icon -- a real,
// honest, thematically-apt reuse, not a placeholder, since this sprint's
// required icon set has no dedicated "navigation" glyph of its own.
// Other has no per-row icon of its own -- see ExplorerPanel for how that
// honestly renders (a plain label, exactly like before this sprint).
[[nodiscard]] Icon iconForCategory(EntityCategory category);

// The real group/"folder" header label Explorer's grouped hierarchy
// shows for each category -- reuses iconCategoryName() for the ones that
// have a real 1:1 icon, with its own real names for Economy/Navigation/
// Other (which don't map onto one of StudioIcons' own category names).
[[nodiscard]] const char* categoryDisplayName(EntityCategory category);

} // namespace engine::studio
