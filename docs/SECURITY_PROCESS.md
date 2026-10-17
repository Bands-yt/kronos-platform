# Security process

How Kronos handles security work. See [THREAT_MODEL.md](THREAT_MODEL.md) for what the sandbox and servers defend
against, and [PROJECT_POLICY.md](PROJECT_POLICY.md) for how to report a problem.

## Reporting and disclosure (0847)

- Report by opening a GitHub issue that says only that you found a security problem and where. No details in public.
- The owner replies with a private way to send the details.
- Proposed (owner to confirm): first reply within 7 days, a fix or a plain explanation within 90 days. These are targets for one person's
  spare time, not promises.
- Reporters are credited if they want to be. We do not take legal action against people who test only their own
  machine and report in good faith.
- **Owner to decide:** a real security e-mail address (until then the issue route above is the process).

## Updating dependencies (0845)

1. Once a month, check each project in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for a new release or a
   published security fix. GitHub's "Security advisories" page for each repo is enough.
2. Update one dependency at a time in `engine/cmake/Dependencies.cmake`, with its exact version or commit.
3. Run the full tests, the smoke test and the compatibility score. Windows CI must pass too.
4. Note the change in the release notes. Security fixes are never left waiting for a release.
5. Never use "latest" or a moving branch; every dependency stays pinned.

## Before every release (0850)

- [ ] Tests, smoke test and compatibility score are green on Linux and Windows CI.
- [ ] `git diff` since the last release has no secrets, keys, tokens or personal paths.
- [ ] Dependency check done (above) and licences still listed.
- [ ] Plugin sandbox escape table in THREAT_MODEL.md still passes on both platforms.
- [ ] Servers: the remote-call checks (rate limit, argument sanity, visibility) pass in the tests.
- [ ] Downloads come from CI, not from a developer PC. Hashes are recorded in the release notes.
- [ ] Gemini or any other API key is on the servers only, never in a download.
- [ ] Crash reports still redact user name, home folder and e-mail.

## Permission prompts for outside tools (0849)

Anything that talks to something outside the player's PC must ask first and say what it sends:

- signing in, publishing and joining a game that is not on the same PC are the player's own actions;
- a game or plugin cannot make web requests today (`HttpService` requests always fail), so there is nothing to prompt;
- if a future feature allows it (web requests from games, voice, telemetry), it needs a plain prompt naming the
  destination, a "never allow" choice, and a setting to review what was allowed.

## Server authority (0840)

The server decides; the client only asks.

| Thing | Who decides |
|---|---|
| Where a player is and what they touched | The server's physics and scripts |
| Health, damage, kills, inventory, currency | Server scripts only. A client script can ask through a RemoteEvent, and the server checks |
| Remote calls | Server rate-limits per player; non-finite numbers and objects the sender can't see are refused |
| ClickDetector / ProximityPrompt use | Client sends the request; the server checks reach |
| DataStore | Server-side only |
| Chat | The word filter runs on each player's PC; a server-side check is planned (see CHAT_MODERATION.md) |

Writing a game: never trust a number or a name that came from a client. Check range, owner and cooldown on the
server. A script that only runs on a client can be edited by that player.
