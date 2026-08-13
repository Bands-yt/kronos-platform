#include "runtime/GameLoop.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include "core/Logger.hpp"

namespace engine::runtime {

using namespace engine::core;

GameLoop::GameLoop(Subsystems subsystems) : subsystems_(subsystems) {}

void GameLoop::simTick(float dt) {
    if (preTickHook_) {
        preTickHook_(dt);
    }

    // scripting.tick(dt);
    subsystems_.scripting->tick(dt);

    // physics.step(dt);
    subsystems_.physics->step(dt, *subsystems_.ecs);

    if (postPhysicsHook_) {
        postPhysicsHook_(dt);
    }

    // ecs.update(dt);
    subsystems_.ecs->update(dt);

    // audio.mix();
    // Listener = the real camera now (Camera.hpp) -- §4.3 says this is
    // mixed from AudioSource + Transform each frame, relative to whatever
    // the active viewpoint is. Kept on the sim tick (not render) since
    // the camera/character state it reads is itself sim-tick-driven.
    if (subsystems_.camera) {
        subsystems_.audio->mix(*subsystems_.ecs, subsystems_.camera->position, subsystems_.camera->forward(),
                                glm::vec3{0.0f, 1.0f, 0.0f});
    } else {
        subsystems_.audio->mix(*subsystems_.ecs, glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
    }
}

void GameLoop::networkTick(float dt) {
    if (networkTickHook_) {
        networkTickHook_(dt);
    }
}

void GameLoop::renderTick(float dt) {
    if (preRenderHook_) {
        preRenderHook_(dt);
    }

    // renderer.render();
    if (!subsystems_.renderer->renderFrame()) {
        std::fprintf(stderr, "GameLoop: renderFrame() reported an unrecoverable error.\n");
    }

    if (postRenderHook_) {
        postRenderHook_(dt);
    }
}

void GameLoop::run() { run(RunConfig{}); }

void GameLoop::run(RunConfig config) {
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    float simAccumulator = 0.0f;
    float networkAccumulator = 0.0f;

    // Fixed-tick accumulators: real frame time can vary (vsync, load,
    // background processes), but every simTick()/networkTick() call
    // advances by exactly its own configured dt, matching each
    // subsystem's real fixed-tick model instead of a variable dt that
    // would make physics/script/network timing framerate-dependent.
    // kMaxStepsPerFrame is doubled from the pre-Sprint-14 single-60Hz-tick
    // value (8) to preserve the same real stall-recovery time budget now
    // that simDt defaults to 120Hz (half the step size needs twice the
    // step count to recover the same real elapsed time after a stall).
    constexpr float kMaxFrameTime = 0.25f; // clamp to avoid a "spiral of death" after a stall
    constexpr int kMaxSimStepsPerFrame = 16;
    constexpr int kMaxNetworkStepsPerFrame = 8;

    while (subsystems_.window->pumpEvents()) {
        auto frameStart = Clock::now();
        float frameTime = std::chrono::duration<float>(frameStart - previous).count();
        previous = frameStart;
        frameTime = std::min(frameTime, kMaxFrameTime);

        simAccumulator += frameTime;
        networkAccumulator += frameTime;

        int simSteps = 0;
        while (simAccumulator >= config.simDt && simSteps < kMaxSimStepsPerFrame) {
            simTick(config.simDt);
            simAccumulator -= config.simDt;
            ++simSteps;
        }
        // Kronos (Phase 1 stability audit fix): real, honest telemetry +
        // a real bound -- confirmed live gap: under sustained overload
        // (simSteps pegged at kMaxSimStepsPerFrame every frame),
        // simAccumulator previously just kept growing forever with no
        // warning anywhere, silently falling further and further behind
        // wall-clock with zero visibility. Real fix: once steps are
        // capped, drop the un-simulatable backlog outright (there is no
        // real way to "catch up" once behind by more than one frame's
        // worth of max steps -- accumulating it just delays the same
        // real problem) and log it once so it's actually visible.
        if (simSteps == kMaxSimStepsPerFrame && simAccumulator >= config.simDt) {
            logWarn("GameLoop",
                    "sim falling behind wall-clock -- capped at %d steps this frame, dropping %.3fs of "
                    "un-simulatable backlog.",
                    kMaxSimStepsPerFrame, static_cast<double>(simAccumulator));
            simAccumulator = 0.0f;
        }

        int networkSteps = 0;
        while (networkAccumulator >= config.networkDt && networkSteps < kMaxNetworkStepsPerFrame) {
            networkTick(config.networkDt);
            networkAccumulator -= config.networkDt;
            ++networkSteps;
        }
        if (networkSteps == kMaxNetworkStepsPerFrame && networkAccumulator >= config.networkDt) {
            logWarn("GameLoop",
                    "network tick falling behind wall-clock -- capped at %d steps this frame, dropping %.3fs of "
                    "un-simulatable backlog.",
                    kMaxNetworkStepsPerFrame, static_cast<double>(networkAccumulator));
            networkAccumulator = 0.0f;
        }

        // Real, uncoupled render call -- exactly once per real loop
        // iteration, regardless of how many sim/network steps just ran
        // (zero, one, or several after a stall). `frameTime` here is real
        // wall-clock time, consistent with Renderer::renderFrame()'s own
        // real fps measurement (see that function's own comment).
        renderTick(frameTime);

        // Real frame pacing (Sprint 14's "stable 180 FPS", not just "180
        // or more"): if this real iteration's work finished under the
        // target render period, sleep off the real remainder so
        // presented frame time stabilizes near the target instead of
        // free-running as fast as the GPU allows.
        if (config.targetRenderDt > 0.0f) {
            float elapsed = std::chrono::duration<float>(Clock::now() - frameStart).count();
            float remaining = config.targetRenderDt - elapsed;
            if (remaining > 0.0f) {
                std::this_thread::sleep_for(std::chrono::duration<float>(remaining));
            }
        }
    }
}

} // namespace engine::runtime
