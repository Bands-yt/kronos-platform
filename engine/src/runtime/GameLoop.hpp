#pragma once

#include <functional>

#include <glm/glm.hpp>

#include "core/Audio.hpp"
#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/Physics.hpp"
#include "core/Renderer.hpp"
#include "core/Scripting.hpp"
#include "core/Window.hpp"

namespace engine::runtime {

// The fixed-tick order from docs/ARCHITECTURE.md §6's scheduler table, now
// genuinely rate-decoupled (Sprint 14, "render-tick decoupling") into
// three independent real cadences instead of one shared fixedDt:
//   - Sim tick     (default 120Hz): Scripting::tick() -> Physics::step()
//     -> postPhysicsHook -> ECS::update() -> Audio::mix().
//   - Network tick (default 60Hz): a real, separately-accumulated cadence
//     -- Application wires net::NetworkSession::tick() here, not into the
//     sim hook, so networking keeps its own already-tested 60Hz real
//     cadence independent of whatever the sim rate is tuned to.
//   - Render (uncapped by default, optionally real frame-paced to a
//     target) -- called exactly once per real run() loop iteration,
//     never coupled 1:1 to either accumulator above. This is the real
//     decoupling this class's own previous header comment flagged as a
//     stated gap ("this attaches here once it exists, not a rewrite of
//     this ordering") -- see run()'s own comment for the real mechanism.
//
// This class owns *only* that ordering/pacing. It does not own subsystem
// lifetime (Application does) and does not know what a "game" is --
// still deliberately not a place for gameplay logic to grow.
class GameLoop {
public:
    struct Subsystems {
        engine::core::Window* window = nullptr;
        engine::core::Renderer* renderer = nullptr;
        engine::core::ECS* ecs = nullptr;
        engine::core::Physics* physics = nullptr;
        engine::core::Audio* audio = nullptr;
        engine::core::Scripting* scripting = nullptr;
        engine::core::Camera* camera = nullptr; // audio listener transform; nullptr is valid (origin/forward fallback)
    };

    explicit GameLoop(Subsystems subsystems);

    // Invoked first in every simTick(), before Scripting::tick() -- the
    // one deliberate seam in an otherwise fixed, literal ordering. Runs at
    // the *sim* rate (see RunConfig::simDt), not the network rate -- see
    // setNetworkTickHook() for that one.
    using PreTickHook = std::function<void(float dt)>;
    void setPreTickHook(PreTickHook hook) { preTickHook_ = std::move(hook); }

    // Invoked right after Physics::step(), before ECS::update() -- the
    // symmetric counterpart to the pre-tick hook above, for exactly the
    // same reason: this is how Application wires in draining Physics'
    // real collision events and firing Scripting's events.onCollision
    // bus without GameLoop needing to know either system exists.
    using PostPhysicsHook = std::function<void(float dt)>;
    void setPostPhysicsHook(PostPhysicsHook hook) { postPhysicsHook_ = std::move(hook); }

    // Sprint 14's real, separate hook for whatever needs to run at the
    // *network* rate specifically -- distinct from PreTickHook, which
    // runs at the sim rate. Application wires net::NetworkSession::tick()
    // (and the networked-client input-sampling that feeds it) in here, so
    // it genuinely advances at its own real, independent cadence
    // regardless of what the sim rate is configured to.
    using NetworkTickHook = std::function<void(float dt)>;
    void setNetworkTickHook(NetworkTickHook hook) { networkTickHook_ = std::move(hook); }

    // Invoked right after Renderer::renderFrame() returns, once per real
    // rendered frame (not once per sim/network step -- see class
    // comment). `dt` passed in is real wall-clock time since the
    // previous render call, matching Renderer::renderFrame()'s own
    // already-real wall-clock fps measurement.
    using PostRenderHook = std::function<void(float dt)>;
    void setPostRenderHook(PostRenderHook hook) { postRenderHook_ = std::move(hook); }

    // Kronos ("Active Joining UI" -- engine_runtime ImGui + input
    // integration): the real, symmetric counterpart to PostRenderHook --
    // invoked right BEFORE Renderer::renderFrame() is called, once per
    // real rendered frame. The one real seam this class didn't have
    // until now: ImGui's own per-frame sequence (NewFrame() -> real UI
    // draw calls -> Render(), which just builds draw-command lists) must
    // run before renderFrame() reaches the point that actually submits
    // to the swapchain (see core::Renderer::setOverlayCallback(), which
    // fires from inside renderFrame() and needs that draw data already
    // built by then) -- PostRenderHook fires too late for this.
    // studio::StudioApp's own hand-rolled loop achieves the same ordering
    // via two separate calls (beginFrame()/endFrame()) around its own
    // renderFrame() call; GameLoop's real, fixed run() loop has no
    // equivalent seam to split, so this single hook covers both halves
    // in one real call.
    using PreRenderHook = std::function<void(float dt)>;
    void setPreRenderHook(PreRenderHook hook) { preRenderHook_ = std::move(hook); }

    struct RunConfig {
        float simDt = 1.0f / 120.0f;    // Sprint 14: physics/scripting/ecs/audio tick rate
        float networkDt = 1.0f / 60.0f; // Sprint 14: independent, real networking tick rate
        // Real render frame-pacing target -- 0 means uncapped (render as
        // fast as the real per-frame work + present mode allow, which on
        // this engine's lightweight demo scenes is typically well above
        // 180fps once render is no longer coupled 1:1 to a 60Hz sim
        // step). A positive value real-sleeps the remainder of each loop
        // iteration so presented frame time is *stable* near this target
        // rather than free-running -- matching Sprint 14's own "stable
        // 180 FPS" ask, not just "180 or more, wildly variable".
        float targetRenderDt = 1.0f / 180.0f;
    };

    // Runs until the window requests close. Two independent real
    // fixed-rate accumulators (sim, network) plus one real, uncoupled
    // render call per real loop iteration -- see class comment.
    //
    // Two overloads, not one with a default argument: a nested struct's
    // (RunConfig's) in-class default member initializers can't be used
    // via a `= {}` default *argument* of an enclosing-class member
    // function while still inside that function's declaration -- the
    // same real GCC/Clang-confirmed C++ rule (not a compiler bug)
    // CharacterController's own constructor hit; see that class's header
    // comment for the full account. run() below just forwards to
    // run(RunConfig{}) from the .cpp file, where GameLoop is already a
    // complete type.
    void run();
    void run(RunConfig config);

    // The real, sim-rate-only tick: preTickHook -> scripting -> physics ->
    // postPhysicsHook -> ecs -> audio. No renderer call here anymore --
    // see class comment.
    void simTick(float dt);

    // The real, network-rate-only tick -- just the injected hook, kept as
    // its own real method (not inlined into run()) so a caller (e.g. a
    // test) can drive it directly at an arbitrary dt without also paying
    // for a full sim tick.
    void networkTick(float dt);

    // The real, per-real-frame render call -- renderer.renderFrame() then
    // postRenderHook.
    void renderTick(float dt);

private:
    Subsystems subsystems_;
    PreTickHook preTickHook_;
    PostPhysicsHook postPhysicsHook_;
    NetworkTickHook networkTickHook_;
    PostRenderHook postRenderHook_;
    PreRenderHook preRenderHook_;
};

} // namespace engine::runtime
