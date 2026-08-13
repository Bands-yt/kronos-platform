#include "core/AvatarLoadoutSync.hpp"

#include <cstdio>
#include <vector>

#include "core/AvatarAttachment.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/Components.hpp"
#include "core/ObjLoader.hpp"

namespace engine::core {

glm::vec3 defaultAttachmentOffset(AvatarItemCategory category) {
    switch (category) {
        case AvatarItemCategory::Head: return glm::vec3(0.0f, 0.85f, 0.0f);
        case AvatarItemCategory::Hair: return glm::vec3(0.0f, 0.95f, 0.0f);
        case AvatarItemCategory::Face: return glm::vec3(0.0f, 0.82f, 0.12f);
        case AvatarItemCategory::Torso: return glm::vec3(0.0f, 0.35f, 0.0f);
        case AvatarItemCategory::Legs: return glm::vec3(0.0f, -0.35f, 0.0f);
        case AvatarItemCategory::Accessory: return glm::vec3(0.0f, 0.5f, 0.0f);
        case AvatarItemCategory::LayeredClothing: return glm::vec3(0.0f, 0.36f, 0.0f);
        case AvatarItemCategory::Emote: return glm::vec3(0.0f);
    }
    return glm::vec3(0.0f);
}

namespace {

uint32_t loadOrGetCachedMesh(const std::string& path, MeshLibrary& meshLibrary, AssetCache<uint32_t>& cache,
                              VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    uint32_t cached = 0;
    if (cache.tryGet(path, cached)) return cached;

    ObjLoadResult obj = loadObj(path);
    if (!obj.succeeded) {
        std::fprintf(stderr, "AvatarLoadoutSync: failed to parse \"%s\": %s\n", path.c_str(), obj.error.c_str());
        return Renderable::kInvalidHandle;
    }
    Mesh mesh;
    if (!mesh.uploadFromHost(allocator, device, cmdPool, queue, obj.vertices, obj.indices)) {
        std::fprintf(stderr, "AvatarLoadoutSync: GPU upload failed for \"%s\"\n", path.c_str());
        return Renderable::kInvalidHandle;
    }
    uint32_t handle = meshLibrary.registerMesh(std::move(mesh));
    cache.put(path, handle);
    return handle;
}

uint32_t loadOrGetCachedTexture(const std::string& path, TextureLibrary& textureLibrary, AssetCache<uint32_t>& cache,
                                 VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    uint32_t cached = 0;
    if (cache.tryGet(path, cached)) return cached;

    Texture texture = Texture::loadFromFile(path, allocator, device, cmdPool, queue, /*srgb=*/true);
    if (!texture.isValid()) {
        std::fprintf(stderr, "AvatarLoadoutSync: failed to load texture \"%s\"\n", path.c_str());
        return TextureLibrary::kInvalidHandle;
    }
    uint32_t handle = textureLibrary.registerTexture(std::move(texture));
    cache.put(path, handle);
    return handle;
}

} // namespace

void applyLoadoutToAvatar(const AvatarLoadout& loadout, EntityId avatarRoot, ECS& ecs, const CatalogueIndex& index,
                          MeshLibrary& meshLibrary, TextureLibrary& textureLibrary, AssetCache<uint32_t>& meshCache,
                          AssetCache<uint32_t>& textureCache, VmaAllocator allocator, VkDevice device,
                          VkCommandPool cmdPool, VkQueue queue) {
    // Collected first, then destroyed -- calling destroyEntity() while
    // iterating the same view it mutates is undefined behavior.
    std::vector<EntityId> stale;
    for (auto entity : ecs.view<AttachedTo>()) {
        const auto* attachment = ecs.tryGetComponent<AttachedTo>(entity);
        if (attachment != nullptr && attachment->parent == avatarRoot) stale.push_back(entity);
    }
    for (EntityId entity : stale) ecs.destroyEntity(entity);

    for (const auto& [category, itemId] : loadout.equippedItems()) {
        if (category == AvatarItemCategory::Emote) continue; // no visual attachment, see header comment

        const AvatarItemManifest* entry = index.findById(itemId);
        if (entry == nullptr) {
            std::fprintf(stderr, "AvatarLoadoutSync: equipped item \"%s\" not found in catalogue, skipping\n",
                         itemId.c_str());
            continue;
        }

        uint32_t meshHandle =
            loadOrGetCachedMesh(entry->item.meshPath, meshLibrary, meshCache, allocator, device, cmdPool, queue);
        if (meshHandle == Renderable::kInvalidHandle) continue; // already logged

        EntityId child = ecs.createEntity(entry->item.name);
        auto& renderable = ecs.addComponent<Renderable>(child);
        renderable.meshHandle = meshHandle;
        renderable.baseColor = entry->item.baseColor;
        renderable.metallic = entry->item.metallic;
        renderable.roughness = entry->item.roughness;

        if (!entry->item.texturePath.empty()) {
            uint32_t textureHandle = loadOrGetCachedTexture(entry->item.texturePath, textureLibrary, textureCache,
                                                              allocator, device, cmdPool, queue);
            if (textureHandle != TextureLibrary::kInvalidHandle) renderable.albedoTexture = textureHandle;
        }

        auto& meshSource = ecs.addComponent<MeshSource>(child);
        meshSource.kind = MeshSourceKind::Obj;
        meshSource.path = entry->item.meshPath;

        auto& attachment = ecs.addComponent<AttachedTo>(child);
        attachment.parent = avatarRoot;
        attachment.localOffset = defaultAttachmentOffset(category);
    }

    updateAttachments(ecs);
}

} // namespace engine::core
