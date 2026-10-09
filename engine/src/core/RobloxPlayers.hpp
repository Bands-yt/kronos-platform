#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"

// Roblox's Players service, Player objects, characters and Humanoids on top
// of the Instance tree. A character is a Model in the workspace holding the
// player's capsule (as HumanoidRootPart) and a Humanoid. The host (Player
// app, Studio Play) keeps driving the capsule with CharacterController,
// which reads the Humanoid through control()/reportMotion().
namespace engine::core::players {

// Per-Humanoid runtime state (not saved).
struct HumanoidState {
    bool dead = false;
    double lastHealth = 100.0;
    bool hasMoveTo = false;
    glm::vec3 moveToTarget{0.0f};
    InstanceRef moveToPart = kNoInstance;
    float moveToElapsed = 0.0f;
    glm::vec3 scriptedMove{0.0f};
    bool scriptedMoveRelativeToCamera = false;
    float lastSpeed = 0.0f;
    std::string state = "Running"; // a HumanoidStateType item
};

// Per-Player runtime state (not saved).
struct PlayerState {
    EntityId rootPart = kNullEntity;
    bool local = false;
    float deadSeconds = 0.0f;
    bool loadRequested = false;
};

// Roblox's default WalkSpeed and JumpPower; the controller's own speeds
// match these, and other values scale them.
inline constexpr double kDefaultWalkSpeed = 16.0;
inline constexpr double kDefaultJumpPower = 50.0;
inline constexpr double kDefaultJumpHeight = 7.2;

// Adds a player now: a Player under Players (with Backpack and PlayerGui),
// then its character around `rootPart` if one is given. Fires PlayerAdded
// and CharacterAdded (deferred, like every signal).
InstanceRef join(ECS& ecs, const std::string& name, int64_t userId, EntityId rootPart, bool local);
// Same, but waits for the next tick(), so scripts that load this frame
// still see PlayerAdded.
void requestJoin(ECS& ecs, const std::string& name, int64_t userId, EntityId rootPart, bool local);
// Fires PlayerRemoving and CharacterRemoving, then removes the Player and
// its character Model. The root part itself is left to the host.
void leave(ECS& ecs, InstanceRef player);

// Once per frame after scripts have run: pending joins, respawns after
// death (Players.RespawnTime), LoadCharacter, falling below
// workspace.FallenPartsDestroyHeight, and MoveTo time-outs.
void tick(ECS& ecs, float dt);
// Forgets every player (Studio Stop). Their entities go with the scene.
void reset(ECS& ecs);

// Where players appear when the place has no SpawnLocation.
void setFallbackSpawn(ECS& ecs, glm::vec3 position);

[[nodiscard]] InstanceRef localPlayer(ECS& ecs);
[[nodiscard]] std::vector<InstanceRef> list(ECS& ecs);
[[nodiscard]] InstanceRef playerFromCharacter(ECS& ecs, InstanceRef character);
[[nodiscard]] InstanceRef playerByUserId(ECS& ecs, int64_t userId);
// Builds a fresh character at a spawn (now, not on the next tick).
void loadCharacter(ECS& ecs, InstanceRef player);
// Asks for a respawn on the next tick (Player:LoadCharacter()).
void requestLoadCharacter(ECS& ecs, InstanceRef player);

// The Humanoid in the Model holding `rootPart`, or kNullEntity.
[[nodiscard]] EntityId humanoidFor(ECS& ecs, EntityId rootPart);

// What the Humanoid asks of the character this frame.
struct HumanoidControl {
    bool found = false;
    bool dead = false;
    float walkScale = 1.0f; // WalkSpeed / 16
    float jumpScale = 1.0f; // from JumpPower or JumpHeight
    bool jump = false;      // Humanoid.Jump was set; consumed
    bool autoRotate = true;
    bool hasMoveTo = false;
    glm::vec3 moveToTarget{0.0f};
    glm::vec3 scriptedMove{0.0f}; // Humanoid:Move
    bool scriptedMoveRelativeToCamera = false;
};
[[nodiscard]] HumanoidControl control(ECS& ecs, EntityId rootPart);

// What the character did this frame: sets MoveDirection and fires Running,
// Jumping, FreeFalling, StateChanged and MoveToFinished.
struct HumanoidMotion {
    glm::vec3 moveDirection{0.0f};
    float speed = 0.0f; // studs per second, Roblox scale
    bool grounded = true;
    bool jumped = false;
    bool reachedMoveTo = false;
};
void reportMotion(ECS& ecs, EntityId rootPart, const HumanoidMotion& motion);

// Humanoid methods.
void takeDamage(ECS& ecs, EntityId humanoid, double amount);
void moveTo(ECS& ecs, EntityId humanoid, glm::vec3 target, InstanceRef part);
void move(ECS& ecs, EntityId humanoid, glm::vec3 direction, bool relativeToCamera);
[[nodiscard]] std::string state(ECS& ecs, EntityId humanoid);
void changeState(ECS& ecs, EntityId humanoid, const std::string& state);

// Property hooks from the class table (core/InstanceTree.cpp).
void onHealthSet(ECS& ecs, EntityId humanoid, const InstanceValue& value);
void onMaxHealthSet(ECS& ecs, EntityId humanoid, const InstanceValue& value);

// The player list Roblox draws when players have a "leaderstats" folder:
// one column per value in it, in the order the values were added.
struct Leaderboard {
    std::vector<std::string> columns;
    struct Row {
        std::string name;
        std::vector<std::string> values;
        bool local = false;
    };
    std::vector<Row> rows;
};
// Empty (no columns, no rows) when no player has leaderstats.
[[nodiscard]] Leaderboard leaderboard(ECS& ecs);

} // namespace engine::core::players
