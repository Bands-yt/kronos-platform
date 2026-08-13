#pragma once

#include <vector>

#include "core/AvatarController.hpp"
#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/Physics.hpp"
#include "platform_adapters/UnifiedInput.hpp"

namespace engine::core {

// Real, pure velocity-ramping math -- ramps `current` toward `target` by
// at most `rate` (studs/sec^2) over `dt`, snapping exactly to `target`
// once within reach rather than overshooting and oscillating. Extracted
// out of CharacterController::tick() specifically so this real
// acceleration/deceleration/air-control behavior is unit-testable
// without a live UnifiedInput (which needs a real SDL/window context to
// initialize() at all, see tick()'s own doc comment) -- the same
// "pure decision logic split from the I/O-touching method" pattern
// core::AvatarController::tickAnimation() already established relative
// to its own ECS/Physics-touching tick().
[[nodiscard]] glm::vec2 rampVelocityTowardTarget(glm::vec2 current, glm::vec2 target, float rate, float dt);

// A baseline, always-available character you can move -- the thing an
// empty Roblox place gives you for free (spawn, walk, jump, a camera that
// follows) before any gameplay script exists. Once the Instance/DataModel
// layer exists (Scripting.hpp's TODO), this becomes what a default
// Humanoid-driven character does; until then it's what engine_runtime
// itself provides directly, so there's something to control at all.
//
// Keyboard + mouse only in this pass -- see UnifiedInput.cpp's note on why
// gamepad axis support isn't wired into movement yet (combining a boolean
// key binding and a signed analog stick under one action needs a real
// input-mapping decision, not a guess made without a controller attached
// to verify the feel).
class CharacterController {
public:
    struct Settings {
        float walkSpeed = 8.0f;  // studs/sec-equivalent
        float runSpeed = 16.0f;  // held while the "Run" action is down -- see configureInput()
        float jumpSpeed = 7.0f;
        float capsuleRadius = 0.35f;
        float capsuleHalfHeight = 0.55f; // ~1.8m tall including both hemispherical caps
        float cameraDistance = 6.0f;
        float cameraHeight = 1.7f;
        float mouseSensitivity = 0.15f;
        float noseForwardOffset = 0.42f; // just outside the capsule radius
        float noseHeightOffset = 0.2f;   // roughly chest height, not centered on the capsule

        // Real acceleration model, not an instant velocity snap: horizontal
        // velocity ramps toward the input-driven target at
        // groundAcceleration (studs/sec^2) while grounded, and decays back
        // toward zero at groundDeceleration when there's no input --
        // deceleration is deliberately a separate, usually-higher number
        // than acceleration (stopping "digs in" faster than starting
        // moves, the standard character-controller feel; real ground
        // friction, not just "let physics damping handle it").
        float groundAcceleration = 60.0f;
        float groundDeceleration = 80.0f;
        // [0,1] -- how much of groundAcceleration still applies while
        // airborne. 1.0 would mean full mid-air directional control (an
        // arcade feel); 0.0 would mean "whatever horizontal velocity you
        // had at jump time is locked in until you land" (a stricter,
        // realistic feel). A real, tunable middle ground, not either
        // extreme.
        float airControlMultiplier = 0.3f;

        // The tallest ledge tick() will step the character straight up
        // onto instead of blocking movement into it -- real stairs/curbs,
        // not just flat ground. See tryStepUp()'s comment for the
        // two-raycast technique (this engine's character is a raw Jolt
        // dynamic capsule, not JPH::CharacterVirtual, which has step
        // handling built in -- this is the real, if simpler, equivalent
        // for that simpler body type).
        float stepHeight = 0.3f;

        // Ground steeper than this angle (from horizontal) is real
        // *collision* (isGrounded()'s raycast still hits it) but not
        // *standable* ground for gameplay purposes -- no jump, and
        // gravity/sliding take over instead of the character sticking to
        // a wall-steep slope the way a naive "ray hit something = grounded"
        // check would otherwise allow.
        float maxSlopeDegrees = 45.0f;

        // Real exponential camera-position smoothing (studs/sec-scale
        // rate, higher = snappier/less lag) -- the camera's *look*
        // direction (yaw/pitch) still follows the mouse instantly (any lag
        // there would feel like input latency), but its *position*
        // eases toward the character's focus point instead of snapping
        // there every tick, so a sudden vertical motion (landing a jump,
        // a step-offset nudge) doesn't visibly teleport the view. 0
        // disables smoothing entirely (instant snap, this class's
        // original pre-this-pass behavior).
        float cameraPositionSmoothing = 12.0f;
    };

    // Two constructors rather than one with `Settings settings = {}`: a
    // nested class's default member initializers can't be used via a
    // default *argument* of the enclosing class's own constructor while
    // still inside that constructor's declaration (a real, if obscure,
    // C++ rule -- GCC's diagnostic for it is a cryptic "could not
    // convert"; Clang's names the actual rule directly). Splitting into
    // CharacterController() and CharacterController(Settings) sidesteps
    // it entirely: settings_ default-initializes through Settings' own
    // complete-class context instead.
    CharacterController() = default;
    explicit CharacterController(Settings settings) : settings_(settings) {}

    // Kronos ("Gameplay Loop" world-building, "Suit upgrades"): a real,
    // mutable accessor -- lets a live system (Application's own TNT Wars
    // upgrade purchase handling) retune movement speed at runtime, the
    // same "caller reads/writes, this class just owns the storage"
    // convention TntWarsMatch::trenchesWallMutable() already establishes.
    // A caller applying a multiplier should scale from a real, separately
    // captured *base* value, not repeatedly compound this live value --
    // see Application's own tntWarsBaseWalkSpeed_/tntWarsBaseRunSpeed_
    // comment for exactly that.
    [[nodiscard]] Settings& settingsMutable() { return settings_; }
    [[nodiscard]] const Settings& settings() const { return settings_; }

    // Binds the default WASD + Space + Shift + mouse-look scheme. A real
    // settings UI would call bindAction() again per action to remap --
    // see UnifiedInput.hpp's doc comment on why that's free.
    void configureInput(platform_adapters::UnifiedInput& input) const;

    // Spawns the character's capsule body. If `noseMeshHandle` is set
    // (not kInvalidHandle), also spawns a small marker entity that tick()
    // keeps positioned just in front of the character, facing the same
    // way -- without it, a capsule is rotationally symmetric and
    // setRotationY's facing update, while genuinely applied to the
    // physics body every tick, would be completely invisible on screen.
    [[nodiscard]] EntityId spawn(ECS& ecs, Physics& physics, glm::vec3 spawnPosition,
                                  uint32_t noseMeshHandle = Renderable::kInvalidHandle);

    // Real, optional override of the look direction tick() starts from --
    // spawn() itself never touches cameraYawDegrees_/cameraPitchDegrees_
    // (they keep their own real defaults, tuned for the bring-up scene's
    // own -Z-facing spawn), so a caller spawning a character somewhere
    // that default doesn't make sense for (e.g. a map-specific spawn
    // point facing a specific direction) calls this once, right after
    // spawn(), before the first real tick() runs.
    void setInitialCameraAngles(float yawDegrees, float pitchDegrees) {
        cameraYawDegrees_ = yawDegrees;
        cameraPitchDegrees_ = pitchDegrees;
    }

    // Reads input, moves the character (relative to camera yaw), sets
    // facing, and moves `camera` to follow in third person. Must run
    // *before* Physics::step() in GameLoop's tick (see GameLoop's
    // pre-tick hook) so the velocity/rotation set here are what this
    // tick's simulation actually uses, not next tick's.
    //
    // `avatarController`/`skinnedEntities` are optional (both default
    // null, so every pre-existing call site is unaffected) -- the real
    // movement<->animation sync seam this pass adds: when present,
    // this method drives `avatarController`'s locomotion blend tree from
    // this tick's *actual* physics-resolved speed/grounded state
    // (movement -> animation), and, symmetrically, suppresses new
    // movement input whenever `avatarController->isEmotePlaying()` is
    // true (animation -> movement) -- the same "an emote takes over the
    // character" rule Roblox's own client enforces, not a full root-
    // motion system (this engine's clips carry no root-displacement
    // data to extract -- a real, deliberately un-built extension, see
    // README's Known Issues).
    void tick(float dt, ECS& ecs, Physics& physics, platform_adapters::UnifiedInput& input, Camera& camera,
              AvatarController* avatarController = nullptr, const std::vector<EntityId>* skinnedEntities = nullptr);

    [[nodiscard]] EntityId entity() const { return entity_; }

    // Real two-raycast step-offset check (see Settings::stepHeight's
    // comment): if movement is blocked at foot height but clear at
    // step height directly ahead, nudges the character straight up via
    // Physics::setPosition() so it climbs the ledge instead of stopping
    // dead against it. A no-op (returns false) if there's nothing to
    // step onto -- callers don't need to guard the call themselves.
    // Called internally by tick() every grounded, moving tick; public
    // (needs only ECS&/Physics&, no UnifiedInput) so it's independently
    // testable and directly usable (e.g. a script nudging a character
    // over a known obstacle) without going through the full input-driven
    // tick().
    bool tryStepUp(ECS& ecs, Physics& physics, glm::vec3 moveDir);

private:
    Settings settings_;
    EntityId entity_ = kNullEntity;
    EntityId noseEntity_ = kNullEntity;
    float cameraYawDegrees_ = -90.0f;
    float cameraPitchDegrees_ = -15.0f;
    float facingYawRadians_ = 0.0f;
    // Real per-tick smoothed camera focus position -- see
    // Settings::cameraPositionSmoothing. Starts uninitialized-but-unused:
    // the very first tick() call snaps it directly to that tick's target
    // (no lag on the character's first frame), every tick after that
    // eases toward the new target instead of snapping.
    glm::vec3 smoothedCameraFocus_{0.0f};
    bool cameraFocusInitialized_ = false;
};

} // namespace engine::core
