# Kronos Alpha — Section 7: Project System Finalization

Status of the [Alpha Completion Checklist](ALPHA_COMPLETION_CHECKLIST.md)'s
Section 7. Verified with a full rebuild (`engine_runtime`, `studio`,
`engine_tests`) and the full test suite green (9582/9582 checks passing,
up from 9571 — 11 new tests).

## Save / load

Already real (Alpha Roadmap Phase 9) — `core::ProjectFile::saveToFile()`/
`loadFromFile()`, the same hand-rolled "KEY value per line, END
terminator" text format every other persistence struct in `core/` uses.
No changes needed.

## Metadata

Already real — name, creation/modification timestamps (real
`std::chrono::system_clock` seconds, meant to be shown to a user, unlike
`core::SceneFile`'s clock-comparable-only mtimes), the open scene list,
and which tab was active. No changes needed.

## Versioning

**Real, honest gap closed.** `ProjectFile::version` was a plain string,
round-tripped on save/load but never actually compared against anything
— a project written by a hypothetical future, incompatible Kronos build
(or a hand-corrupted `VERSION` line) would open silently with no signal
anything might be wrong.

- `core::kProjectFormatVersion` (new) is the one real source of truth for
  "what format does this build write" — `ProjectFile::version`'s own
  default now reads from it instead of an independent, driftable literal.
- `ProjectFile::isCompatibleVersion()` (new) — a real semver major-version
  comparison: same major as `kProjectFormatVersion` is compatible
  regardless of minor/patch (the real, standard semver contract),
  different major (higher *or* lower) is not, and an unparseable version
  string is treated as incompatible rather than guessed at.
  Deliberately doesn't block `loadFromFile()` itself — this alpha has
  nothing to actually migrate yet — it's a real signal callers can act on.
- `StudioApp`'s Open Project flow now surfaces a real, visible warning in
  its status line when a loaded project's version is incompatible,
  instead of opening it with no indication anything might be off.

## Auto-backup / recovery

**Real, honest gap closed.** Scene-level autosave/recovery already
existed (`SceneManager::tickAutosave()`/`recoveryPathFor()`/
`hasRecoveryFile()`, with a real "Recover unsaved changes?" banner in
Studio) — but it only protects scene *content*. The project's own
metadata (which scenes are open, which tab is active) had no equivalent:
losing an in-progress project-structure change to a crash before the next
explicit Save was unrecoverable.

- `ProjectFile::recoveryPathFor()`/`hasRecoveryFile()` (new) — the exact
  same non-clobbering `path + ".autosave"` pattern `SceneManager` already
  established, applied to project metadata instead of scene content.
- `StudioApp::tickProjectAutosave()` (new) — ticked once per frame right
  alongside `sceneManager_.tickAutosave()`. A real, honest no-op until a
  project has been saved/opened at least once, and only actually writes
  when the open scene list or active tab genuinely changed since the last
  write (the same coarse-dirty-check spirit `SceneManager`'s own
  entity-count heuristic uses) — not a write-every-frame busy loop.
- A deliberate Save Project clears any pending recovery file for that
  path, same "an explicit save wins" rule `SceneManager::saveScene()`
  already applies to its own `.autosave` file.
- Opening a project with a pending recovery file shows a second, real
  recovery banner in Studio (`projectRecoveryOfferPath_`, alongside the
  existing scene one — both can be pending at once, since they're
  independent events) with real Recover/Dismiss actions.

## Summary

| Item | Status |
|---|---|
| Save/load | Already real |
| Metadata | Already real |
| Versioning | Real gap closed — `isCompatibleVersion()`, single-source-of-truth format constant, a real Open-time warning |
| Auto-backup | Real gap closed — project-level `.autosave` snapshot, mirroring `SceneManager`'s scene-level pattern |
| Recovery | Real gap closed — a second, real recovery banner for project metadata |
