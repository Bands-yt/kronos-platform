# Collaboration: design notes

Team Create is **not built**. These are the rules decided in advance, so the first version doesn't have to invent
them under pressure. See [PROJECT_POLICY.md](PROJECT_POLICY.md) for the fallback that exists today.

## Shared undo history (686)

- Each person has their own undo stack. Ctrl+Z only undoes your own last change.
- If someone else changed the same property after you, undo asks first: "Anna changed this since. Undo anyway?"
- Undo never deletes work another person built on top of yours without that question.

## Invites, revocation and expiry (689)

- An invite is a link with an expiry (default 7 days) and a role (viewer, editor).
- The owner can revoke an invite or remove a person at any time; their session ends within a few seconds and the
  edits they already made stay.
- Roles are checked on the host, not in the editor.

## Follow a collaborator's camera (692)

- Click a name to follow their view; any mouse or key movement of your own stops following.
- Following is read-only: you see what they see and cannot change their camera.

## Simulating bad networks (696)

Use the existing sender-side loss and delay settings of the network layer (the "Network:" menu in Studio) to
test collaboration over bad connections once it exists. Targets: still usable at 150 ms and 3% loss.

## Disabled fallback (699)

If collaboration is off or unreachable, Studio works exactly as today for one person. Nothing needs a connection to start.
