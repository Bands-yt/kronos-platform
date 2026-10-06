#include "studio/PlaySoloPlayer.hpp"

#include <cstdio>

#include "core/Components.hpp"
#include "core/Physics.hpp"
#include "core/Renderer.hpp"

namespace engine::studio {

namespace {
// Same spot the Player uses when a game has no SpawnLocation.
constexpr glm::vec3 kDefaultSpawn{0.0f, 3.0f, -6.0f};
} // namespace

bool PlaySoloPlayer::begin(core::ECS& ecs, core::Physics& physics, core::Renderer& renderer,
                           core::RiggedMeshLibrary& riggedMeshLibrary, const core::PlayerAvatarLook& look,
                           const core::CatalogueIndex& catalogueIndex, const std::string& playerName,
                           core::Camera& camera) {
    if (active_) return true;
    if (!inputReady_) {
        if (!input_.initialize()) return false;
        controller_.configureInput(input_);
        inputReady_ = true;
    }

    controller_ = core::CharacterController{};
    const glm::vec3 spawn = core::findPlayerSpawnPosition(ecs, kDefaultSpawn);
    if (controller_.spawn(ecs, physics, spawn) == core::kNullEntity) {
        std::fprintf(stderr, "PlaySoloPlayer: couldn't create the character.\n");
        return false;
    }
    controller_.setInitialCameraAngles(camera.yawDegrees, -15.0f);
    if (auto* name = ecs.tryGetComponent<core::Name>(controller_.entity()); name && !playerName.empty()) {
        name->value = playerName;
    }

    avatarEntities_.clear();
    avatar_ = core::spawnPlayerAvatarRig(ecs, renderer, riggedMeshLibrary, look, catalogueIndex, avatarEntities_);
    for (core::EntityId entity : avatarEntities_) ecs.addComponent<core::PlayerAvatarPart>(entity, controller_.entity());

    editorCamera_ = camera;
    shiftLocked_ = false;
    shiftKeyWasDown_ = true; // a Shift held while pressing Play shouldn't lock straight away
    input_.setShiftLock(false);
    input_.setRelativeMouseMode(false);
    active_ = true;
    return true;
}

void PlaySoloPlayer::tick(float dt, core::ECS& ecs, core::Physics& physics, core::Camera& camera,
                          bool pointerInViewport, bool typing, float mouseWheel) {
    if (!active_) return;
    input_.setBlocked(typing);
    input_.setPointerOverUi(!pointerInViewport && !input_.isOrbitDragging());
    input_.addMouseWheel(mouseWheel);
    input_.update();

    const bool shiftDown = input_.isActionDown("ShiftLock");
    if (shiftDown && !shiftKeyWasDown_) {
        shiftLocked_ = !shiftLocked_;
        input_.setRelativeMouseMode(shiftLocked_);
    }
    shiftKeyWasDown_ = shiftDown;
    input_.setShiftLock(shiftLocked_);

    controller_.tick(dt, ecs, physics, input_, camera, avatar_.get(), avatar_ ? &avatarEntities_ : nullptr);
}

void PlaySoloPlayer::end(core::ECS& ecs, core::Camera& camera) {
    if (!active_) return;
    for (core::EntityId entity : avatarEntities_) {
        if (ecs.raw().valid(entity)) ecs.destroyEntity(entity);
    }
    avatarEntities_.clear();
    avatar_.reset();
    shiftLocked_ = false;
    input_.setShiftLock(false);
    input_.setRelativeMouseMode(false);
    camera = editorCamera_;
    active_ = false;
}

} // namespace engine::studio
