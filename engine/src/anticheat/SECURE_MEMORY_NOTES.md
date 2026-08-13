# Secure Memory Regions -- Implementation Notes

Not implemented as code in this skeleton. This is a note on *why* and what a
real implementation needs to weigh, per docs/ARCHITECTURE.md §11:

> Secure memory regions -- integrity-checked memory for the client-side
> detection logic itself. Framed honestly: raises the cost for casual cheat
> tools, does not stop a determined kernel-level attacker.

## What this actually means

The client-side anti-cheat detection code (behavioral telemetry collection,
exploit signature matching, the fingerprint collector) is itself a target:
a cheat tool that can read or patch that code can blind it. "Secure memory
regions" is the standard mitigation -- placing that code/data in memory
that's harder to read or tamper with from another process, and periodically
verifying it hasn't been altered.

## Concrete techniques a real implementation would use

- **Code-page integrity checks**: checksum (e.g. CRC32 or a keyed hash) over
  the compiled anti-cheat module's code pages at load time, re-verified
  periodically at runtime. A mismatch means something patched the binary in
  memory.
- **Memory protection flags**: mark sensitive data pages non-writable
  (`mprotect`/`VirtualProtect`) outside of the brief windows where the
  engine itself needs to update them.
- **Anti-debug / anti-injection checks**: detect an attached debugger
  (`ptrace` on Linux, `IsDebuggerPresent`/`CheckRemoteDebuggerPresent` on
  Windows) or unexpected loaded modules, as a signal (not a hard block --
  false positives against legitimate tools like profilers are a real risk).
- **Obfuscation**: makes static analysis of the detection logic more
  expensive, which raises the cost of building a bypass -- it does not
  prevent one.

## What this does *not* solve

All of the above run in **user-mode**, in the same process (or an
unprivileged watcher process) as the game itself. A cheat running with
**kernel-mode** privileges (a malicious/compromised driver) can read or
patch anything a user-mode process can, including memory the OS's own
protections would otherwise guard. Stopping that requires kernel-mode
anti-cheat of your own -- which docs/ARCHITECTURE.md §11 explicitly declines
to make the default posture:

> This platform does not default to kernel-level anti-cheat. Kernel drivers
> are largely Windows-only, sit in tension with the Linux-native goal, and
> carry real user-trust cost. The default posture is user-mode detection
> plus strong server authority everywhere. Kernel-level anti-cheat is
> available as an opt-in, per-experience escalation for competitive modes
> that explicitly choose it -- never platform-wide.

So: build the user-mode hardening above when this becomes a real
implementation task. Treat it as raising attacker cost, not as a security
boundary — the actual boundary against cheating is server authority
(Principle 3), which this whole subsystem is a secondary layer behind.
