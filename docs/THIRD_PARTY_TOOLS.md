# Building tools for Kronos

You're welcome to build things that work with Kronos: launchers, wrappers,
mod and plugin managers, Studio plugins, performance overlays, asset tools
and integrations with other editors. This page says what's allowed and
which parts of Kronos you can rely on.

## What you may do

- **Launch the official Kronos apps** from your own program, using the
  launch options below, and show your own UI around them.
- **Download official releases** from the
  [Releases page](https://github.com/Bands-yt/kronos-platform/releases)
  for your users, and manage install folders, versions and update
  channels.
- **Build plugins and tools** with the [plugin kit](../LICENSING.md)
  (Apache 2.0): the C plugin API, the Luau plugin template and the
  script type definitions.
- **Sell your tool**, or keep its source closed. It's your project.

## What you may not do

- **Ship changed or repackaged Kronos apps.** Point users at the official
  downloads instead. (The engine, Player and Studio aren't open source;
  see [LICENSE](../LICENSE).)
- **Pretend to be Kronos.** Don't use the Kronos logo, and don't call your
  tool "Kronos" or "official". "for Kronos" or "works with Kronos" is
  fine, for example "Speedy Launcher for Kronos".
- **Get around the rules of online play.** That includes chat moderation,
  bans, rate limits and account security. Don't collect people's
  passwords or login tokens.
- **Run the official online services as your own**, or scrape them
  heavily.

## Launch options you can rely on

These won't be removed or changed without a major version bump and a
note in the release notes. Anything not listed here may change in any
release.

### Kronos Player (`engine_runtime`)

| Option | What it does |
|---|---|
| `--client <address> <port>` | Joins a game server |
| `--server` / `--port <port>` | Hosts a game server |
| `--game <slug>` | With `--server` or `--client`: the game to host or load |
| `--headless` | Runs without a window (servers, bots, tests) |
| `kronos://launch?game=<slug>` | Opens a game, like the website's Play button. Pass it as the only argument |

### Environment variables

| Variable | What it does |
|---|---|
| `KRONOS_SHELL_PAGE` | Opens the Player on a page: `discover`, `avatar`, `create`, `settings`, `directory` or `notifications` (unset: Home) |
| `KRONOS_SILENT_AUDIO=1` | Starts with all sound muted |
| `KRONOS_DATASTORE_DIR` | Folder where games save DataStore data |

### Plugin API

`engine/src/plugin/kronos_plugin.h` has its own version number. A plugin
built for version 1.x keeps working with every Kronos release that
supports 1.x, and Kronos refuses plugins built for a different major
version instead of crashing. See [PLUGIN_API.md](PLUGIN_API.md).

## Questions

Not sure if your idea is allowed?
[Open an issue](https://github.com/Bands-yt/kronos-platform/issues/new/choose)
and ask. Telling us what you're building also helps us avoid breaking it.

---

This document is licensed under the [Apache License 2.0](../LICENSES/Apache-2.0.txt).
