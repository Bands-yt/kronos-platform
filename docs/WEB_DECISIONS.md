# Web decisions

These are decisions, not features. Kronos has no web player.

## Fallback renderer (928)
The renderer needs Vulkan 1.3. A browser build would need WebGPU. Decision: **no separate fallback
renderer**. If the browser has no WebGPU, show a page that says so and links to the download.

## Networking transport (932)
Browsers cannot send raw UDP. The game protocol (ENet) would need WebSocket or WebRTC data channels in
front of it. Decision: use a WebSocket relay first (simple, works everywhere), look at WebRTC only if the
latency matters for a real game.

## WebGPU probe (927)
Before a web build exists, a one-page probe can report `navigator.gpu`, the adapter limits and whether
timestamp queries exist. Not written yet.

## Size budget (929)
Aim for a first download under 30 MB without game data. `overBundleBudget` in
`core/AudioNetTools.hpp` lists the biggest files when a bundle goes over.
