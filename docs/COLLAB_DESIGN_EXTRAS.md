# Collaboration design notes (extras)

Part of [COLLABORATION_DESIGN.md](COLLABORATION_DESIGN.md). None of this is built; the helper code
in `core/CollabPluginTools.hpp` (merge, leases, roles) is the tested starting point.

## Editing scripts together
- Scripts merge by line with a three-way merge. If both people change the same line, both versions are
  kept with conflict markers and the editor asks which to keep.
- A person who opens a script for editing takes a short lease (about a minute, renewed while typing).
  Others see "Ann is editing" and can still read.

## Chat in a session
Text chat only for the first version, with the same word filter as games. Voice chat is a separate
decision (it needs a server and moderation) and is not planned for v5.

## Hosting a session on your own PC
The person who starts the session is the host and everyone connects to their PC, like a LAN game.
No cloud service is needed. The session ends when the host closes it, so the host saves a copy first.
