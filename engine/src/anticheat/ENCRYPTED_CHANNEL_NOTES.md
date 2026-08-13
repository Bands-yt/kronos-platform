# Encrypted Network Channels -- Implementation Notes

Not implemented as code in this skeleton -- `net/ENetTransport` (see
`src/net/`) sends and receives plaintext ENet packets today. This is a note
on what closing that gap actually involves, per docs/ARCHITECTURE.md §11:

> Encrypted network channels -- DTLS over ENet now, QUIC's built-in TLS 1.3
> later (§4.2), so packet tampering/replay is at least as hard as breaking
> modern TLS.

## Why this belongs in the transport layer, not scattered elsewhere

Encryption here is about the *wire*, not the application payload --
InputCommand/DeltaSnapshot/RemoteEvent payloads (see `src/net/NetTypes.hpp`,
`RemoteEvent.hpp`) should stay unaware of it entirely. That's the same
"swapping the transport shouldn't touch call sites" boundary
`ENetTransport.hpp`'s own header comment already calls out for the ENet ->
QUIC migration -- encryption is one more thing that upgrade buys for free,
not a separate integration point.

## Two real paths, matching the two transports in docs/ARCHITECTURE.md §3

1. **Now (ENet)**: ENet itself has no built-in encryption. The standard
   approach is layering DTLS (e.g. via OpenSSL's DTLS support, or a
   focused library like tinydtls/mbedTLS) underneath ENet's packet I/O --
   encrypting each UDP datagram before ENet's reliability/sequencing layer
   sees it, and decrypting on receipt before handing it back to ENet. This
   is a real integration project (a third-party TLS library becomes a new
   dependency in `cmake/Dependencies.cmake`, and `ENetTransport::send`/
   `poll` need an encrypt/decrypt pass), not a small patch.
2. **Later (QUIC)**: QUIC bakes TLS 1.3 into the transport handshake by
   design -- migrating to QUIC (quiche or msquic, per §3) gets encryption
   "for free" as part of that upgrade rather than needing a separate DTLS
   integration. This is part of why §3 frames QUIC as the v2 upgrade rather
   than a nice-to-have: it collapses two separate pieces of work (better
   transport, encryption) into one.

## What encryption here does and doesn't protect against

Protects: casual packet sniffing/tampering/replay on the wire (public
Wi-Fi, a malicious router, a man-in-the-middle) -- "at least as hard as
breaking modern TLS" per the doc.

Does not protect against: a compromised *endpoint*. A cheat running on the
client machine sees the same plaintext InputCommand the game code does,
before it's ever encrypted for the wire. That's what server-authoritative
simulation (Principle 3) and the rest of `src/anticheat/` are for --
channel encryption and endpoint integrity are different problems with
different mitigations, and this note is scoped to the former only.
