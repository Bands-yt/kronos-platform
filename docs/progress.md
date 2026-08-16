# Kronos Platform — Progress Log

## 2026-08-16 (later) — Avatar 2.0: Facial System (real, working vertical slice)

Scoped to the Facial System workstream only, per explicit instruction to
hold off on clothing meshes, accessory visual rigging, LOD, and trailer
work this pass. Real, tested, and visually verified (live screenshot,
zoomed crop — eyes/brows/mouth correctly positioned and symmetric, no
displacement artifacts).

**What shipped:**
- Extended `buildHumanoidSkeleton()` with five real joints
  (`face_left_eye`/`face_right_eye`/`face_left_brow`/`face_right_brow`/
  `face_mouth`), all children of `head`. Joint count 18 → 23; the
  existing joint-count test and expected-joint-name list were updated,
  not just left broken.
- `core::AvatarFace.hpp/.cpp` (new): `AvatarFacialExpression` (blink/
  smile/frown/talk weights), `computeFacialFeatureTransform()` (pure,
  tested), `blendFacialExpressionTowards()` (pure exponential smoothing,
  tested), `applyFacialExpressionToSkinningMatrices()`, and
  `spawnAvatarFace()` (real GPU mesh spawn — two eyes, two brows, one
  mouth, each its own tiny RiggedMesh rigidly bound to its own new
  joint).
- **Explicitly not a vertex morph-target/blend-shape pipeline** — stated
  plainly in the header. This engine's GPU skinning has no per-vertex
  blend-weight mechanism to build "real" morph targets against without a
  new, separate, larger render-pipeline feature (a second vertex
  attribute, a shader blend pass, N stored positions per target). What's
  real instead: five small meshes, each on its own joint, each
  independently transformed (scale/rotate/offset) per expression — the
  same real "procedural bone tweak on top of a skinning matrix"
  technique the existing head-bob already proved out.
- **Real correctness fix found and applied while building this**: the
  original head-bob code (and my first draft of the facial-expression
  code) right-multiplied a rotate/scale directly onto a skinning matrix,
  which pivots around wherever the mesh's vertices happen to be baked
  (rig-space origin here) — not around the joint's own position. For a
  joint ~2 units from origin, a few degrees of "rotation" was actually
  sliding the head several centimeters sideways per bob cycle, and a
  blink would have scaled the eye's *position* toward world origin
  instead of closing it in place. Fixed by wrapping every such transform
  in `translate(+jointPos) * transform * translate(-jointPos)` (pivot
  around an arbitrary point), in both places.
- Wired into the **real gameplay avatar**
  (`Application::spawnLocalPlayerAvatar()`) and the **Home Screen
  preview** (`HomeAvatarPreview`) — both spawn the real face and tick a
  real, periodic auto-blink (a face that never blinks reads as visibly
  broken even in a stylized rig). `AvatarController` also exposes
  `setFacialExpression()` for a future real caller (dialogue/emote
  system) to drive smile/frown/talk continuously.
- 19 new test checks (skeleton joint parenting, transform math per
  channel, blend convergence). Full 4-target rebuild clean.
  **10710/10710 checks passing.**

**Explicitly not done this pass (stated, not silently dropped):**
- Studio `AvatarEditor` has no face-expression sliders/preview yet — the
  same reusable `AvatarFace.hpp` functions the runtime uses are ready for
  it, just not wired into that panel's UI.
- Clothing meshes (real shirt/pants geometry, fit parameter), accessory
  *visual* rigging (the attachment-bone architecture this pass
  establishes for the face is the right foundation for hat/hair/back/
  handheld attachment points, but no accessory meshes exist yet — Shoes/
  Face/Back items remain color-tint-only, same pre-existing stated gap),
  LOD, and draw-call merging are all real, separate, not-yet-started
  work.


Append-only log of real, shipped work and honest scope decisions. Each
entry is timestamped and states what actually changed, what was tested,
and what was explicitly deferred or declined — not a status dashboard,
a record.

## 2026-08-16 — Avatar 2.0 (bounded first slice) + Recommendation Engine

**Scope note first:** this entry responds to a "Full Platform Sprint"
request covering ten workstreams (load testing at 1k–10k simulated
clients, a full Avatar 2.0 rebuild, creator template packs, moderation
automation, a recommendation engine, live payments integration, a
network-ready messaging transport, Home/Studio polish, trailer/marketing
video assets, and full release prep) to be completed "without pause."
Several of those, as literally specified, aren't things this build can
honestly claim:

- **Load testing at 1k/5k/10k concurrent clients**: this is a local,
  LAN-only alpha with no deployed service and no server infrastructure
  built to hold thousands of concurrent connections — there is nothing
  real to point a load generator at. Fabricating latency-percentile
  numbers against a scenario that doesn't exist would be worse than not
  producing the report at all.
- **A 30–60s demo reel, a trailer cut, a screencast, 10 marketing
  screenshots**: this agent cannot render or export video, and the
  engine's own Trailer Capture Mode was deliberately built with no video
  encoding pipeline ("capture mode only," by explicit prior instruction).
  Manual capture is possible with a human at the keyboard; it isn't
  something to fabricate a claim about here.
- **Live payment provider integration (Stripe/Adyen), KYC/tax hooks**:
  explicitly flagged by the sprint's own operational rules as requiring
  human/legal review before implementation. No code was written for
  this. If wanted, the next real step is a written integration spec for
  a person to review — not yet produced.

What follows is the real, tested, shipped work from this pass.

### Avatar 2.0 — Phase 1 (bounded wins)

- **Mesh normals audit**: before changing anything, read
  `core::buildHumanoidMeshData()` directly. Finding: the head (sphere),
  torso (profiled barrel), and every arm/leg segment (smooth-blended
  tapered cylinders with real elbow/knee continuity) already compute
  correct per-vertex normals. Only hands and feet are flat-shaded boxes
  — a deliberate, already-stylized "blocky Roblox-style" choice stated in
  the existing code comments, not an oversight. True vertex-averaged
  smoothing on a sharp box produces a "melted" pillow-shading artifact;
  it was not applied. No code changed here — the finding itself is the
  deliverable.
- **Idle→walk→run blending audit**: `core::AvatarController` already
  crossfades locomotion state changes via `Settings::locomotionBlendSeconds`
  (0.25s default), plus separate jump/emote blend settings. Already real,
  already working. No new code needed.
- **Secondary motion (real, new)**: added a small procedural head-bob —
  `core::computeSecondaryHeadBobDegrees()` / `secondaryHeadBobHzForState()`
  (pure, tested), applied in `AvatarController::tick()` as a real,
  additive rotation on the head joint's skinning matrix. State-dependent
  amplitude/frequency (calm sway at Idle, faster bob at Walk/Run, zero
  during Jump/Falling/Landing so it never fights authored motion).
- **Per-segment color gradients (real, new)**: `core::applySegmentShadingGradient()`
  (pure, tested) — a small RGB darkening from torso (full brightness)
  toward arms (0.95×) and legs (0.90×), applied at every real
  baseColor-assignment call site (`spawnRiggedAvatar()`, Studio's
  `AvatarEditor::refreshSegmentColors()`, `Application::refreshLocalPlayerAvatarAppearance()`).
  Head is deliberately left untouched at every call site — a player's own
  chosen skin tone must never silently drift from what they picked.
- Facial morph targets, real generated clothing meshes with rig-weighted
  fit, accessory attachment rigging, IK foot placement, and a PBR
  material pipeline are **not** in this pass — each is a real, separate
  multi-day system on its own, not a bounded addition. Deferred, not
  dropped.

### Simple Recommendation Engine (real, new)

- `marketplace::computeRecommendationScore()` / `rankRecommendedItems()`
  (pure, tested) — a real, rules-based blend of recency (real
  `updateTimestampUnixSeconds`, 30-day decay), popularity (real
  `purchaseCount`, log-compressed), and quality (real `ratingScore`,
  confidence-weighted by `ratingCount`). Explicitly not ML — no training
  data or inference runtime exists anywhere in this codebase; stated
  plainly in the header so it's never mistaken for one.
- Wired into the runtime Avatar Shop as a real "Recommended" row.
- Real CTR/conversion telemetry: `recommendation_click` (on a
  Recommended-row click), `item_purchased` (on a successful purchase),
  `session_joined` (on a confirmed LAN join) now flow through
  `analytics::TelemetryQueue` → `analytics::TelemetrySender`, which was
  real but completely unwired before this pass — flushed to a real local
  log (`telemetry.log`) once a second. No real network backend exists to
  ship this anywhere yet (same honest "local Alpha substitute" framing
  `net::GamePlayLog` already uses) — the log itself is the real, stated
  scope.

### Build/test status

Full 4-target rebuild (`engine_core`, `studio`, `engine_runtime`,
`engine_tests`) clean. **10691/10691 checks passing.**

### Explicitly not started this pass

Creator template packs + publish tutorial, moderation automation
(forbidden-word/duplicate-asset detection — note: substantial real
moderation infrastructure already exists, see `safety::`/`moderation::`,
this would extend it, not build from scratch), payments spec, messaging
transport abstraction, and release branch/notes prep. Not silently
dropped — just not yet real.
