#pragma once

#include "core/ECS.hpp"

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

union SDL_Event;
struct lua_State;

namespace engine::platform_adapters {
class UnifiedInput;
}

namespace engine::net {
class NetworkSession;
}

namespace engine::core {

class Physics;
class CharacterController;
class Camera;
class UIRenderer;
class Renderer;
class Scripting;
class Audio;
class MeshLibrary;
class ScriptUiApi;
class ScriptUiLayoutApi;
struct SceneLighting;
struct PerformanceMetrics;

// What a game mode may touch on the Application; built fresh for each call.
struct GameModeContext {
    ECS& ecs;
    Physics& physics;
    CharacterController& controller;
    Camera& camera;
    platform_adapters::UnifiedInput& input;
    UIRenderer& ui;
    Renderer& renderer;
    Scripting& scripting;
    net::NetworkSession& network;
    MeshLibrary& meshes;
    Audio& audio;
    ScriptUiApi& scriptUi;
    ScriptUiLayoutApi& scriptUiLayout;
    const PerformanceMetrics& performance;
    uint32_t windowWidth;
    uint32_t windowHeight;
    float totalSimTime;
    bool reducedMotion;
};

// A game built on the engine hooks into the Application through this, so
// engine_core needs no game headers. Every hook defaults to doing nothing.
class GameMode {
public:
    virtual ~GameMode() = default;

    // Before the character controller moves this tick.
    virtual void beforeMovement(GameModeContext& ctx) { (void)ctx; }
    // Once per tick with the other generic systems.
    virtual void updateSystems(GameModeContext& ctx, float dt) { (void)ctx; (void)dt; }
    // After the simulation clock advanced this tick (ctx.totalSimTime is current).
    virtual void tickEnvironment(GameModeContext& ctx, float dt) { (void)ctx; (void)dt; }
    // Each raw SDL event, before the engine's own handling.
    virtual void onRawEvent(GameModeContext& ctx, const SDL_Event& event) { (void)ctx; (void)event; }
    // Lets the mode adjust this tick's lighting (fog, exposure) around the player.
    virtual void adjustLighting(GameModeContext& ctx, SceneLighting& lighting, const glm::vec3& playerPos) {
        (void)ctx; (void)lighting; (void)playerPos;
    }
    // The mode's own world and HUD for this tick.
    virtual void tickWorld(GameModeContext& ctx, float dt, EntityId character, const glm::vec3& characterPos) {
        (void)ctx; (void)dt; (void)character; (void)characterPos;
    }
    // Every tick, with the Interact key's current state and what the player looks at.
    virtual void tickLookAt(GameModeContext& ctx, float dt, EntityId character, bool interactDown, EntityId lookAtTarget) {
        (void)ctx; (void)dt; (void)character; (void)interactDown; (void)lookAtTarget;
    }
    // A fresh Interact press; return true to swallow it (the rest of the press handling is skipped).
    virtual bool consumeInteractPress(GameModeContext& ctx, EntityId character, const glm::vec3& characterPos) {
        (void)ctx; (void)character; (void)characterPos;
        return false;
    }
    // A Door was interacted with; return false to keep it shut.
    virtual bool allowDoorToggle(GameModeContext& ctx, EntityId door, EntityId character) {
        (void)ctx; (void)door; (void)character;
        return true;
    }
    // A target was interacted with (after the generic door/pickup handling).
    virtual void onInteract(GameModeContext& ctx, EntityId target, EntityId character, const glm::vec3& characterPos) {
        (void)ctx; (void)target; (void)character; (void)characterPos;
    }
    // The "[E] ..." text for what the player is aimed at (empty when nothing).
    virtual void setInteractionPrompt(const std::string& prompt) { (void)prompt; }
    // After physics stepped: AI, rules, HUD.
    virtual void afterPhysics(GameModeContext& ctx, float dt) { (void)ctx; (void)dt; }
    // True when the mode drives the camera itself (no player movement or follow camera).
    [[nodiscard]] virtual bool controlsCamera() const { return false; }
    // Called each tick in place of the player's camera when controlsCamera() is true.
    virtual void tickCamera(GameModeContext& ctx, float dt) { (void)ctx; (void)dt; }
    // Adds the mode's own functions to a script VM that is being set up.
    virtual void registerScripting(lua_State* L) { (void)L; }
    // True once the player asked to restart; checked by main after run() returns.
    [[nodiscard]] virtual bool restartRequested() const { return false; }
};

} // namespace engine::core
