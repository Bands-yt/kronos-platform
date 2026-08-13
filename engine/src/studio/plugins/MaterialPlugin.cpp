#include "studio/plugins/MaterialPlugin.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/Components.hpp"
#include "studio/MaterialPresets.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

MaterialPlugin::MaterialPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary),
      textureLibrary_(&textureLibrary) {
    for (auto& buffer : pathBuffers_) buffer[0] = '\0';
    // radius=0.5, halfHeight=0.0 -- see this class's header comment on
    // why a zero-height capsule is a real, exact UV sphere.
    previewSphereMesh_ = meshLibrary_->registerMesh(core::Mesh::createCapsule(allocator, device, cmdPool, queue, 0.5f, 0.0f));
}

void MaterialPlugin::ensurePreviewEntity() {
    if (previewEntity_ != core::kNullEntity) return;
    previewEntity_ = previewScene_.ecs().createEntity("MaterialPreview");
    auto& renderable = previewScene_.ecs().addComponent<core::Renderable>(previewEntity_);
    renderable.meshHandle = previewSphereMesh_;
}

void MaterialPlugin::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    previewScene_.render(cmd, renderer, *meshLibrary_, *textureLibrary_);
}

void MaterialPlugin::shutdown(core::Renderer& renderer) { previewScene_.destroy(renderer, allocator_, device_); }

void MaterialPlugin::drawTextureSlot(Slot slot, const char* label, core::Renderable& renderable) {
    ImGui::PushID(label);

    uint32_t* handle = nullptr;
    bool srgb = false;
    switch (slot) {
        case Slot::Albedo:
            handle = &renderable.albedoTexture;
            srgb = true;
            break;
        case Slot::Normal:
            handle = &renderable.normalTexture;
            srgb = false;
            break;
        case Slot::Metallic:
            handle = &renderable.metallicTexture;
            srgb = false;
            break;
        case Slot::Roughness:
            handle = &renderable.roughnessTexture;
            srgb = false;
            break;
        case Slot::AO:
            handle = &renderable.aoTexture;
            srgb = false;
            break;
    }

    char* pathBuffer = pathBuffers_[static_cast<size_t>(slot)];
    bool hasTexture = *handle != core::Renderable::kInvalidHandle;

    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::TextDisabled(hasTexture ? "(assigned)" : "(none -- flat value)");

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##path", pathBuffer, 256);
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        core::Texture texture = core::Texture::loadFromFile(pathBuffer, allocator_, device_, cmdPool_, queue_, srgb);
        if (texture.isValid()) {
            *handle = textureLibrary_->registerTexture(std::move(texture));
        }
    }
    if (hasTexture) {
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            *handle = core::Renderable::kInvalidHandle;
        }
    }

    ImGui::PopID();
}

void MaterialPlugin::drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) {
    ImGui::Begin("Material Editor");
    drawPluginHeader("Material Editor");

    auto* renderable = selected != core::kNullEntity ? ecs.tryGetComponent<core::Renderable>(selected) : nullptr;
    if (renderable == nullptr) {
        ImGui::TextDisabled("Select an entity with a Renderable component.");
        drawPluginFooter();
        ImGui::End();
        return;
    }

    ImGui::ColorEdit4("Base Color", &renderable->baseColor.x);
    ImGui::SliderFloat("Metallic", &renderable->metallic, 0.0f, 1.0f);
    // Roughness floors at 0.045, matching scene.frag's own clamp -- an
    // exact 0 roughness makes the GGX distribution singular (see that
    // shader's comment), so the slider can't ask for a value the pipeline
    // would immediately clamp away anyway.
    ImGui::SliderFloat("Roughness", &renderable->roughness, 0.045f, 1.0f);
    ImGui::SliderFloat("Normal Intensity", &renderable->normalIntensity, 0.0f, 3.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("(0 = ignore normal map, 1 = as-authored)");
    ImGui::ColorEdit3("Emissive Color", &renderable->emissiveColor.x);
    ImGui::SliderFloat("Emissive Intensity", &renderable->emissiveIntensity, 0.0f, 5.0f);
    ImGui::Checkbox("Visible", &renderable->visible);
    ImGui::SameLine();
    ImGui::Checkbox("Casts Shadow", &renderable->castsShadow);

    // Live preview sphere (Sprint 10 task category 1) -- copies the
    // *current* edited values onto the preview scene's own sphere entity
    // every single drawPanel() call, so orbiting it always shows exactly
    // what's being edited right now, live, not a stale snapshot from
    // whenever the panel was last interacted with.
    ImGui::SeparatorText("Live Preview");
    ensurePreviewEntity();
    if (auto* previewRenderable = previewScene_.ecs().tryGetComponent<core::Renderable>(previewEntity_)) {
        previewRenderable->baseColor = renderable->baseColor;
        previewRenderable->metallic = renderable->metallic;
        previewRenderable->roughness = renderable->roughness;
        previewRenderable->normalIntensity = renderable->normalIntensity;
        previewRenderable->emissiveColor = renderable->emissiveColor;
        previewRenderable->emissiveIntensity = renderable->emissiveIntensity;
    }
    ImGui::BeginChild("##material_preview", ImVec2(0.0f, 220.0f), true);
    previewScene_.drawAndHandleOrbit();
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextUnformatted("Presets (sets color, metallic, roughness, and emissive)");
    for (const MaterialPresetInfo& preset : kMaterialPresets) {
        if (ImGui::Button(preset.label)) {
            renderable->baseColor = preset.baseColor;
            renderable->metallic = preset.metallic;
            renderable->roughness = preset.roughness;
            renderable->emissiveColor = preset.emissiveColor;
            renderable->emissiveIntensity = preset.emissiveIntensity;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::SeparatorText("Texture Slots");
    ImGui::TextDisabled("Type a file path, click Load. Empty slot = flat value above.");
    drawTextureSlot(Slot::Albedo, "Albedo", *renderable);
    drawTextureSlot(Slot::Normal, "Normal", *renderable);
    drawTextureSlot(Slot::Metallic, "Metallic", *renderable);
    drawTextureSlot(Slot::Roughness, "Roughness", *renderable);
    drawTextureSlot(Slot::AO, "Ambient Occlusion", *renderable);

    ImGui::Separator();
    bool canApplyAll = selectedEntities.size() > 1;
    ImGui::BeginDisabled(!canApplyAll);
    if (ImGui::Button("Apply Material To All Selected")) {
        for (core::EntityId entity : selectedEntities) {
            if (entity == selected) continue;
            if (auto* other = ecs.tryGetComponent<core::Renderable>(entity)) {
                other->baseColor = renderable->baseColor;
                other->metallic = renderable->metallic;
                other->roughness = renderable->roughness;
                other->normalIntensity = renderable->normalIntensity;
                other->emissiveColor = renderable->emissiveColor;
                other->emissiveIntensity = renderable->emissiveIntensity;
                other->albedoTexture = renderable->albedoTexture;
                other->normalTexture = renderable->normalTexture;
                other->metallicTexture = renderable->metallicTexture;
                other->roughnessTexture = renderable->roughnessTexture;
                other->aoTexture = renderable->aoTexture;
            }
        }
    }
    ImGui::EndDisabled();
    if (!canApplyAll) {
        ImGui::TextDisabled("Select 2+ entities (ctrl/shift-click in Explorer) to copy this material to the rest.");
    }

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
