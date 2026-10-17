# What Kronos is, who it's for, and the rules we hold ourselves to

Written 17 Oct 2026. This page answers the "what are we promising?" questions in one place. Where
something is a decision the owner still has to make, it says so. For what works today see
[MATURITY.md](MATURITY.md); for what's coming see [ROADMAP.md](ROADMAP.md).

## In one sentence

Kronos is a game platform like Roblox, with a fresh coat of paint: a Player to play games, a Studio to
build them, and a way to bring simple Roblox places over.

## Who it's for, and who it isn't

For:
- people who want to make and share simple 3D games without installing a large engine
- Roblox creators who want to try their places somewhere else
- plugin and tool makers (the plugin kit is open source)

Not for (yet):
- big commercial games that need consoles, mobile or the web
- creators who need a finished Roblox clone: big Roblox games use things Kronos doesn't have
  ([what doesn't work yet](../README.md#what-doesnt-work-yet))
- anyone who needs a stable, unchanging API: everything is still pre-beta

Non-goals: we are not rewriting the ECS or the game loop, not building our own distributed build system,
and not promising features before they are built and tested.

## What runs on your PC and what doesn't

| | Where it happens |
|---|---|
| The Player, Studio, the game engine, your scripts and saves | On your PC |
| Playing a game with friends | One player's PC or a game server runs the game; the others connect to it |
| Accounts, publishing, the game catalogue | The Kronos backend (a server) |
| Chat moderation | Local word filters on your PC. Gemini, an online AI check, is **off** everywhere today and would only run on game servers |
| Crash reports | Written to your PC and not sent anywhere. Your user name, home folder and e-mail are removed from the text |
| Cloud hosting of games, matchmaking, Team Create, web player | Not built |

Kronos doesn't upload crash reports. Anything else that leaves your PC is something you chose: signing in, publishing, or joining an online game.

## Computer requirements (what we've tested)

- A graphics card and driver with **Vulkan 1.3**. Studio's welcome panel shows your card and says
  whether it is good to go.
- Windows 11 (64-bit, tested in a virtual machine and on one real laptop) or Linux (64-bit). Windows 10 is not tested. macOS is not supported.
- No minimum RAM or disk figure has been measured yet. Software rendering (a test setup with no real graphics
  card) runs the Player but crashed on one Windows test setup; real graphics cards have worked.

## What we promise about old projects

| Level | Meaning | Applies to |
|---|---|---|
| Keeps working | We aim to migrate it for you, with a test | Saved scenes and projects: the scene format carries a version number so older files can be read |
| Best effort | We try, and tell you what changed | Roblox imports (the report lists what couldn't be converted) |
| No promise yet | May change between versions | Plugin ABI before version 1.0, script APIs marked preview, the network protocol |

Plugin ABI changes bump a major version and are listed in the release notes. The launch options promised
to tool makers in [THIRD_PARTY_TOOLS.md](THIRD_PARTY_TOOLS.md) are not changed without a major version.

## Feature labels and when something counts as "stable"

The labels (stable, preview, experimental) are in [MATURITY.md](MATURITY.md). A feature is **stable** when
it has shipped in a release, has automatic tests, has been used on a clean Windows and Linux machine, and has
a doc page. Until then it is preview or experimental. Nothing is called stable today.

## How we call a bug serious

| Severity | Example | What we do |
|---|---|---|
| Blocker | The download won't start, data is lost, a security hole | Fixed before any release; a release is held for it |
| Major | A common feature is broken, with no workaround | Fixed in the next release |
| Minor | Wrong but there is a workaround | Fixed when convenient, listed in known limits |
| Cosmetic | Looks off, still works | Collected, fixed in polish passes |

A failing test on `4.1-creators` blocks a release. A test that fails only sometimes is written down and
fixed, not ignored.

## Security: how to report a problem

Please don't post an exploit in a public issue. Open a GitHub issue that only says you found a security
problem and where (no details), and the owner will reply with a private way to send them. A
proper security contact address and a response time are a decision the owner still has to make.
The sandbox rules and what is tested are in [THREAT_MODEL.md](THREAT_MODEL.md).

## How decisions are made

Kronos has one owner. Anyone can propose a change in an issue or a pull request ([CONTRIBUTING.md](../CONTRIBUTING.md)).
The owner decides. Big changes (a new file format, a network protocol change, a new licence) are written
down in `docs/` first. The roadmap and the 1,000-item plan ([V5_PLAN.md](V5_PLAN.md)) are the shared to-do lists.

## Licence

The plugin kit is open source (Apache 2.0). Everything else is all rights reserved. Details and the exact
file list are in [../LICENSING.md](../LICENSING.md). Whether to open more of the engine (the advisor suggested
MPL 2.0) is an open decision for the owner.

## Money

Nothing in Kronos costs money today and nothing is sold. There is no cloud bill because there is no cloud
hosting. If hosting is added later, the policy is: show an estimate before anything is started, never start
paid services automatically, and let people host their own servers for free.

## Telemetry

Kronos sends no usage data today. If a future version asks to collect first-launch performance numbers
(graphics card name, frame time, load time), the rules are fixed now: it is **off until you say yes**, the
question is shown once in plain words with an example of exactly what is sent, nothing identifies you, you can
turn it off any time, and the data is not sold or shared. Crash reports stay on your PC (see above).

## Working together (collaboration)

Team Create (several people editing one place live) is not built and is not planned for v5. Until then, the
fallback is: one person edits, and you share work by exporting a package or handing over the project folder.
Studio never opens a shared session by itself, so nothing breaks when collaboration is missing.

## Kronos compared with other tools

These are honest, limited comparisons, not claims of being better.

| | Kronos | Roblox Studio | Unity |
|---|---|---|---|
| Where games run | Kronos Player on Windows and Linux | Roblox (their app and servers) | Anywhere you build for, including mobile and consoles |
| Scripting | Luau (Roblox-style), Python (preview), native plugins in C or Rust | Luau | C# |
| Roblox places | Imports simple ones | Native | No |
| Hosting and players | You or friends host; no public player base | Roblox's servers and millions of players | You provide it |
| Graphics, tools, assets | A young Vulkan renderer, few tools, few assets | Mature | Very mature, huge asset store |
| Cost | Free to try, no marketplace | Free, Roblox takes a share of sales | Free tier, paid plans |

The honest summary: Kronos is early. It is a good fit for trying a small game or a Roblox place somewhere
new, and a poor fit for a team that needs a proven engine today.

## What counts as success

Not downloads or hype. Success is **creators who come back**: people who make or import a game, play it
with a friend, and open Studio again the next week. We measure it by the first-15-minutes test with real
testers ([BACKLOG_AUDIT.md](BACKLOG_AUDIT.md)) and by how many of the test places the Compatibility Lab can play.
