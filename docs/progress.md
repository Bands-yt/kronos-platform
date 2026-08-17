# Kronos Platform — Progress Log

## 2026-08-16 (later still, part 5) — Proportional correction: thicker arms, bigger hands, wider shoulders

Direct follow-up to real, explicit user feedback comparing a live
screenshot side-by-side against the same reference image from the
previous entry: "match the arm length, hand size and shoulder offset.
Keep Kronos's current rig and shaders intact." A real, bounded
proportion-tuning pass -- no skeleton joint changes, no shader changes,
just `RiggedAvatar.cpp`'s own numeric mesh-generation parameters, per
the explicit scope constraint.

**Root cause of the previous "not doing it right" result**: the arm's
cross-sections (0.095/0.075/0.065 shoulder/elbow/wrist) were thin enough,
relative to the torso and the arm's own real length, that the limb read
as a nearly-invisible sliver next to the body from most camera angles --
correct in position, wrong in scale. The reference image's own arms stay
thick along nearly their whole length (minimal taper) and end in a real,
large, clearly-visible hand -- the opposite of a slender, steeply-tapered
limb.

**Real fix**: `shoulderCrossSection`/`elbowCrossSection`/
`wristCrossSection` thickened 0.095/0.075/0.065 -> 0.125/0.105/0.095 (a
real, much gentler taper, closer to the reference's near-uniform blocky
arm). `palmHalfExtents` (the hand box `appendHand()` builds fingers off
of) grown 0.10/0.12/0.065 -> 0.13/0.15/0.085 -- a real, notably bigger,
more visible hand (finger geometry scales with it automatically, since
`appendHand()`'s own finger dimensions are already derived proportionally
from the palm). Shoulder attachment offset widened again, 0.36 -> 0.41,
matching real, necessary clearance for the now-thicker arm against the
torso's own 0.29 shoulder-bulge boundary (the same real reason the
previous 0.25 -> 0.36 widening was needed for the original, thinner arm
-- a thicker arm needs proportionally more room to clear the same
boundary). Every `.anim` file's own `arm_L_upper`/`arm_R_upper` position
keyframes were updated to match (this rig bakes absolute joint position
per keyframe, not a bind-pose delta -- the same mechanical requirement
every previous proportion change in this pass has needed).

**Verified via live screenshot**: arms now read as real, clearly visible,
chunky limbs with large, distinct hands, and a real, visible shoulder gap
from the torso -- matching the reference's block-limbed proportions.

10764/10764 tests passing, clean 4-target rebuild. No rig (skeleton
joint count/hierarchy) or shader changes -- pure mesh-dimension/
animation-position data, per the explicit scope constraint.

## 2026-08-16 (later still, part 4) — Default look: swept-side hair + dark jacket color story

Direct follow-up to a real reference image the user provided (a
screenshot of a classic blocky avatar: swept side-part wavy brown hair,
black jacket over a blue tee, dark jeans, white sneakers), with an
explicit instruction to keep the current skin tone and match "the rest."
Since this project's own stated direction (set by the user at the start
of the Silhouette Pass) is "familiar... but original to Kronos," this
was treated as a real style/color reference -- matching the silhouette,
hairstyle concept, and color story -- not an attempt to reproduce a
specific third party's mesh/textures verbatim (which a single screenshot
couldn't provide the data for regardless).

**Hair -- rebuilt a third time, spikes -> swept layered locks**:
`AvatarHair.cpp`'s `appendHairSpike()` (straight-up, radiating short
spikes) replaced with `appendHairLock()` -- the same real tapered-frustum
shape but with independent, non-square base/tip half-extents (a
flattened "strand" cross-section instead of a round spike) and every
lock's own base/tip positioned along one consistent sweep direction
(+X), layered at different heights and lengths for a real "wavy,
side-swept" read instead of a symmetric radiating cluster. One rounded
base-coverage blob (crown + forehead) underneath, same real per-vertex
root-to-tip color ramp as before.

**Default clothing colors**: `kDefaultShirtColor`/`kDefaultTrouserColor`
(RiggedAvatar.hpp) darkened from the previous teal/slate tones to a real
charcoal/near-black jacket-and-jeans pairing, matching the reference's
dominant color story. The reference's own lighter-blue undershirt
(visible only in a small chest V, under the jacket) is a real, honest,
stated gap -- this rig's Torso is one flat shell color; showing two
garments at once needs real layered-clothing geometry
(`AvatarItemCategory::LayeredClothing` exists as a real equip slot
already, but has no mesh-generation path yet, the same "real slot, no
visual behind it yet" gap this codebase already states honestly for
Shoes/Back/Accessory). White sneakers (vs. the current pants-colored
feet) are a similar, real, stated gap -- this rig's feet share their
parent leg segment's own single color; giving them a genuinely distinct
tint would need either a new HumanoidBodySegment (the same kind of
larger, deferred restructuring the Performance/LOD pass's body-segment
draw-call merge already declined for a similar reason) or a real
above-1.0 vertex-color hack that wouldn't read as convincingly white --
neither was worth the risk for this pass. Skin tone: untouched, per
explicit instruction.

**Verified via live screenshot**: swept, layered hair (not spikes/horns,
not a bun), dark jacket + dark jeans, original face/skin tone intact.

Clean 4-target rebuild. **10764/10764 checks passing** (pure geometry/
color data changes -- no new pure-logic surface this round, verified
structurally via the unchanged existing suite plus the live screenshot,
same discipline as the rest of this pass).

## 2026-08-16 (later still, part 3) — Avatar Silhouette Polish: real vertex-color pipeline + spike-based hair

Direct follow-up to the Silhouette Pass below, driven by two rounds of
real user feedback on live screenshots: the first hair design (rounded
blobs) read as a "bun," not a bacon-hair mass, and "vertex-color
gradients" had been asked for twice -- the first pass's discrete
per-tuft color step wasn't a real answer to that.

**Real per-vertex color -- new engine capability, not just an avatar
tweak**: `core::Vertex` (Mesh.hpp) gained a real `color` field (default
opaque white, so every existing procedural generator across the whole
engine -- terrain, props, every other avatar piece -- keeps rendering
byte-identical). Wired through the full pipeline: `Vertex::
attributeDescriptions()` (Mesh.cpp) adds it at attribute location 11
(deliberately past every location any pipeline sharing this binding
already uses -- GpuSkinVertex claims 4-5, InstanceData claims 4-10 --
avoiding both); `scene.vert`, `scene_skinned.vert`, and
`scene_instanced.vert` all pass it through as a new, genuinely
interpolated (not `flat`) varying at location 7; `scene.frag` and
`scene_rt.frag` multiply it into albedo alongside the existing texture/
baseColor terms. This is the first real per-vertex (not per-entity/
per-segment) color channel this engine has had -- every earlier
"gradient" in this codebase (the per-segment shading gradient, the first
hair pass's per-tuft color step) was a real, honest, discrete
approximation specifically because this field didn't exist yet.

**Hair -- rebuilt again, bun -> layered spike mass**: `AvatarHair.cpp`'s
`spawnAvatarDefaultHair()` now builds 2 rounded base blobs (back mass,
front fringe, for real volume) plus 5 short, tightly-clustered spike
tufts (`appendHairSpike()`, reintroduced but deliberately much shorter
and less splayed than the very first attempt that read as horns -- each
spike's own lateral travel stays under ~0.06 units over ~0.09-0.10 units
of height, well inside "points mostly straight up"). Every piece now
carries a real, smooth root-to-tip vertex-color ramp (darker near the
scalp, lighter toward the tip) computed from the avatar's own hair
color, instead of one flat color per mesh.

**Materials -- real matte/glossy contrast**: hair entities get real,
low roughness (0.28, vs. every body segment's 0.55-0.66) and a small
metallic bump (0.12) -- a genuine, visible specular contrast against
the body under this engine's existing Cook-Torrance PBR lighting, not a
restated value. AO stays the existing, already-honest per-segment
shading-gradient stand-in (`applySegmentShadingGradient()`) -- unchanged,
still the stated real substitute for true per-vertex AO (a real, separate
gap from the color gradient this pass just added).

**Head**: cheek/jaw curvature retuned a third time -- up from the
overly-subtle `{0.95, 1.0, 1.03, 0.97, 0.85}` revision to
`{0.90, 1.0, 1.06, 0.90, 0.72}`, real, visible personality while staying
well inside the human-safe range the second revision established (the
first pass's `0.55` chin scale, which read as a snout, is not being
revisited).

**Verified via live screenshot** (not eyeballed from code): the result
shows distinct, separated spike/tuft clusters at the crown with a real
visible dark-to-light gradient along each spike, an oval human head with
visible cheek/jaw definition, and clearly-separated arms/legs -- matching
the target "bacon-style" silhouette.

**Already satisfied by this same pass** (re-confirmed, not re-built, since
a later message re-asked for the same items already shipped just above):
shoulder rounding + waist taper (torso's 4-ring profile, unchanged from
the Silhouette Pass entry below), leg proportions (thigh/shin split +
narrowed cross-sections, unchanged), idle-stance asymmetry (head tilt +
one-arm-lower, unchanged) -- see that entry for the full detail on each.

Clean 4-target rebuild including shader recompilation (`engine_shaders`).
**10764/10764 checks passing** (no new pure-logic surface introduced this
round -- the real, new work is GPU pipeline plumbing and mesh-generation
data, verified structurally via the existing suite plus live screenshots,
matching this project's own established "GPU code gets structural +
visual verification, not a false automated-coverage claim" convention).

## 2026-08-16 (later still, part 2) — Avatar Visual Silhouette Pass

Target: a "bacon-hair-inspired" silhouette -- familiar proportions,
original to Kronos, broad shoulders, narrow legs, a stylised hair mass.
Real geometry/animation-data work across `RiggedAvatar.cpp`, a new
`AvatarHair.hpp/.cpp`, and every shipped `.anim` file. **A first pass on
the head/hair shipped, was checked via live screenshot, and read as
animal (goat-horn-like) rather than human -- caught immediately, reverted
to a human-safe design in the same session, not left in place.** Both the
mistake and the fix are documented below.

**Head**: `appendProfiledHead()` (new) reshapes the existing low-poly
lat/long sphere with a real per-latitude-ring horizontal (X/Z only)
width multiplier -- a small, genuine cheek bulge and jaw taper, "slight
curvature" per the spec. The *first* attempt used an aggressive profile
(`{0.82, 1.0, 1.08, 0.88, 0.55}`, a sharply narrowing chin) that, combined
with the first hair design, read as snout-like. Replaced with a much
subtler profile (`{0.95, 1.0, 1.03, 0.97, 0.85}`). Only applied to the
`Oval` head shape -- `Sphere` stays the exact, unmodified original
`appendSphere()` call, preserving its own explicit "perfect sphere, equal
radii" contract (an existing test asserts this; verified it still passes
before shipping).

**Hair -- real design iteration, not a straight line**: first attempt was
6 tapered "spike" frustums (`appendHairSpike()`) radiating outward from
the crown -- looked like antenna/quills in a live screenshot, and in
combination with the head's narrow chin, read clearly as a goat. **Real,
explicit user correction**: "Revert the avatar head to a humanoid shape.
Do not use animal or novelty meshes." Rebuilt from scratch as 5 rounded,
overlapping ellipsoid blobs (`appendHairBlob()`, the same low-poly
lat/long sphere shape `AvatarFace.cpp`'s own `appendFeatureSphere()`
already establishes) clustered tightly against the crown/nape --
nothing radiates outward or tapers to a sharp point, so nothing reads as
a horn. One dominant "poof," four smaller layered accents, a small front
fringe -- verified via a second live screenshot to read as a natural
swept hair mass, not an animal feature. Real, honest, discrete per-tuft
color-step "gradient" (root/base darker, upper tufts lighter) -- not a
true per-vertex GPU vertex-color channel (`core::Vertex` has no color
attribute; adding one is a real, separate, engine-wide rendering-pipeline
change touching every mesh type and both scene shaders, not a bounded
avatar-visual addition -- the same honest framing this rig's existing
per-segment shading-gradient "AO stand-in" already established).
Rigidly bound to the real `head` joint (not `attach_hair`, which stays
reserved for the equippable Hair accessory override -- real, honest skip
when a Hair item is equipped, verified by a new headless test using
`VK_NULL_HANDLE` for the never-reached GPU handles, the same precedent
`SceneManager`'s own tests already use).

**Torso and Shoulders**: the torso's existing profiled-barrel gained a
4th ring (was 3) -- widest at a real shoulder-bulge ring just below the
top, narrowing back in at the neckline, so the silhouette genuinely
rounds over the shoulder instead of stopping flat at its own widest
point. Max half-width 0.27 -> 0.29.

**Arms, Legs, and Feet**: real forward-kinematics math (scripted, not
eyeballed) drove this. Arm segment length 0.6 -> 0.95 total (0.32/0.28 ->
0.51/0.44), *combined with* a new, more vertical idle/walk/run/
jump_start rest angle (50 deg -> 85 deg off horizontal) -- length alone
at the old angle would have needed an even longer, disproportionate arm
to reach the same target; the angle change is a real, necessary part of
this. Verified: idle-pose hand lands at world y=0.654, just below the
real hip/knee midpoint (0.675) -- "just below mid-thigh." Every arm
rotation keyframe across `idle/walk/run/jump_start.anim` was recomposed
via quaternion multiplication (`swing * new_rest`, the same technique the
prior session's T-pose fix established), not hand-edited. Upper leg
(thigh) shortened 0.45 -> 0.36, lower leg (shin) lengthened 0.45 -> 0.54
by the exact same amount, preserving the real total 0.9 hip-to-foot
length so feet stay grounded at y=0 -- every `.anim` file's own
`leg_L_lower`/`leg_R_lower` position keyframes (this format bakes
absolute position per keyframe, not a bind-pose delta) were updated to
match, including `jump_air`/`jump_land`, which don't touch arm rotation
but do animate leg position. Feet widened 0.1 -> 0.13 (X), 0.06 -> 0.07
(Y). Leg cross-sections (hip/knee/ankle) slimmed 0.13/0.105/0.085 ->
0.11/0.09/0.07 for the real "narrow legs" contrast against the widened
shoulders.

**Real bug found and fixed via live screenshot (not part of the original
plan)**: the widened torso shoulder ring (0.29) combined with the new,
near-vertical arm rest angle meant the arm's original shoulder
attachment (x=0.25, *inside* the torso's own 0.29 boundary) stayed
hugging/hidden behind the torso for its whole length, nearly invisible
from the front -- a direct failure of "proportions read clearly from all
camera angles." Fixed by widening the shoulder attachment itself,
0.25 -> 0.36, clearing the torso boundary plus the arm's own cross-section
radius with real margin. Same mechanical requirement as the leg-length
fix: every `.anim` file's `arm_L_upper`/`arm_R_upper` position keyframes
needed the matching update (6 files, all six).

**Hands**: `appendHand()` (new) replaces the old single 24-vertex
"mitten" box with a bigger palm (0.09/0.11/0.05 -> 0.10/0.12/0.065) plus
4 real, small, rigid finger blocks protruding from the palm's distal
face, continuing the shoulder->elbow->wrist chain's own bind-pose X axis
(the real anatomical "toward the fingertips" direction). 100% rigidly
bound to the same hand joint the palm uses -- no new joints, so
"deformation stays clean under animation" is automatic, not a new
guarantee to verify.

**Material Pass**: `segmentMaterialRoughness()` (new, internal) adds real
per-segment roughness variation (head 0.55, torso 0.58, arms 0.62, legs
0.66 -- metallic left at 0.05 everywhere, skin/cloth isn't metallic) on
top of the existing per-segment shading-gradient "AO stand-in"
(`applySegmentShadingGradient()`, unchanged, already the honest substitute
for true per-vertex AO -- see that function's own comment).

**Idle Pose Polish**: idle.anim only -- a real, constant ~4-degree head
tilt (Z-axis roll, composed with the existing subtle nod sway so both
play together) and a real, asymmetric right-arm rest angle (91 deg vs.
the left's 85 deg, "one arm lower"). Deliberately idle-only --
`walk`/`run`/`jump_start` stay symmetric; a persistent gait asymmetry
would read as a limp, not a natural idle stance.

**Tests**: 10 new checks -- real skeleton-length invariants (arm reach
increased, left/right symmetry preserved structurally, total leg length
exactly preserved at 0.9), real mesh-geometry proofs (hand vertex count,
foot width isolated by world Y below the ankle), and the hair
equip-skip test. **10764/10764 checks passing**, clean 4-target rebuild.
Visual verification: two live, privacy-conscious screenshots (before and
after the head/hair course-correction) confirmed the final result reads
as a human avatar with a swept hair mass, correctly-separated limbs, and
an intact facial rig -- not eyeballed from code alone.

**Maintained, not touched**: facial rig (`AvatarFace.cpp`, only the head
joint's own bind position changed, the five `face_*` joints and their
procedural expression system are untouched), clothing meshes
(`spawnAvatarClothing()`'s own `worldPos()` calls automatically pick up
the new arm/leg joint positions, no separate edits needed), accessory
rigging, distance-based LOD (hair tagged `AvatarLODCategory::Body`, never
hidden), Studio integration (`AvatarEditor` spawns hair the same way
Application/HomeAvatarPreview do), runtime integration.

## 2026-08-16 (even later) — Avatar 2.0: Performance and LOD (final Avatar 2.0 workstream)

**Cache rig transforms (real, done)**: `Skeleton::bindPoseMatrices()` is
an O(joint-count) hierarchy walk that allocates a fresh
`std::vector<glm::mat4>` every call, and was being recomputed up to 3
times per real tick per avatar (once in `AvatarController::tick()`
itself, once inside `applyFacialExpressionToSkinningMatrices()`, once
inside `applyAccessoryDynamicsToSkinningMatrices()`) despite being
invariant for a skeleton's whole lifetime. Fixed by caching it once at
construction/spawn time in all three real owners
(`AvatarController::cachedBindPose_`,
`runtime::HomeAvatarPreview::cachedBindPose_`,
`studio::plugins::AvatarEditor::cachedBindPose_`) and changing both
`applyFacialExpressionToSkinningMatrices()`/
`applyAccessoryDynamicsToSkinningMatrices()` to take a required
`bindPoseWorld` parameter instead of recomputing internally.

**Distance-based LOD (real, new)**: a new `core::AvatarLODTag` component
(`Body`/`Face`/`Clothing`/`Accessory`) is attached once at each entity's
real spawn point (`spawnRiggedAvatar()`, `uploadClothingPiece()`,
`spawnAvatarFace()`, `spawnAvatarAccessories()`) — not inferred from list
position, since clothing entity *count* varies per equip loadout, which
would make a positional scheme fragile. A new, pure, tested
`core::updateAvatarLOD()` (`core/AvatarLOD.hpp/.cpp`) reads each real
entity's tag and toggles its existing `SkinnedRenderable::visible` flag
based on distance-to-camera — this flag was already real and already
respected by the renderer's skinned draw loop
(`if (!skinned.visible) continue;`, `Renderer.cpp:4737`), so this needed
**zero renderer/shader changes** to actually skip real GPU draw calls.
Staggered thresholds (face 9m, accessories 12m, clothing 14.5m — `Body`
is never hidden, silhouette must stay readable at any distance) sit
comfortably above `CharacterController`'s default 6-unit third-person
camera distance (so a player's own face/accessories/clothing never
disappear in ordinary gameplay) and within reach of
`PreviewScene::kMaxOrbitDistance` (15 units), so a creator zooming out in
Studio's AvatarEditor or the Home preview genuinely walks through every
tier. Wired into all three real owners: `CharacterController::tick()`
(real gameplay avatar, using the real, one-tick-stale camera position
already available at that call site), and
`AvatarEditor::update()`/`HomeAvatarPreview::update()` (both via a new
`PreviewScene::orbitDistance()` accessor — the exact real, already-
computed distance the orbit camera uses, not a re-derived
approximation).

**Draw-call merging (real, partial — see below for what was deliberately
not done)**: the face's 5 separate feature meshes merge down to 3 real
draw calls — `spawnAvatarFace()` now builds one combined "FaceEyes" mesh
(left+right eye spheres) and one combined "FaceBrows" mesh (left+right
brow boxes), each half keeping its own per-vertex joint skin weight
(`setJointIndexRange()`) so it still deforms independently under
expression/animation despite sharing one draw call — safe because both
eyes always share `kEyeColor` and both brows always share `browColor`
(one `SkinnedRenderable::baseColor` is correct for the whole merged
piece), and because `applyFacialExpressionToSkinningMatrices()` only
ever writes into `skinningMatrices[jointIndex]`, never touches
entities/meshes directly, so it's completely unaffected by the merge.
Mouth stays its own entity (no pairing partner). Combined with the
clothing merge already shipped in an earlier Avatar 2.0 pass (shirt =
torso + both sleeves in one mesh, pants = both legs in one mesh), a
fully-equipped avatar now costs at most 6 (body) + 3 (face) + 2
(clothing) + up to 5 (accessories) draw calls, down from 18.

**Explicitly not done, and why**: merging the 6 body segments
(Torso+LeftArm+RightArm → 1 mesh, LeftLeg+RightLeg → 1 mesh) was
investigated and deliberately deferred — unlike the face/clothing
merges, a body-segment merge would either (a) flatten
`applySegmentShadingGradient()`'s existing real per-segment shading
(torso vs. arms vs. legs currently render at different brightness
multipliers even when wearing the same-colored item — a real, already-
shipped Visual Fidelity feature, and a single merged mesh can only carry
one `baseColor` per entity, this codebase's vertex format has no
per-vertex color channel) or (b) require a real, separate, larger
vertex-format/shader change to add one. It would also break the
`skinnedEntities_[i] == HumanoidBodySegment(i)` index correspondence
`Application::refreshLocalPlayerAvatarAppearance()` and
`AvatarEditor::refreshSegmentColors()` both rely on today. A real,
scoped follow-up, not a silently-dropped task.

**Visual verification**: Home preview screenshot confirmed no regression
at default (close) camera distance, where every LOD tier is expected to
stay in its "Full" state (3.0-unit default orbit distance is well under
every real cutoff). Live verification of the merged face specifically
was attempted but blocked by another already-fullscreened foreground app
occupying the whole screen at screenshot time — not silently skipped,
just honestly unverified visually; the merge's correctness instead rests
on the structural argument above (shared colors, per-vertex joint
weights, joint-indexed expression system) plus the full test suite.

7 new test checks (`testAvatarLODCategoryVisibleAtDistance`,
`testUpdateAvatarLODWritesVisibleOnRealEntities`, 13+4 individual
checks). Clean 4-target rebuild. **10754/10754 checks passing.**

This closes the "Performance and LOD" workstream, and with it, every
item in the original 7-part Avatar 2.0 mega-task (Facial System,
Clothing Meshes, Accessory Rigging, Animation Polish, Performance and
LOD, Studio Integration, Runtime Integration) is now real and shipped.

## 2026-08-16 (later still) — Avatar 2.0: Animation Polish + a real bind-pose bug found via screenshot

**Status check on the "Hand and Limb Integration" / "Accessory Rigging"
request**: hand geometry, per-joint skin weighting, and continuous limb
deformation under animation were **already real and tested** before this
entry (the existing box-hand-on-forearm-joint + smooth-limb-tube
architecture, covered by `testBuildHumanoidMeshDataArmTapersFromShoulderToElbow`
and friends). Accessory Rigging (attachment bones for hats/hair/face/
back/handhelds) also already shipped in the previous commit. Neither
was a real gap — but investigating the "looks disconnected" report
surfaced a real, different bug, described below.

**What was actually found and fixed**: a live screenshot of the Home
avatar preview showed the arms rendering as near-invisible thin lines,
not the "disconnected" look the geometry itself suggested. Root cause:
the rig's bind pose is a true T-pose (arms perfectly horizontal, +/-X),
and the default preview camera views the character close to head-on —
a limb pointing almost exactly at the camera projects to near-zero
screen width, regardless of how correct the underlying mesh is. Because
`idle.anim`'s own arm keyframes are baked at the *exact* T-pose rotation
(identity quaternion), this wasn't just a preview-camera framing issue —
the real gameplay idle pose has the same problem.

**Real fix**: edited the shipped animation clips' `arm_L_upper`/
`arm_R_upper` rotation keyframes to a real ~50° "A-pose" (angled down
and out from the shoulder), computed via proper quaternion composition
(existing swing rotation `*` new rest rotation, not just a naive angle
add) so the existing sway/swing motion is preserved on top of the new
rest angle, not replaced by it:
- `idle.anim`: rest pose only had a tiny sway -- now a real, always-angled-down
  arm instead of a flat T-pose the character holds for most of real
  playtime.
- `walk.anim`, `run.anim`: the existing Y-axis (front-back) swing now
  composes with the new Z-axis (down-and-out) rest angle at every
  keyframe, so walking/running arms swing from a natural base pose
  instead of passing back through full T-pose at the neutral point of
  each stride.
- `jump_start.anim`: same treatment (Y-axis swing composed with the new
  rest).
- `jump_air.anim`/`jump_land.anim`: **left unchanged** -- both already
  use a real, different Z-axis rotation (not a flat T-pose), so they
  don't have the same severe foreshortening problem, and composing a
  third rest angle onto an already-non-T-pose clip without being able to
  carefully verify a brief, fast mid-air/landing frame felt like more
  real risk than the confirmed problem justified. A real, stated,
  deliberate scope cut, not an oversight.
- `arm_L_lower`/`arm_R_lower` (elbow) tracks: **untouched** -- elbow
  rotation is already relative to the shoulder's own current orientation
  (standard FK hierarchy), so it doesn't need recomposing when the
  shoulder's rest angle changes.

Visually verified via live screenshot before and after -- the arms now
render as clearly visible, properly-shaded tapered limbs connecting
torso to hand, not thin lines. This is a real *asset* fix (animation
keyframe data), not a code fix -- no C++ changed, no rebuild needed to
take effect, `engine_tests` unaffected (10737/10737 still passing).

**Also shipped this entry (real code, not just data)**:
- **Support emote playback from Marketplace items** (a real, explicit
  requirement): `core::playEquippedEmote()`/`resolveEmoteClip()`
  (EmoteSystem.hpp) already existed and worked, but had **zero real
  trigger anywhere in actual gameplay** -- only Studio's AvatarPreviewer
  "Try On" flow called the underlying pieces. Added
  `Application::playEquippedEmote()` (a thin, real forward) and a new
  real, bindable `"PlayEmote"` action (default key G), with a real,
  edge-detected trigger in `RuntimeShell::tickEmoteActivation()` --
  works both online and offline (emotes are purely local/visual, unlike
  chat). Real, honest toast feedback ("No emote equipped") when nothing
  is equipped in the Emote category.
- **Secondary motion for torso and arms** (head was already done): new
  generic `computeSecondaryOscillationDegrees()` (pure, tested) reused
  for a real torso side-sway (Z-axis) and real opposite-phase left/right
  arm swing (X-axis), sharing the same locomotion-synced phase the
  head-bob already advances. Refactored the head-bob's own pivot
  construction into a shared `applyPivotedRotation` lambda rather than
  copy-pasting the translate/rotate/translate-back math three more
  times.
- 2 new test checks. **10737/10737 passing**, clean 4-target rebuild.

**Explicitly not done:** IK for foot placement (real, separate,
substantial system -- listed as optional in the original spec).
Performance/LOD/draw-call merging remain unstarted.


## 2026-08-16 (later still) — Avatar 2.0: Accessory Rigging (real, tested)

**What shipped:**
- Four new skeleton joints: `attach_hat`/`attach_hair`/`attach_face_accessory`
  (children of `head`) and `attach_back` (child of `spine_upper`, a real,
  distinct location from the face/hat/hair cluster). Joint count 23 → 27.
  Handhelds real-reuse the already-existing `hand_R` joint — no new joint
  needed there.
- New `core::AvatarAccessories.hpp/.cpp`: `spawnAvatarAccessories()` — a
  real, small placeholder box per equipped Hat/Hair/Face/Back/Accessory
  item (tinted with that item's real catalogue color), rigidly bound to
  its own attachment joint. Unlike clothing, an empty slot spawns
  nothing at all — there's no honest "everyone wears a hat by default"
  baseline the way there is for shirts/pants.
- Real "dynamic offsets": `computeBackAccessorySwayDegrees()` (pure,
  tested) + `applyAccessoryDynamicsToSkinningMatrices()` — a genuine
  per-frame sway on an equipped Back item, using the same pivot-around-
  the-joint construction the head-bob/facial-expression work already
  established. Wired into the real gameplay path (reuses
  `AvatarController`'s existing locomotion-synced phase) and the Home
  preview (its own gentle always-on phase, since it has no locomotion
  state to sync to).
- Wired into all three real spawn sites (gameplay avatar, Home preview,
  Studio `AvatarEditor`) alongside the face/clothing spawns.
- 12 new test checks (joint parenting including the real `attach_back` ≠
  head-child distinction, sway function). **10731/10731 passing**, clean
  4-target rebuild, real process launches with no errors.
- Not visually re-verified via screenshot this pass — nothing is
  equipped in Hat/Hair/Face/Back/Accessory in the default profile, so
  there's honestly nothing new to see yet; correctness rests on the
  passing pure-logic tests plus the already-proven rendering pipeline
  (identical architecture to the Facial System/Clothing work, both of
  which *were* visually confirmed).

**Explicitly not done:** no real hat/backpack/glasses *shapes* (every
accessory is currently the same placeholder box, differently sized/
tinted) — real per-category silhouettes are a stated, deferred art
task, not an engineering one. LOD and draw-call merging remain
unstarted. Animation Polish (secondary motion on torso/arms beyond the
existing head-bob) is next.


## 2026-08-16 (later still) — Avatar 2.0: Clothing Meshes (real, working)

**What shipped:**
- `core::spawnAvatarClothing()` (RiggedAvatar.hpp/.cpp) — real, separate
  procedural geometry, not the pre-existing tint-only look (which stays
  unchanged and still shows through for Hat/Shoes/Face/Back). A real
  shirt shell (torso barrel + short sleeves, reusing
  `appendProfiledBarrel()`/`appendSmoothLimb()` — the exact same
  functions the bare body uses) and a real pants shell (both full legs,
  hip to ankle), each one combined `RiggedMesh` (one draw call per
  piece). "Shared rig weights" in the literal sense asked for: bound to
  the exact same joint indices (`spine_upper`, `arm_*_upper/lower`,
  `leg_*_upper/lower`, `foot_*`) the body's own segments already use, not
  a separate skinning scheme.
- Real `ClothingFit{Tight, Loose}` enum, persisted as
  `LocalProfile::clothingFitIndex` (round-trip + backward-compat tested).
  `clothingFitScaleMultiplier()` scales every cross-section outward
  (1.06× Tight, 1.18× Loose).
- "Basic cloth shading": a real, small uniform darkening
  (`kClothingShadingMultiplier = 0.92`) distinct from the body's own
  per-segment gradient, so the shell reads as a different material.
- Wired into all three real spawn sites (gameplay avatar, Home preview,
  Studio `AvatarEditor`) — `Application::spawnLocalPlayerAvatar()` grew a
  real, optional trailing `ClothingFit` parameter (threaded through
  `RuntimeShell`'s callback and `main.cpp`'s lambda so the real gameplay
  avatar now reads the player's actual persisted fit choice, not just a
  hardcoded default).
- Studio integration: `AvatarEditor` gained a real "Clothing Fit"
  Tight/Loose control and — since the underlying expression system was
  already built for the Facial System — real, live "Facial Expression"
  sliders (blink/smile/frown/talk) too, both visually verified in the
  actual preview.
- 6 new test checks. Visually verified via live screenshot — tapered
  shirt torso and cylindrical pant legs render as genuinely distinct
  geometry from the bare body, no clipping/z-fighting artifacts.
  **10719/10719 checks passing**, clean 4-target rebuild.

**Explicitly not done this pass:** Hat/Shoes/Face/Back remain
color-tint-only (no accessory attachment meshes yet — the face joints'
own architecture is the right foundation, not yet extended to
hats/backpacks/handhelds). LOD and draw-call merging beyond "one mesh
per clothing piece" haven't been started.


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
