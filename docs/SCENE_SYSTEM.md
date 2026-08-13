# Kronos Platform — Phase 2: Scene System (Hierarchy)

Status of the Alpha Roadmap's Phase 5 ("Scene + Entity System"), scoped to
the **hierarchy** half of that section — see
[ALPHA_ROADMAP.md](ALPHA_ROADMAP.md) for the full roadmap and its
recommended order (which splits this roadmap section into "Scene system"
then "Component system" — the latter, adding real `Light`/`Script`/
`Network` components, is tracked separately as Phase 3, not covered here).

All work below was verified with a full rebuild (`engine_runtime`,
`studio`, `engine_tests`) and the full test suite green after every change
(9342/9342 checks passing, up from 9304 — 38 new tests added: 13 for
`core::hierarchy` itself, 1 end-to-end scene round-trip covering both the
text-file and live-ECS layers).

## Audit finding

A pre-implementation audit (the step this phase started from) confirmed
this engine had **zero** prior Parent/Children concept anywhere: no
component, no field, no serialization, no UI. `Renderable`/`Transform`
were always drawn/read in isolation. `core::Prefab`'s multi-piece assets
and `tntwars`/`miningsim` "visual" structs (`DungeonVisual`,
`ZoneVisualTheme`, etc.) all group *entities* they spawn, but only in
transient C++ vectors owned by whoever spawned them — never as a real,
queryable ECS relationship an Explorer tree, a serialized scene, or a
gizmo could walk. This was a real, complete gap, not a partial feature.

## 1. Core hierarchy component + API

**Built — `core::Hierarchy` + `core::hierarchy::*`.**
`core/Components.hpp` gained a new, minimal component:

```cpp
struct Hierarchy {
    EntityId parent = kNullEntity;
    std::vector<EntityId> children;
};
```

Never mutated directly — every real caller goes through
`core/Hierarchy.{hpp,cpp}`'s five functions:

- `setParent()` — real, validated reparent. Rejects self-parenting and
  cycles (walks the prospective new parent's own ancestor chain before
  committing); moves the child out of any previous parent's children list
  first; correctly *reuses* an existing `Hierarchy` component on either
  side instead of blindly `addComponent()`-ing over it (a real footgun
  caught during implementation — `ECS::addComponent()` is
  `emplace_or_replace`, so a naive call would silently wipe an existing
  parent's other children).
- `unparent()` — real, honest detach. Preserves the child's current
  *world* position/rotation/scale by decomposing its world matrix before
  detaching and writing that back as its new local `Transform`, so nothing
  visually jumps. A no-op (not an error) if the entity is already a root.
- `destroyEntityRecursive()` — real cascade delete, depth-first so no
  child is destroyed while its own children still reference it.
  Deliberately a separate, opt-in function from `ECS::destroyEntity()`
  (unchanged, zero regression for every caller with no idea hierarchy
  exists).
- `computeWorldMatrix()` — walks the real parent chain multiplying local
  `Transform::matrix()`s together. An entity with no `Hierarchy` component
  (or `parent == kNullEntity`) returns exactly `transform.matrix()`,
  byte-identical to reading `Transform` directly — the reason every
  pre-existing entity's rendering is completely unaffected by this
  feature's mere existence.
- `isAncestorOf()` — the real ancestor-chain walk `setParent()`'s own
  cycle check uses, exposed for callers (a future drag-and-drop UI) that
  need the same check before attempting a reparent.

All five are bounded by a defensive `kMaxWalkDepth = 256` recursion guard
— `setParent()` already prevents cycles at insertion time, so this is
defense-in-depth against a corrupted/hand-edited scene file, not a claim
that cycles are otherwise reachable.

**Tests (13 new, `testHierarchy*` in `tests/test_main.cpp`):** two-sided
setParent/unparent relationship building, self-parent rejection, cycle
rejection (3-level chain), reparenting an already-parented child, sibling
preservation across the `emplace_or_replace` footgun above, root/1-level/
3-level `computeWorldMatrix()` correctness, unparent world-position
preservation, unparent-on-root no-op, recursive cascade delete (whole
subtree + parent-list detachment), `isAncestorOf()` across a real chain.

## 2. Renderer wiring

**Fixed — all 4 draw call sites now use `computeWorldMatrix()`.**
`core/Renderer.cpp`'s shadow pass, opaque pass, glass/water pass, and
skinned-entity pass all previously read `transform.matrix()` directly —
meaning a parented entity would have been tracked in the ECS but never
actually rendered at its correct world position. Every site now calls
`hierarchy::computeWorldMatrix(ecs, entity)` instead — byte-identical
output for the overwhelming majority of entities (anything without a
`Hierarchy` component), real world-space correctness for parented ones.

## 3. Explorer panel — real parent-child tree

**Rewritten — `studio/panels/ExplorerPanel.{hpp,cpp}`.** This panel's own
header comment had stood for a while promising "once the Instance layer
exists, this becomes a tree walk over Instance/Parent relationships
instead of a flat entity list — a data-source swap, not a rewrite of the
panel's selection/drawing logic." That promise is now kept:

- Rows are still grouped into the same category "folders"
  (Terrain/Prop/Physics/Economy/Navigation/Other), but only entities with
  no real parent become a category's own root rows; every parented entity
  is drawn nested under its real parent's row instead, recursively,
  regardless of which category the parent (or the child itself)
  classifies into — Parent always wins over category grouping, matching
  every real editor's tree behavior.
- Every row (leaf or branch) now goes through `ImGui::TreeNodeEx` instead
  of the old leaf-only `Selectable`, so leaf and branch siblings share the
  same arrow-column indentation instead of leaves sitting one indent
  narrower.
- Multi-select (ctrl-click toggle, shift-click range-select) is preserved
  exactly — the flat `rows` index used for shift-click is now a full
  pre-order DFS flattening built once per frame, independent of any
  node's current collapsed state, matching the same "hidden rows still
  count toward the index" invariant the old collapsed-*category* case
  already relied on, now extended to collapsed *tree nodes* too.
- **Real drag-and-drop reparenting**: dragging any row onto another calls
  `core::hierarchy::setParent()` directly (its own cycle/self-parent
  rejection applies unchanged). Each row's right-click context menu offers
  **Unparent**, disabled when the entity is already a root.
- New Studio console bindings, `world.setParent(child, parent)` and
  `world.unparent(entity)` (`studio/StudioEcsScriptApi.cpp`), give the
  same real API a live-script/console path — deliberately scoped to just
  these two (not `world.createEntity()` et al, which is real, flagged-
  missing Phase 7 scope, not this phase's).

**Honest verification gap:** the tree-drawing/drag-drop/context-menu code
is ImGui-only logic with no headless test surface (matching this
codebase's existing convention — see `ExplorerPanel::animateOpenAmount()`
being pulled out specifically so *that* piece could get real test
coverage). It was verified by a full clean compile of `studio` and code
review; a live visual capture was attempted but not completed this pass
(the desktop was in active use for unrelated work at the time) — flagged
here explicitly rather than silently claimed as visually verified.

## 4. Scene file persistence

**Extended — `core::SceneFile`/`core::SceneEntityRecord`.** A new
`parentName` field (empty = root), serialized as an optional `PARENT
<name>` line right after `ENTITY <name>` — same "field just doesn't
appear when absent" convention `hasRenderable`/`hasMeshSource`/
`hasParticleEmitter` already use, and same name-based (not `EntityId`-
based) identification the whole file format already relies on for every
other cross-reference, since `EntityId` is a per-session handle.

**`studio::SceneManager`** now captures/rebuilds it end-to-end:

- `captureScene()`: if an entity has a real parent, looks up that
  parent's own `Name` and stores it. A parent with no name (or an empty
  one) can't be re-identified on load either, so this honestly falls back
  to saving the entity as a root, with a real `fprintf` warning — no
  silent data loss disguised as success.
- `loadScene()`: a genuine two-pass rebuild. Pass one creates every entity
  (parent references can point forward in the file, so every entity must
  exist before any can be resolved). Pass two resolves each `parentName`
  through a real name → `EntityId` map and calls `core::hierarchy::
  setParent()` — not a raw `Hierarchy` write, so a corrupted/hand-edited
  scene file still gets the same cycle/self-parent rejection a live
  `setParent()` call would apply. An unresolvable parent name logs a real
  warning and the entity loads as a root instead of silently failing.

**Test:** `testSceneFileHierarchyRoundTrip` — a real 3-level chain
(Root → Child → Grandchild) saved via `SceneManager::saveScene()`,
verified at the text-file level (`PARENT` lines present/absent
correctly) and again end-to-end by reloading into a fresh `ECS` via
`SceneManager::loadScene()` and confirming the live `Hierarchy`
components come back pointing at the correct re-created entities.

## Summary

| Area | Status |
|---|---|
| Core `Hierarchy` component + `core::hierarchy` API | Built, 13 real tests |
| Renderer world-transform propagation | Fixed at all 4 real draw call sites |
| Explorer panel real tree (grouping, multi-select, drag-drop, unparent) | Rewritten; visual capture not completed this pass (flagged) |
| Scene file save/load | Extended with real `parentName` round-trip, 1 end-to-end test |
| `Light`/`Script`/`Network` components | Not started — tracked as Phase 3 |

Phase 2 (hierarchy) is functionally complete for the scope above. The one
deliberately incomplete item (a live visual capture of the new Explorer
tree) is called out rather than silently claimed. Phase 3 (the roadmap's
"Component system" — `Light`/`Script`/`Network`) has not started.
