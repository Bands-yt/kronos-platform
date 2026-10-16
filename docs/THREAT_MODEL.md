# Threat model (first version, 16 Oct 2026)

What Kronos protects, from whom, and where the lines are. This is a plan to test against, not a promise
that every row is solved.

## What matters
- The creator's computer: files, keys, accounts.
- Players' computers and accounts.
- Game state: scores, saves, anything that looks like money.
- The backend: accounts, published games, moderation data.

## Who we worry about
1. A malicious **game** someone downloads (scripts, assets).
2. A malicious **plugin** someone installs into Studio.
3. A malicious **player** or modified client in a multiplayer game.
4. A malicious **file** a creator opens (a crafted `.rbxl`, glTF, zip package).
5. Someone attacking the **backend** or a **server**.

## Where each is stopped

| Threat | Defence today | Known gap |
|---|---|---|
| Game scripts read files or run programs | Luau and Python VMs have no OS, file or network access; memory, time and stack limits | Python and Luau sandbox tests exist but aren't one named escape suite |
| Game scripts hang or eat memory | per-script and per-game budgets, wall-clock watchdog | none known |
| Plugin reads files or crashes Studio | capability bits; separate process on Linux | **no sandbox on Windows**; trusted-folder plugins run in-process with full rights |
| Client lies about the game | server is authoritative for Roblox remotes: arguments are validated, instances a client can't see become nil, NaN/inf rejected, ClickDetector and ProximityPrompt reach re-checked on the server, per-client remote rate budget | each game's own scripts must still validate what clients send; there is no anti-cheat beyond that |
| Crafted place or model file | parsers return errors instead of crashing in tests | **no fuzzing**; no resource limits proven for every parser |
| Zip package escaping its folder | package restore stays inside the game folder | needs a dedicated test for `..` paths |
| Chat abuse | profanity and threat filter, rate limits, moderation queue | the cloud classifier is not active |
| Stolen tokens in logs or crash reports | | **no redaction pass**; crash reports have no consent step |
| Backend attack | per-route auth, migrations reviewed by hand | no rate-limit dashboard, no staging/production split proven |

## Rules
- Never trust the client for anything that counts: score, items, currency, damage.
- A feature that touches multiplayer, plugins, imports, chat, files or accounts needs a note here and a test.
- New parsers get limits (size, depth) and a fuzz or malformed-input test before release.
