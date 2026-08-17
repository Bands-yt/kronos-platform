#include "runtime/HomeAvatarPreview.hpp"

#include <cmath>
#include <cstdio>

#include "core/Animation.hpp"
#include "core/AvatarIdleClipResolution.hpp"
#include "core/AvatarSkinTone.hpp"
#include "core/Components.hpp"
#include "core/Renderer.hpp"
#include "core/ResourcePaths.hpp"

namespace engine::runtime {

namespace {
std::string shippedIdleClipPath() {
    return engine::core::resolveResourceDir(engine::core::executableDirectory(), "assets", ENGINE_ASSET_DIR) +
           "/animations/idle.anim";
}
} // namespace

HomeAvatarPreview::HomeAvatarPreview(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                      core::RiggedMeshLibrary& riggedMeshLibrary, const core::LocalProfile& localProfile,
                                      const core::CatalogueIndex& catalogueIndex, const core::AvatarLoadout& loadout,
                                      const core::AnimationDatabase& animationDatabase)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      riggedMeshLibrary_(&riggedMeshLibrary),
      localProfile_(&localProfile),
      catalogueIndex_(&catalogueIndex),
      loadout_(&loadout),
      animationDatabase_(&animationDatabase),
      skeleton_(core::buildHumanoidSkeleton()) {
    spawnPreviewBody();
}

void HomeAvatarPreview::spawnPreviewBody() {
    for (core::EntityId entity : skinnedEntities_) scene_.ecs().destroyEntity(entity);
    skinnedEntities_.clear();

    std::string error;
    glm::vec4 initialTone = core::resolveSkinToneColor(localProfile_->skinToneIndex);
    core::HeadShape initialHeadShape = core::headShapeFromIndex(localProfile_->headShapeIndex);
    core::BodyProportions proportions{localProfile_->bodyHeight, localProfile_->bodyWidth, localProfile_->bodyLimbScale,
                                       localProfile_->bodyTorsoLength, localProfile_->bodyShoulderWidth};
    core::Skeleton scaledSkeleton = core::applyBodyProportionsToSkeleton(skeleton_, proportions);
    if (!core::spawnRiggedAvatar(scene_.ecs(), scaledSkeleton, *loadout_, *catalogueIndex_, *riggedMeshLibrary_,
                                  allocator_, device_, cmdPool_, queue_, skinnedEntities_, error, initialTone,
                                  initialHeadShape, proportions)) {
        std::fprintf(stderr, "HomeAvatarPreview: failed to spawn preview body: %s\n", error.c_str());
    }

    // Kronos ("Avatar 2.0" -- "Runtime Integration" -- "Ensure Home
    // avatar preview supports facial expressions"): real -- same
    // spawnAvatarFace() call core::Application::spawnLocalPlayerAvatar()
    // makes for the real gameplay avatar, folded into the same
    // skinnedEntities_ list so update()'s existing "assign the current
    // pose to every skinned entity" loop covers the face for free.
    std::vector<core::EntityId> faceEntities;
    std::string faceError;
    if (core::spawnAvatarFace(scene_.ecs(), scaledSkeleton, initialTone, *riggedMeshLibrary_, allocator_, device_,
                               cmdPool_, queue_, faceEntities, faceError)) {
        skinnedEntities_.insert(skinnedEntities_.end(), faceEntities.begin(), faceEntities.end());
    } else {
        std::fprintf(stderr, "HomeAvatarPreview: failed to spawn face: %s\n", faceError.c_str());
    }

    // Kronos ("Avatar 2.0" -- "Clothing Meshes" -- "Runtime Integration"):
    // real, reads the real, persisted fit choice straight off
    // localProfile_ (this preview always has one, unlike Application::
    // spawnLocalPlayerAvatar()).
    std::vector<core::EntityId> clothingEntities;
    std::string clothingError;
    core::ClothingFit fit = core::clothingFitFromIndex(localProfile_->clothingFitIndex);
    if (core::spawnAvatarClothing(scene_.ecs(), scaledSkeleton, *loadout_, *catalogueIndex_, proportions, fit,
                                   *riggedMeshLibrary_, allocator_, device_, cmdPool_, queue_, clothingEntities,
                                   clothingError)) {
        skinnedEntities_.insert(skinnedEntities_.end(), clothingEntities.begin(), clothingEntities.end());
    } else {
        std::fprintf(stderr, "HomeAvatarPreview: failed to spawn clothing: %s\n", clothingError.c_str());
    }

    std::vector<core::EntityId> accessoryEntities;
    std::string accessoryError;
    if (core::spawnAvatarAccessories(scene_.ecs(), scaledSkeleton, *loadout_, *catalogueIndex_, *riggedMeshLibrary_,
                                      allocator_, device_, cmdPool_, queue_, accessoryEntities, accessoryError)) {
        skinnedEntities_.insert(skinnedEntities_.end(), accessoryEntities.begin(), accessoryEntities.end());
    } else {
        std::fprintf(stderr, "HomeAvatarPreview: failed to spawn accessories: %s\n", accessoryError.c_str());
    }

    // Kronos ("Avatar Visual Silhouette Pass" -- "Head and Hair"): real,
    // same fold pattern as face/clothing/accessories above.
    std::vector<core::EntityId> hairEntities;
    std::string hairError;
    if (core::spawnAvatarDefaultHair(scene_.ecs(), scaledSkeleton, *loadout_, core::kDefaultHairColor,
                                      *riggedMeshLibrary_, allocator_, device_, cmdPool_, queue_, hairEntities,
                                      hairError)) {
        skinnedEntities_.insert(skinnedEntities_.end(), hairEntities.begin(), hairEntities.end());
    } else {
        std::fprintf(stderr, "HomeAvatarPreview: failed to spawn hair: %s\n", hairError.c_str());
    }

    previewPlayer_ = std::make_unique<core::AnimationPlayer>(scaledSkeleton);
    // Kronos ("Avatar 2.0" -- "Performance and LOD" -- "cache rig
    // transforms"): real, computed once here (not every real update()
    // tick) -- see cachedBindPose_'s own header comment.
    cachedBindPose_ = scaledSkeleton.bindPoseMatrices();

    // Kronos ("Home Screen Avatar Preview" -- "idle animation"): real,
    // automatic -- unlike AvatarEditor's own demo body (which starts
    // idle-empty until a slider/combo is touched), the Home preview
    // always plays *something* the moment it's visible. See
    // core::resolveAvatarIdleClipPath()'s own header comment for the
    // real, pure, separately-tested resolution logic.
    std::string idleClipPath =
        core::resolveAvatarIdleClipPath(*localProfile_, *animationDatabase_, shippedIdleClipPath());
    core::AnimationClip idleClip;
    if (idleClip.loadFromFile(idleClipPath)) {
        previewPlayer_->play(std::move(idleClip), core::AnimationLayer::Base, /*looping=*/true, /*fadeSeconds=*/0.0f);
    }
}

void HomeAvatarPreview::refresh() { spawnPreviewBody(); }

void HomeAvatarPreview::update(float dt) {
    if (!previewPlayer_) return;
    previewPlayer_->tick(dt);
    (void)previewPlayer_->consumeFiredEvents(); // no consumer wired up here -- draining keeps the queue from growing unbounded

    // Kronos ("Avatar 2.0" -- "Facial System"): real, same small,
    // periodic auto-blink AvatarController's own gameplay path
    // establishes -- see this class's own header comment on why it's
    // duplicated here rather than shared.
    autoBlinkTimer_ -= dt;
    if (autoBlinkTimer_ <= 0.0f && autoBlinkProgress_ < 0.0f) {
        autoBlinkProgress_ = 0.0f;
        autoBlinkTimer_ = 3.0f;
    }
    if (autoBlinkProgress_ >= 0.0f) {
        autoBlinkProgress_ += dt;
        float durationFraction = autoBlinkProgress_ / 0.15f;
        if (durationFraction >= 1.0f) {
            autoBlinkProgress_ = -1.0f;
            facialExpression_.blinkWeight = 0.0f;
        } else {
            facialExpression_.blinkWeight = std::sin(durationFraction * 3.14159265f);
        }
    }

    accessorySwayPhase_ = std::fmod(accessorySwayPhase_ + dt * 1.2f, 6.28318530718f);

    std::vector<glm::mat4> matrices = previewPlayer_->skinningMatrices();
    core::applyFacialExpressionToSkinningMatrices(matrices, previewPlayer_->skeleton(), cachedBindPose_,
                                                   facialExpression_);
    core::applyAccessoryDynamicsToSkinningMatrices(matrices, previewPlayer_->skeleton(), cachedBindPose_,
                                                    accessorySwayPhase_);
    for (core::EntityId entity : skinnedEntities_) {
        if (auto* skinned = scene_.ecs().tryGetComponent<core::SkinnedRenderable>(entity)) {
            skinned->skinningMatrices = matrices;
        }
    }

    // Kronos ("Avatar 2.0" -- "Performance and LOD" -- "Ensure runtime
    // and Home preview both use the optimized pipeline"): real, same
    // exact live orbit-distance source AvatarEditor::update() uses --
    // see PreviewScene::orbitDistance()'s own comment.
    core::updateAvatarLOD(scene_.ecs(), skinnedEntities_, scene_.orbitDistance());
}

void HomeAvatarPreview::draw() { scene_.drawAndHandleOrbit(); }

void HomeAvatarPreview::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    // No plain-Renderable/particle content of its own -- same real
    // "unused-but-required" pattern every other PreviewScene-owning
    // class already establishes (see AvatarEditor::renderPreview()).
    static core::MeshLibrary sUnusedMeshLibrary;
    static core::TextureLibrary sUnusedTextureLibrary;
    const core::SceneLighting& lighting = core::avatarIndoorPreviewLighting();
    scene_.render(cmd, renderer, sUnusedMeshLibrary, sUnusedTextureLibrary, riggedMeshLibrary_, &lighting);
}

void HomeAvatarPreview::shutdown(core::Renderer& renderer) { scene_.destroy(renderer, allocator_, device_); }

} // namespace engine::runtime
