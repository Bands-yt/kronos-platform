#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/AvatarController.hpp"
#include "core/Camera.hpp"
#include "core/CharacterController.hpp"
#include "core/PlayerAvatar.hpp"
#include "platform_adapters/UnifiedInput.hpp"

namespace engine::core {
class Physics;
class Renderer;
} // namespace engine::core

namespace engine::studio {

// Roblox-style Play in Studio: drops the creator's avatar into the scene
// with the same controls as the Player (WASD, Space, Ctrl to run,
// right-drag to look, Shift lock, wheel zoom), then gives the editor
// camera back on Stop.
class PlaySoloPlayer {
public:
    // Spawns at the scene's SpawnLocation, or where the Player would.
    bool begin(core::ECS& ecs, core::Physics& physics, core::Renderer& renderer,
               core::RiggedMeshLibrary& riggedMeshLibrary, const core::PlayerAvatarLook& look,
               const core::CatalogueIndex& catalogueIndex, const std::string& playerName, int64_t userId,
               core::Camera& camera);
    // `pointerInViewport`: the mouse is over the game view, not a panel.
    // `typing`: a text box has keyboard focus, so keys don't move the player.
    void tick(float dt, core::ECS& ecs, core::Physics& physics, core::Camera& camera, bool pointerInViewport,
              bool typing, float mouseWheel);
    // Fires PlayerRemoving, removes the avatar and restores the editor
    // camera. Call before the scene is restored; the capsule and the
    // character Model go with the scene restore.
    void end(core::ECS& ecs, core::Camera& camera);

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool shiftLocked() const { return shiftLocked_; }
    [[nodiscard]] core::EntityId character() const { return active_ ? controller_.entity() : core::kNullEntity; }

private:
    platform_adapters::UnifiedInput input_;
    bool inputReady_ = false;
    core::CharacterController controller_;
    std::unique_ptr<core::AvatarController> avatar_;
    std::vector<core::EntityId> avatarEntities_;
    core::Camera editorCamera_;
    bool active_ = false;
    bool shiftLocked_ = false;
    bool shiftKeyWasDown_ = false;
};

} // namespace engine::studio
