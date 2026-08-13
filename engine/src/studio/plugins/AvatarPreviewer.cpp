#include "studio/plugins/AvatarPreviewer.hpp"

#include <imgui.h>

#include "core/AvatarLoadoutSync.hpp"
#include "core/Components.hpp"
#include "core/EmoteSystem.hpp"
#include "studio/plugins/AnimationPreviewerPlugin.hpp"

namespace engine::studio::plugins {

AvatarPreviewer::AvatarPreviewer(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                  core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary,
                                  core::AnimationDatabase& animationDatabase, AnimationPreviewerPlugin& animationPreviewer)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      meshLibrary_(&meshLibrary),
      textureLibrary_(&textureLibrary),
      animationDatabase_(&animationDatabase),
      animationPreviewer_(&animationPreviewer) {
    spawnMannequin();
}

void AvatarPreviewer::spawnMannequin() {
    // A plain procedural capsule -- the same stand-in this engine's
    // player character already renders as (see CharacterController's
    // header comment), not a real humanoid rig. There is no
    // skeleton/bone system anywhere in this engine (see
    // AvatarAttachment.hpp's class comment); building one is real,
    // separate, substantial work this pass doesn't attempt to half-do.
    core::Mesh mannequinMesh = core::Mesh::createCapsule(allocator_, device_, cmdPool_, queue_, 0.4f, 0.55f);
    uint32_t meshHandle = meshLibrary_->registerMesh(std::move(mannequinMesh));

    mannequin_ = scene_.ecs().createEntity("Mannequin");
    auto& renderable = scene_.ecs().addComponent<core::Renderable>(mannequin_);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = {0.75f, 0.76f, 0.8f, 1.0f};
    renderable.metallic = 0.0f;
    renderable.roughness = 0.7f;
}

bool AvatarPreviewer::equipItem(const std::string& itemId, const core::CatalogueIndex& index, bool focusPanel) {
    const core::AvatarItemManifest* entry = index.findById(itemId);
    if (entry == nullptr) {
        statusMessage_ = "Item \"" + itemId + "\" not found in catalogue.";
        return false;
    }
    (void)loadout_.equip(itemId, index); // can't fail -- entry was just resolved above
    reapplyLoadout(index);

    if (entry->item.category == core::AvatarItemCategory::Emote) {
        // No mannequin entity to update (applyLoadoutToAvatar() already
        // skips Emote -- see its own comment) -- hand the real clip to
        // the Animation Previewer instead, see this method's header
        // comment.
        core::AnimationClip clip;
        std::string resolveError;
        if (core::resolveEmoteClip(itemId, *animationDatabase_, clip, resolveError)) {
            animationPreviewer_->previewClip(std::move(clip), entry->item.name);
            statusMessage_ = "Equipped emote \"" + entry->item.name + "\" -- playing in the Animation Previewer.";
            if (focusPanel) animationPreviewer_->setOpen(true);
        } else {
            statusMessage_ = "Equipped \"" + entry->item.name + "\", but couldn't preview it: " + resolveError;
        }
        return true;
    }

    if (focusPanel) setOpen(true);
    statusMessage_ = "Equipped \"" + entry->item.name + "\".";
    return true;
}

void AvatarPreviewer::reapplyLoadout(const core::CatalogueIndex& index) {
    core::applyLoadoutToAvatar(loadout_, mannequin_, scene_.ecs(), index, *meshLibrary_, *textureLibrary_, meshCache_,
                                textureCache_, allocator_, device_, cmdPool_, queue_);
}

void AvatarPreviewer::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    scene_.render(cmd, renderer, *meshLibrary_, *textureLibrary_);
}

void AvatarPreviewer::drawPanel(core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                 const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Avatar Previewer");

    ImGui::TextWrapped(
        "Drag to orbit, scroll to zoom. \"Try On\"/\"Equip\" from the Catalogue panel populate this mannequin.");

    if (ImGui::Button("Reset Preview")) {
        loadout_.clear();
        scene_.reset();
        spawnMannequin();
        statusMessage_ = "Preview reset.";
    }
    if (!statusMessage_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Equipped:");
    if (loadout_.equippedItems().empty()) {
        ImGui::TextDisabled("(nothing equipped)");
    } else {
        for (const auto& [category, itemId] : loadout_.equippedItems()) {
            ImGui::BulletText("%s: %s", core::avatarItemCategoryName(category), itemId.c_str());
        }
    }

    ImGui::Separator();
    scene_.drawAndHandleOrbit();

    ImGui::End();
}

void AvatarPreviewer::shutdown(core::Renderer& renderer) { scene_.destroy(renderer, allocator_, device_); }

} // namespace engine::studio::plugins
