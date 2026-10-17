# Collaboration research (0676 to 0682)

Nothing here is built. Building blocks that are tested: three-way merge, leases, roles, a session log, a
last-writer-wins register (`core/CollabPluginTools.hpp`) and chunked snapshots (`core/ProductTools.hpp`).

- **Session with invite (0676):** host-based, like a LAN game: the host's Studio is the authority and
  people join with a code. No cloud service.
- **Authoritative service (0677):** later, only if hosting moves to a server.
- **Selection outlines (0679):** each person gets a colour (`presenceColour`); their selection is drawn with it.
- **Transform sync (0680):** send changed objects only, at 10 to 20 Hz, with the last-writer-wins register
  per property.
- **Conflicts (0681):** same property edited by two people: the later stamp wins and the other person sees a
  small notice. Text (scripts) uses the line merge.
- **CRDT (0682):** a CRDT for the object tree (add, move, delete) is the hard part; a prototype should try a
  tree with unique ids and per-child ordering keys. Three weeks to do properly.
