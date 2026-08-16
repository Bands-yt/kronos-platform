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

// Kronos ("Home Screen Avatar Preview" -- "cinematic lighting preset"):
// a real, deliberately different look from studio::PreviewScene's own
// flat "studio lightbox" default (see that file's own previewLighting())
// -- a warm, low-key directional key light (dimmer ambient than the
// lightbox default, for real contrast/shadow instead of flat, even
// fill) plus a cool rim point light placed behind-and-above the focus
// point for silhouette separation from the background, the standard
// three-point-lighting-inspired combination a real character showcase
// uses. Real, already-existing engine features (SceneLighting's own
// ambient/directional terms, Sprint 16's real point lights) -- not a new
// rendering capability, just a deliberately different real preset.
const core::SceneLighting& cinematicPreviewLighting() {
    static const core::SceneLighting kLighting = [] {
        core::SceneLighting lighting;
        lighting.directionWS = glm::vec3(-0.55f, -0.65f, -0.3f);
        lighting.color = glm::vec3(1.0f, 0.92f, 0.78f); // warm key
        lighting.intensity = 2.6f;
        lighting.ambient = glm::vec3(0.06f, 0.07f, 0.11f);       // dim, cool sky fill
        lighting.ambientGround = glm::vec3(0.04f, 0.035f, 0.03f); // dim, warm ground fill
        lighting.pointLights.push_back(core::SceneLighting::PointLight{
            glm::vec3(-1.4f, 2.4f, 1.6f), // behind-and-above the real {0,1,0} focus point
            6.0f,
            glm::vec3(0.55f, 0.7f, 1.0f), // cool rim
            2.2f,
        });
        return lighting;
    }();
    return kLighting;
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

    previewPlayer_ = std::make_unique<core::AnimationPlayer>(scaledSkeleton);

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

    std::vector<glm::mat4> matrices = previewPlayer_->skinningMatrices();
    core::applyFacialExpressionToSkinningMatrices(matrices, previewPlayer_->skeleton(), facialExpression_);
    for (core::EntityId entity : skinnedEntities_) {
        if (auto* skinned = scene_.ecs().tryGetComponent<core::SkinnedRenderable>(entity)) {
            skinned->skinningMatrices = matrices;
        }
    }
}

void HomeAvatarPreview::draw() { scene_.drawAndHandleOrbit(); }

void HomeAvatarPreview::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    // No plain-Renderable/particle content of its own -- same real
    // "unused-but-required" pattern every other PreviewScene-owning
    // class already establishes (see AvatarEditor::renderPreview()).
    static core::MeshLibrary sUnusedMeshLibrary;
    static core::TextureLibrary sUnusedTextureLibrary;
    const core::SceneLighting& lighting = cinematicPreviewLighting();
    scene_.render(cmd, renderer, sUnusedMeshLibrary, sUnusedTextureLibrary, riggedMeshLibrary_, &lighting);
}

void HomeAvatarPreview::shutdown(core::Renderer& renderer) { scene_.destroy(renderer, allocator_, device_); }

} // namespace engine::runtime
