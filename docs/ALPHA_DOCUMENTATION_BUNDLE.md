# Kronos Alpha — Section 9: Alpha Documentation Bundle

Status of the [Alpha Completion Checklist](ALPHA_COMPLETION_CHECKLIST.md)'s
Section 9, and the final section of the approved Sections 1-9 scope.

## Audit finding

Plugin/Lua/networking/asset documentation already existed and was real
([PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md), [LUA_API.md](LUA_API.md),
[NETWORKING_UPGRADE.md](NETWORKING_UPGRADE.md),
[ASSET_PIPELINE.md](ASSET_PIPELINE.md), plus this pass's own
[PLUGIN_DEVELOPER_EXPERIENCE.md](PLUGIN_DEVELOPER_EXPERIENCE.md) and
[LUA_CREATOR_EXPERIENCE.md](LUA_CREATOR_EXPERIENCE.md)). What genuinely
didn't exist: a creator-facing entry point. `engine/README.md` is real
but is a 4,700-line, sprint-by-sprint engineering changelog — accurate
and valuable as engineering history, but not something a newcomer should
have to read to build and run the project. There was no repository-root
README, no focused quickstart, and no troubleshooting guide anywhere.

## Alpha README

**Built** — [/README.md](../README.md) (repository root, new): what
Kronos is, what's in the repo, and a real routing table to every other
doc — the actual entry point a newcomer or a downloaded-the-package
creator would land on first. Explicitly links to `engine/README.md` for
the full engineering history rather than duplicating or replacing it.

## Quickstart guide

**Built** — [QUICKSTART.md](QUICKSTART.md) (new): requirements, build,
run (including every real `engine_runtime` CLI launch mode — confirmed
against `main.cpp`'s actual argument parsing, not guessed), test, and
where the plugin/Lua/project templates live — condensed from the same
real, verified facts `engine/README.md`'s own "Requirements"/"Building"/
"Running"/"Testing" sections already established, kept current rather
than copied stale.

## Plugin / Lua / networking / asset pipeline docs

Already real (see Audit finding above) — cross-linked from the new
README's routing table rather than duplicated.

## Troubleshooting guide

**Built** — [TROUBLESHOOTING.md](TROUBLESHOOTING.md) (new): organized by
where in the workflow a failure shows up (configure / build / run / test
/ plugins / multiplayer), quoting the actual, real error strings this
codebase produces (`Renderer: volkInitialize failed...`, `Renderer: no
Vulkan-capable physical devices found.`, etc. — grepped from the real
source, not invented) so a creator can match what they're actually
seeing to a real, specific fix. Includes the real "moved binary /
packaged build" guidance directly tied to this pass's own
[ALPHA_PACKAGING.md](ALPHA_PACKAGING.md) fix.

## Summary

| Item | Status |
|---|---|
| Alpha README | Built (`/README.md`) |
| Quickstart guide | Built (`docs/QUICKSTART.md`) |
| Plugin docs | Already real |
| Lua docs | Already real |
| Networking docs | Already real |
| Asset pipeline docs | Already real |
| Troubleshooting guide | Built (`docs/TROUBLESHOOTING.md`) |
