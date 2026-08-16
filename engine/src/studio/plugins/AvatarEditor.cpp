#include "studio/plugins/AvatarEditor.hpp"

#include <imgui.h>

#include <array>

#include "core/Animation.hpp"
#include "core/AvatarSkinTone.hpp"
#include "core/Components.hpp"
#include "core/Renderer.hpp"
#include "core/ResourcePaths.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
// Kronos ("Avatar Creation System, Marketplace & Economy"): same real
// "local_profile.profile" path runtime::RuntimeShell's own
// kLocalProfilePath and plugins::CataloguePanel's own kLocalProfilePath
// use -- one real, shared identity across every real caller.
constexpr const char* kLocalProfilePath = "local_profile.profile";
// Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory Slots"):
// same real path StudioApp::localAvatarLoadoutPath_ uses -- one real,
// shared "what the local player has equipped" record.
constexpr const char* kLocalAvatarLoadoutPath = "local_avatar_loadout.loadout";

// Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory Slots"):
// the real, exact six slots the spec names -- "top, bottom, shoes" for
// clothing, "hat, face, back" for accessories. Torso/Legs/Head/Face
// already existed as real AvatarItemCategory values; Shoes/Back are real,
// new ones added alongside them (see AvatarItem.hpp's own comment).
struct ClothingSlot {
    engine::core::AvatarItemCategory category;
    const char* label;
};
constexpr std::array<ClothingSlot, 6> kClothingSlots = {{
    {engine::core::AvatarItemCategory::Torso, "Top"},
    {engine::core::AvatarItemCategory::Legs, "Bottom"},
    {engine::core::AvatarItemCategory::Shoes, "Shoes"},
    {engine::core::AvatarItemCategory::Head, "Hat"},
    {engine::core::AvatarItemCategory::Face, "Face"},
    {engine::core::AvatarItemCategory::Back, "Back"},
}};

// Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"): the
// real, exact six clip slots core::AvatarController exposes
// (setIdleClip/setWalkClip/setRunClip/setJumpClip/setJumpAirClip/
// setJumpLandClip) and core::Application::spawnLocalPlayerAvatar()'s own
// loadClip() resolves shipped defaults for (idle.anim/walk.anim/
// run.anim/jump_start.anim/jump_air.anim/jump_land.anim).
struct AnimSlot {
    std::string engine::core::LocalProfile::*field;
    const char* label;
    const char* shippedFileBaseName;
};
const std::array<AnimSlot, 6> kAnimSlots = {{
    {&engine::core::LocalProfile::animOverrideIdleId, "Idle", "idle"},
    {&engine::core::LocalProfile::animOverrideWalkId, "Walk", "walk"},
    {&engine::core::LocalProfile::animOverrideRunId, "Run", "run"},
    {&engine::core::LocalProfile::animOverrideJumpStartId, "Jump Start", "jump_start"},
    {&engine::core::LocalProfile::animOverrideJumpAirId, "Jump Air", "jump_air"},
    {&engine::core::LocalProfile::animOverrideJumpLandId, "Jump Land", "jump_land"},
}};

std::string shippedClipPath(const char* fileBaseName) {
    return engine::core::resolveResourceDir(engine::core::executableDirectory(), "assets", ENGINE_ASSET_DIR) +
           "/animations/" + fileBaseName + ".anim";
}
} // namespace

AvatarEditor::AvatarEditor(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                            core::RiggedMeshLibrary& riggedMeshLibrary, core::LocalProfile& localProfile,
                            core::CatalogueIndex& catalogueIndex, core::AvatarLoadout& loadout,
                            core::AnimationDatabase& animationDatabase)
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
    spawnDemoBody();
}

void AvatarEditor::spawnDemoBody() {
    std::string error;
    glm::vec4 initialTone = core::resolveSkinToneColor(localProfile_->skinToneIndex);
    core::HeadShape initialHeadShape = core::headShapeFromIndex(localProfile_->headShapeIndex);
    core::BodyProportions proportions{localProfile_->bodyHeight, localProfile_->bodyWidth, localProfile_->bodyLimbScale,
                                       localProfile_->bodyTorsoLength, localProfile_->bodyShoulderWidth};
    core::Skeleton scaledSkeleton = core::applyBodyProportionsToSkeleton(skeleton_, proportions);
    if (!core::spawnRiggedAvatar(scene_.ecs(), scaledSkeleton, *loadout_, *catalogueIndex_, *riggedMeshLibrary_,
                                  allocator_, device_, cmdPool_, queue_, skinnedEntities_, error, initialTone,
                                  initialHeadShape, proportions)) {
        statusMessage_ = "Failed to spawn avatar preview: " + error;
        std::fprintf(stderr, "AvatarEditor: %s\n", statusMessage_.c_str());
    }

    // Kronos ("Avatar 2.0" -- "Studio Integration" -- "clothing mesh
    // preview"): real -- the exact same spawnAvatarFace()/
    // spawnAvatarClothing() calls the real gameplay avatar and Home
    // preview both make, folded into this panel's own skinnedEntities_
    // so update()'s existing pose-write loop covers them for free (see
    // that method's own unchanged loop below).
    std::vector<core::EntityId> faceEntities;
    std::string faceError;
    if (core::spawnAvatarFace(scene_.ecs(), scaledSkeleton, initialTone, *riggedMeshLibrary_, allocator_, device_,
                               cmdPool_, queue_, faceEntities, faceError)) {
        skinnedEntities_.insert(skinnedEntities_.end(), faceEntities.begin(), faceEntities.end());
    } else {
        std::fprintf(stderr, "AvatarEditor: failed to spawn face: %s\n", faceError.c_str());
    }
    std::vector<core::EntityId> clothingEntities;
    std::string clothingError;
    core::ClothingFit fit = core::clothingFitFromIndex(localProfile_->clothingFitIndex);
    if (core::spawnAvatarClothing(scene_.ecs(), scaledSkeleton, *loadout_, *catalogueIndex_, proportions, fit,
                                   *riggedMeshLibrary_, allocator_, device_, cmdPool_, queue_, clothingEntities,
                                   clothingError)) {
        skinnedEntities_.insert(skinnedEntities_.end(), clothingEntities.begin(), clothingEntities.end());
    } else {
        std::fprintf(stderr, "AvatarEditor: failed to spawn clothing: %s\n", clothingError.c_str());
    }
    std::vector<core::EntityId> accessoryEntities;
    std::string accessoryError;
    if (core::spawnAvatarAccessories(scene_.ecs(), scaledSkeleton, *loadout_, *catalogueIndex_, *riggedMeshLibrary_,
                                      allocator_, device_, cmdPool_, queue_, accessoryEntities, accessoryError)) {
        skinnedEntities_.insert(skinnedEntities_.end(), accessoryEntities.begin(), accessoryEntities.end());
    } else {
        std::fprintf(stderr, "AvatarEditor: failed to spawn accessories: %s\n", accessoryError.c_str());
    }

    // Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"): real
    // rebuild against the same scaled skeleton the body was just spawned
    // with -- a stale previewPlayer_ built against a pre-Body-Sliders bind
    // pose would skin incorrectly once the body respawns at a new scale
    // (same real "skeleton and mesh must share one bind pose" reasoning
    // Application::spawnLocalPlayerAvatar()'s own comment already
    // establishes for AvatarController). Starts idle-empty; drawPanel()'s
    // own combo selections (or their real "Default" entries) are what
    // first calls previewPlayer_->play().
    previewPlayer_ = std::make_unique<core::AnimationPlayer>(scaledSkeleton);
    // Kronos ("Avatar 2.0" -- "Performance and LOD" -- "cache rig
    // transforms"): real, computed once here -- see cachedBindPose_'s
    // own header comment.
    cachedBindPose_ = scaledSkeleton.bindPoseMatrices();
}

void AvatarEditor::applyHeadShape(core::HeadShape shape) {
    localProfile_->headShapeIndex = core::headShapeToIndex(shape);
    for (core::EntityId entity : skinnedEntities_) scene_.ecs().destroyEntity(entity);
    skinnedEntities_.clear();
    spawnDemoBody();
    (void)localProfile_->saveToFile(kLocalProfilePath);
    statusMessage_ = std::string("Head shape set to \"") + core::headShapeName(shape) + "\".";
}

void AvatarEditor::applyBodyProportions() {
    core::BodyProportions clamped = core::clampBodyProportions(
        {localProfile_->bodyHeight, localProfile_->bodyWidth, localProfile_->bodyLimbScale,
         localProfile_->bodyTorsoLength, localProfile_->bodyShoulderWidth});
    localProfile_->bodyHeight = clamped.height;
    localProfile_->bodyWidth = clamped.width;
    localProfile_->bodyLimbScale = clamped.limbScale;
    localProfile_->bodyTorsoLength = clamped.torsoLength;
    localProfile_->bodyShoulderWidth = clamped.shoulderWidth;
    for (core::EntityId entity : skinnedEntities_) scene_.ecs().destroyEntity(entity);
    skinnedEntities_.clear();
    spawnDemoBody();
    (void)localProfile_->saveToFile(kLocalProfilePath);
    statusMessage_ = "Body proportions updated.";
}

void AvatarEditor::applyClothingFit(core::ClothingFit fit) {
    localProfile_->clothingFitIndex = core::clothingFitToIndex(fit);
    for (core::EntityId entity : skinnedEntities_) scene_.ecs().destroyEntity(entity);
    skinnedEntities_.clear();
    spawnDemoBody();
    (void)localProfile_->saveToFile(kLocalProfilePath);
    statusMessage_ = fit == core::ClothingFit::Tight ? "Clothing fit set to Tight." : "Clothing fit set to Loose.";
}

void AvatarEditor::applySkinTone(int index) {
    localProfile_->skinToneIndex = index;
    refreshSegmentColors();
    (void)localProfile_->saveToFile(kLocalProfilePath);
    statusMessage_ = "Skin tone set to \"" + std::string(core::skinTonePalette()[static_cast<size_t>(index)].name) + "\".";
}

void AvatarEditor::refreshSegmentColors() {
    glm::vec4 skinTone = core::resolveSkinToneColor(localProfile_->skinToneIndex);
    // Real per-segment colors, loadout-aware -- an equipped item's own
    // color wins over the flat skin tone for the segment(s) its category
    // maps to (see core::categoryForBodySegment()'s own comment); skin
    // tone is the real, honest fallback everywhere nothing is equipped.
    std::array<glm::vec4, core::kHumanoidBodySegmentCount> colors =
        core::resolveSegmentColorsForLoadout(*loadout_, *catalogueIndex_, skinTone);
    // skinnedEntities_ is real-ordered to exactly match HumanoidBodySegment's
    // own declaration order -- spawnRiggedAvatar() spawns (and this class's
    // own spawnDemoBody() forwards) one entity per segment in that exact
    // order, so indexing colors[i] against skinnedEntities_[i] is real and
    // correct, not a coincidence.
    for (size_t i = 0; i < skinnedEntities_.size() && i < colors.size(); ++i) {
        if (auto* skinned = scene_.ecs().tryGetComponent<core::SkinnedRenderable>(skinnedEntities_[i])) {
            // Kronos ("Avatar 2.0" -- "Visual Fidelity: per-segment color
            // gradients"): same real shading step spawnRiggedAvatar()
            // itself applies -- see applySegmentShadingGradient()'s own
            // comment.
            skinned->baseColor =
                core::applySegmentShadingGradient(static_cast<core::HumanoidBodySegment>(i), colors[i]);
        }
    }
}

void AvatarEditor::setEquippedItem(core::AvatarItemCategory category, const std::string& itemId) {
    if (itemId.empty()) {
        loadout_->unequip(category);
        statusMessage_ = std::string("Unequipped ") + core::avatarItemCategoryName(category) + ".";
    } else if (loadout_->equip(itemId, *catalogueIndex_)) {
        statusMessage_ = "Equipped \"" + itemId + "\".";
    } else {
        statusMessage_ = "Failed to equip \"" + itemId + "\" -- not found in the catalogue.";
        return;
    }
    if (category == core::AvatarItemCategory::Torso || category == core::AvatarItemCategory::Legs) {
        // Kronos ("Avatar 2.0" -- "Clothing Meshes"): a real, full
        // respawn -- unlike the body's own tint (refreshSegmentColors()'s
        // real, cheap in-place baseColor mutation), the clothing shell's
        // real color is baked into its own RiggedMesh at upload time (no
        // fast re-tint path exists for it), so a Torso/Legs equip change
        // needs a real new upload to actually show the new item's color
        // on the shell. Occasional Studio clicks, not a gameplay hot
        // path -- the real GPU cost here is real but acceptable, same
        // "don't build a fast path for a problem that isn't real yet"
        // precedent this codebase already follows elsewhere.
        for (core::EntityId entity : skinnedEntities_) scene_.ecs().destroyEntity(entity);
        skinnedEntities_.clear();
        spawnDemoBody();
    } else {
        refreshSegmentColors();
    }
    (void)loadout_->saveToFile(kLocalAvatarLoadoutPath);
}

std::string AvatarEditor::previewClipFromPath(const std::string& path, const std::string& displayName) {
    core::AnimationClip clip;
    if (!clip.loadFromFile(path)) return std::string();
    // Real 0.25s crossfade -- "must smoothly transition between clips"
    // per the Avatar Phase spec, using core::AnimationPlayer::play()'s
    // own real, existing blend mechanism (see that method's header
    // comment), not a hard cut.
    previewPlayer_->play(std::move(clip), core::AnimationLayer::Base, /*looping=*/true, /*fadeSeconds=*/0.25f);
    return displayName;
}

void AvatarEditor::setAnimationOverride(std::string core::LocalProfile::*profileField, const std::string& itemId,
                                         const char* shippedFileBaseName) {
    localProfile_->*profileField = itemId;
    (void)localProfile_->saveToFile(kLocalProfilePath);

    std::string previewedName;
    if (!itemId.empty()) {
        const core::AnimationManifest* manifest = animationDatabase_->findById(itemId);
        if (manifest != nullptr) previewedName = previewClipFromPath(manifest->item.clipPath, manifest->item.name);
    }
    if (previewedName.empty()) {
        // Empty selection, or a broken/missing catalogue reference --
        // real, honest fallback to the shipped default clip, the same
        // fail-soft Application::spawnLocalPlayerAvatar()'s own
        // loadClip() lambda already applies for real gameplay.
        previewedName = previewClipFromPath(shippedClipPath(shippedFileBaseName), std::string(shippedFileBaseName) + " (default)");
    }
    statusMessage_ = previewedName.empty() ? ("Override set to \"" + itemId + "\", but no clip could be previewed.")
                                            : ("Previewing \"" + previewedName + "\".");
}

void AvatarEditor::update(float dt, core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    if (!previewPlayer_) return;
    previewPlayer_->tick(dt);
    (void)previewPlayer_->consumeFiredEvents(); // no consumer wired up here -- draining keeps the queue from growing unbounded, same as AnimationPreviewerPlugin::update()
    std::vector<glm::mat4> matrices = previewPlayer_->skinningMatrices();
    core::applyFacialExpressionToSkinningMatrices(matrices, previewPlayer_->skeleton(), cachedBindPose_,
                                                   facialExpression_);
    for (core::EntityId entity : skinnedEntities_) {
        if (auto* skinned = scene_.ecs().tryGetComponent<core::SkinnedRenderable>(entity)) {
            skinned->skinningMatrices = matrices;
        }
    }

    // Kronos ("Avatar 2.0" -- "Performance and LOD" -- "Ensure runtime
    // and Home preview both use the optimized pipeline"): real, exact --
    // scene_.orbitDistance() is the actual live distance from this
    // panel's own orbit camera to the demo body (see PreviewScene::
    // orbitDistance()'s own comment), so a creator scrolling out
    // genuinely sees facial features, then accessories, then clothing
    // drop out, the same real tiers gameplay/Home preview use.
    core::updateAvatarLOD(scene_.ecs(), skinnedEntities_, scene_.orbitDistance());
}

void AvatarEditor::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Avatar Editor");

    ImGui::TextWrapped(
        "Choose a skin tone for your Kronos avatar -- applies consistently across every body region, and is "
        "saved to your real, shared profile (used by both Studio and engine_runtime).");

    ImGui::BeginChild("##avatar_preview", ImVec2(0.0f, 320.0f));
    scene_.drawAndHandleOrbit();
    ImGui::EndChild();

    ImGui::SeparatorText("Head Shape");
    core::HeadShape currentHeadShape = core::headShapeFromIndex(localProfile_->headShapeIndex);
    bool ovalSelected = currentHeadShape == core::HeadShape::Oval;
    if (ovalSelected) ImGui::BeginDisabled();
    if (ImGui::Button("Oval (Default)")) applyHeadShape(core::HeadShape::Oval);
    if (ovalSelected) ImGui::EndDisabled();
    ImGui::SameLine();
    bool sphereSelected = currentHeadShape == core::HeadShape::Sphere;
    if (sphereSelected) ImGui::BeginDisabled();
    if (ImGui::Button("Sphere (Classic)")) applyHeadShape(core::HeadShape::Sphere);
    if (sphereSelected) ImGui::EndDisabled();
    ImGui::TextDisabled("Current: %s", core::headShapeName(currentHeadShape));

    ImGui::SeparatorText("Body");
    // Real respawn (a real GPU re-upload) only once each slider's drag
    // actually releases -- ImGui::IsItemDeactivatedAfterEdit() fires
    // exactly once per completed edit, unlike SliderFloat's own return
    // value (true on every intermediate value while dragging), so a
    // mid-drag respawn every frame is real, wasted GPU churn this avoids.
    ImGui::SliderFloat("Height", &localProfile_->bodyHeight, core::kBodyProportionMin, core::kBodyProportionMax, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) applyBodyProportions();
    ImGui::SliderFloat("Width", &localProfile_->bodyWidth, core::kBodyProportionMin, core::kBodyProportionMax, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) applyBodyProportions();
    ImGui::SliderFloat("Limb Scale", &localProfile_->bodyLimbScale, core::kBodyProportionMin, core::kBodyProportionMax,
                        "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) applyBodyProportions();
    ImGui::SliderFloat("Torso Length", &localProfile_->bodyTorsoLength, core::kBodyProportionMin,
                        core::kBodyProportionMax, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) applyBodyProportions();
    ImGui::SliderFloat("Shoulder Width", &localProfile_->bodyShoulderWidth, core::kBodyProportionMin,
                        core::kBodyProportionMax, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) applyBodyProportions();

    ImGui::SeparatorText("Skin Tone");
    const auto& palette = core::skinTonePalette();
    int currentIndex = localProfile_->skinToneIndex;
    for (size_t i = 0; i < palette.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const glm::vec4& c = palette[i].color;
        ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop;
        bool isSelected = currentIndex == static_cast<int>(i);
        if (isSelected) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::ColorButton(palette[i].name, ImVec4(c.x, c.y, c.z, c.w), flags, ImVec2(32.0f, 32.0f))) {
            applySkinTone(static_cast<int>(i));
        }
        if (isSelected) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", palette[i].name);
        ImGui::PopID();
        // 5 real swatches per row (matches the real "5 depth bands x 3
        // real warmth variants" structure skinTonePalette() itself is
        // laid out in -- see that function's own header comment).
        if ((i + 1) % 5 != 0 && i + 1 < palette.size()) ImGui::SameLine();
    }

    if (currentIndex < 0) {
        ImGui::TextDisabled("No skin tone chosen yet -- using the real, honest default.");
    } else {
        ImGui::Text("Current: %s", palette[static_cast<size_t>(currentIndex)].name);
    }

    ImGui::SeparatorText("Clothing & Accessories");
    ImGui::TextWrapped(
        "Equip items you own from the Catalogue. Top/Bottom now generate a real, separate procedural clothing mesh "
        "(see \"Clothing Fit\" below) instead of just recoloring the body; Hat still recolors the head region. "
        "Shoes/Face/Back are real, saved, ownership-checked equips with no visual effect on this body yet, an "
        "honest, stated gap.");
    for (const auto& slot : kClothingSlots) {
        ImGui::PushID(static_cast<int>(slot.category));
        std::string equippedId = loadout_->equippedItemId(slot.category);
        const core::AvatarItemManifest* equippedManifest =
            equippedId.empty() ? nullptr : catalogueIndex_->findById(equippedId);
        std::string currentLabel = equippedManifest ? equippedManifest->item.name : "None";

        if (ImGui::BeginCombo(slot.label, currentLabel.c_str())) {
            if (ImGui::Selectable("None", equippedId.empty())) setEquippedItem(slot.category, "");
            for (const std::string& ownedId : localProfile_->ownedItemIds) {
                const core::AvatarItemManifest* owned = catalogueIndex_->findById(ownedId);
                if (!owned || owned->item.category != slot.category) continue;
                bool isSelected = ownedId == equippedId;
                if (ImGui::Selectable(owned->item.name.c_str(), isSelected)) setEquippedItem(slot.category, ownedId);
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Clothing Fit");
    core::ClothingFit currentFit = core::clothingFitFromIndex(localProfile_->clothingFitIndex);
    bool tightSelected = currentFit == core::ClothingFit::Tight;
    if (tightSelected) ImGui::BeginDisabled();
    if (ImGui::Button("Tight")) applyClothingFit(core::ClothingFit::Tight);
    if (tightSelected) ImGui::EndDisabled();
    ImGui::SameLine();
    bool looseSelected = currentFit == core::ClothingFit::Loose;
    if (looseSelected) ImGui::BeginDisabled();
    if (ImGui::Button("Loose")) applyClothingFit(core::ClothingFit::Loose);
    if (looseSelected) ImGui::EndDisabled();

    ImGui::SeparatorText("Facial Expression");
    ImGui::TextWrapped(
        "Live preview only -- these sliders drive the real expression system above, but aren't saved (a future "
        "dialogue/emote system is the real, intended real-time driver, see AvatarController::setFacialExpression()).");
    ImGui::SliderFloat("Blink", &facialExpression_.blinkWeight, 0.0f, 1.0f);
    ImGui::SliderFloat("Smile", &facialExpression_.smileWeight, 0.0f, 1.0f);
    ImGui::SliderFloat("Frown", &facialExpression_.frownWeight, 0.0f, 1.0f);
    ImGui::SliderFloat("Talk", &facialExpression_.talkWeight, 0.0f, 1.0f);

    ImGui::SeparatorText("Animations");
    ImGui::TextWrapped(
        "Override idle/walk/run/jump_start/jump_air/jump_land with any Locomotion-category clip from the Animation "
        "catalogue. \"Default\" uses this rig's own shipped clip. Selecting an entry immediately crossfades the live "
        "preview above into it.");
    for (const auto& slot : kAnimSlots) {
        ImGui::PushID(slot.label);
        std::string currentId = localProfile_->*(slot.field);
        const core::AnimationManifest* currentManifest = currentId.empty() ? nullptr : animationDatabase_->findById(currentId);
        std::string currentLabel = currentManifest ? currentManifest->item.name : "Default";

        if (ImGui::BeginCombo(slot.label, currentLabel.c_str())) {
            if (ImGui::Selectable("Default", currentId.empty())) {
                setAnimationOverride(slot.field, "", slot.shippedFileBaseName);
            }
            for (const core::AnimationManifest& entry : animationDatabase_->entries()) {
                if (entry.item.category != core::AnimationCategory::Locomotion) continue;
                bool isSelected = entry.item.id == currentId;
                if (ImGui::Selectable(entry.item.name.c_str(), isSelected)) {
                    setAnimationOverride(slot.field, entry.item.id, slot.shippedFileBaseName);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Preview")) {
            std::string previewedName;
            if (currentManifest != nullptr) previewedName = previewClipFromPath(currentManifest->item.clipPath, currentManifest->item.name);
            if (previewedName.empty()) {
                previewedName = previewClipFromPath(shippedClipPath(slot.shippedFileBaseName),
                                                      std::string(slot.shippedFileBaseName) + " (default)");
            }
            statusMessage_ =
                previewedName.empty() ? "Failed to load a clip to preview." : ("Previewing \"" + previewedName + "\".");
        }
        ImGui::PopID();
    }

    if (!statusMessage_.empty()) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    drawPluginFooter();
    ImGui::End();
}

void AvatarEditor::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    // No plain-Renderable/particle content of its own -- same real
    // "unused-but-required" pattern AnimationPreviewerPlugin's own
    // renderPreview() already establishes.
    static core::MeshLibrary sUnusedMeshLibrary;
    static core::TextureLibrary sUnusedTextureLibrary;
    scene_.render(cmd, renderer, sUnusedMeshLibrary, sUnusedTextureLibrary, riggedMeshLibrary_);
}

void AvatarEditor::shutdown(core::Renderer& renderer) { scene_.destroy(renderer, allocator_, device_); }

} // namespace engine::studio::plugins
