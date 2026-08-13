# Kronos Alpha — Section 2: Editor UX Polish

Status of the [Alpha Completion Checklist](ALPHA_COMPLETION_CHECKLIST.md)'s
Section 2. Verified with a full rebuild (`studio`, `engine_tests`) green
throughout — ImGui interaction code has no pure logic to unit-test (the
same honest ceiling this codebase already applies to `ExplorerPanel::draw()`
and every other `drawPanel()`), so this section's work is verified by
clean compilation and code review rather than new test count.

## Audit findings

- **Smooth panel docking**: already real (`ImGui::DockBuilder`-based
  layout, `StudioApp.cpp`). No known issue found; no action needed.
- **Consistent icons**: `CreatorAssetBrowserPlugin`'s five pre-existing
  categories (Props/Materials/Particles/Terrain) use a `"[Type] Name"`
  text-prefix convention with no per-row `StudioIcons` glyph. The two
  categories added this alpha (Imported Assets, and the two new
  `NetworkOverlayPlugin`/`PluginBrowserPlugin` sections) match that same
  convention — no new inconsistency introduced.
- **Clean hierarchy visuals**: `ExplorerPanel`'s real tree (built in the
  Alpha Roadmap's Scene System phase) already has real icons, indentation,
  selection highlighting, and a real per-category collapse-fade animation.
  Reviewed, no defect found.

## Real work

**Tooltips**: added `helpMarker()`/explanatory text to the controls this
alpha's own new work introduced without one — the Asset Browser's Import
button (re-import-replaces-not-duplicates behavior), the Plugin Browser's
directory scan field (relative-path + snapshot-not-live-watch behavior),
and the Network Overlay's Recent Servers header (history, not live
discovery). Not swept across every existing control in Studio — `Plugin
Chrome.hpp`'s own header comment already documents this as a standing,
explicit scope boundary ("applied to... a representative sample... not
swept across every plugin file"), inherited rather than silently ignored.

**Real drag-and-drop in the Asset Browser** (the one genuinely new
feature this section asked for, since none existed before): material
preset rows in `CreatorAssetBrowserPlugin::drawMaterialEntries()` are now
real `ImGui::BeginDragDropSource()` sources, carrying an index into the
same `kMaterialPresets` table the existing "Use" button already reads
from — no second, could-drift copy of the preset data crosses the drag
boundary. `ExplorerPanel`'s own entity rows are the real drop target: a
second `AcceptDragDropPayload("ASSET_MATERIAL_PRESET")` call inside the
tree's existing drag-drop-target scope (alongside the pre-existing
reparenting payload), applying the preset directly to that row's
`Renderable` if it has one — a real, honest no-op otherwise.

## Summary

| Item | Status |
|---|---|
| Smooth panel docking | Already real, confirmed |
| Consistent icons | Already consistent, confirmed; new sections match the existing convention |
| Clear Logger-based error messages | See [CRASH_TELEMETRY.md](CRASH_TELEMETRY.md) (Section 3) for the full account |
| Tooltips for all UI elements | Added to this alpha's own new controls; not swept across all of Studio (a standing, pre-existing scope boundary) |
| Stable drag-and-drop in Asset Browser | Built — real material-preset drag source + Explorer drop target |
| Clean hierarchy visuals | Already clean, confirmed |
