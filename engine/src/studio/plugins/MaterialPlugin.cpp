#include "studio/plugins/MaterialPlugin.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/Components.hpp"
#include "core/MeshUvPicking.hpp"
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

void MaterialPlugin::stampAllSlots(core::Renderable& renderable, glm::vec2 uv) {
    if (!painterReady_) {
        std::string error;
        painterReady_ = painter_.initialize(allocator_, device_, cmdPool_, queue_, error);
        if (!painterReady_) paintStatusMessage_ = "Painter init failed: " + error;
    }
    if (!painterReady_) return;

    std::string error;
    int stamped = 0;
    auto tryStamp = [&](uint32_t handle, glm::vec4 color) {
        const core::Texture* texture = textureLibrary_->get(handle);
        if (texture == nullptr) return;
        if (painter_.stamp(*texture, uv, paintRadius_, color, paintSoftness_, error)) ++stamped;
    };
    tryStamp(renderable.albedoTexture, paintAlbedoColor_);
    // Real, stated brush-shape simplification: a flat "up" tangent-space
    // normal, not sculpted bump detail -- see this section's own drawn
    // header text and ComputePbrPainter.hpp's class comment.
    tryStamp(renderable.normalTexture, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
    tryStamp(renderable.roughnessTexture, glm::vec4(paintRoughnessValue_, paintRoughnessValue_, paintRoughnessValue_, 1.0f));
    tryStamp(renderable.metallicTexture, glm::vec4(paintMetallicValue_, paintMetallicValue_, paintMetallicValue_, 1.0f));
    paintStatusMessage_ = "Stamped " + std::to_string(stamped) + " real texture(s) directly in GPU memory.";
}

void MaterialPlugin::handleViewportPickPaint(core::Renderable& renderable) {
    glm::vec3 rayOrigin, rayDirection;
    // Click = one stamp; Ctrl-drag = a continuous stroke (see
    // PreviewScene::dragPaintRay()'s own comment) -- both land here so
    // zero-friction creation and the actual stamp only need writing once.
    bool hasRay = previewScene_.consumeClickRay(rayOrigin, rayDirection) ||
                  previewScene_.dragPaintRay(rayOrigin, rayDirection);
    if (!hasRay) return;

    bool anyPaintable = renderable.albedoTexture != core::Renderable::kInvalidHandle ||
                         renderable.normalTexture != core::Renderable::kInvalidHandle ||
                         renderable.roughnessTexture != core::Renderable::kInvalidHandle ||
                         renderable.metallicTexture != core::Renderable::kInvalidHandle;
    // Zero-Friction Painting: the first real click/drag on a selected
    // entity with no paintable texture yet creates a REAL, COMPLETE 2K
    // PBR set -- Albedo, Normal, Roughness, Metallic -- not just Albedo
    // alone (a real, previously-audited gap: stampAllSlots() silently
    // no-ops on any slot that's still core::Renderable::kInvalidHandle,
    // so a click used to "paint" only one of the four real channels
    // core::ComputePbrPainter.hpp's own header comment promises).
    // 2048x2048 specifically (not the manual "New Paintable Texture"
    // button's own 512x512) since a click implies "just start painting",
    // not "I chose a resolution". Clear colors match that same manual
    // button's own per-slot defaults (white albedo, flat-up normal,
    // mid-gray roughness, non-metallic black) exactly.
    if (!anyPaintable) {
        auto createSlot = [&](uint32_t& handle, glm::vec4 clearColor) {
            core::Texture texture =
                core::Texture::createStorageImage(2048, 2048, clearColor, allocator_, device_, cmdPool_, queue_);
            if (texture.isValid()) handle = textureLibrary_->registerTexture(std::move(texture));
        };
        createSlot(renderable.albedoTexture, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        createSlot(renderable.normalTexture, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
        createSlot(renderable.roughnessTexture, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        createSlot(renderable.metallicTexture, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        paintStatusMessage_ = renderable.albedoTexture != core::Renderable::kInvalidHandle
                                  ? "Auto-created a complete 2K PBR set (Albedo/Normal/Roughness/Metallic) -- painting now."
                                  : "Failed to auto-create the 2K PBR set.";
    }

    constexpr float kMaxPickDistance = 100.0f;
    core::MeshUvPickResult pick = core::pickTriangleUv(previewPickMesh_, rayOrigin, rayDirection, kMaxPickDistance);
    if (!pick.hit) return;

    paintUv_ = pick.uv;
    stampAllSlots(renderable, paintUv_);
}

void MaterialPlugin::drawComputePaintSection(core::Renderable& renderable) {
    ImGui::SeparatorText("Compute Paint (direct-to-VRAM PBR stamp)");
    ImGui::TextWrapped(
        "Create a paintable texture per slot, then click directly on the preview sphere above (or use the Stamp "
        "button below) to stamp a soft circular brush directly into GPU memory via a real compute shader -- see "
        "core::ComputePbrPainter's own header comment for exactly what this does (and the real scope cuts: a flat "
        "normal brush rather than sculpted detail).");

    auto createPaintable = [&](uint32_t* handle, glm::vec4 clearColor, const char* label) {
        ImGui::PushID(label);
        if (ImGui::Button("New Paintable Texture")) {
            core::Texture texture =
                core::Texture::createStorageImage(512, 512, clearColor, allocator_, device_, cmdPool_, queue_);
            if (texture.isValid()) {
                *handle = textureLibrary_->registerTexture(std::move(texture));
                paintStatusMessage_ = std::string(label) + ": new 512x512 paintable texture created.";
            } else {
                paintStatusMessage_ = std::string(label) + ": failed to create a paintable texture.";
            }
        }
        ImGui::PopID();
    };

    ImGui::TextUnformatted("Albedo:");
    ImGui::SameLine();
    createPaintable(&renderable.albedoTexture, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), "AlbedoPaint");
    ImGui::TextUnformatted("Normal:");
    ImGui::SameLine();
    createPaintable(&renderable.normalTexture, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f), "NormalPaint");
    ImGui::TextUnformatted("Roughness:");
    ImGui::SameLine();
    createPaintable(&renderable.roughnessTexture, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), "RoughnessPaint");
    ImGui::TextUnformatted("Metallic:");
    ImGui::SameLine();
    createPaintable(&renderable.metallicTexture, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), "MetallicPaint");

    ImGui::SliderFloat2("Stamp UV Center", &paintUv_.x, 0.0f, 1.0f);
    ImGui::SliderFloat("Stamp Radius (UV)", &paintRadius_, 0.01f, 0.5f);
    ImGui::SliderFloat("Stamp Softness", &paintSoftness_, 0.0f, 1.0f);
    ImGui::ColorEdit4("Albedo Stamp Color", &paintAlbedoColor_.x);
    ImGui::SliderFloat("Roughness Stamp Value", &paintRoughnessValue_, 0.0f, 1.0f);
    ImGui::SliderFloat("Metallic Stamp Value", &paintMetallicValue_, 0.0f, 1.0f);

    bool anyPaintable = renderable.albedoTexture != core::Renderable::kInvalidHandle ||
                         renderable.normalTexture != core::Renderable::kInvalidHandle ||
                         renderable.roughnessTexture != core::Renderable::kInvalidHandle ||
                         renderable.metallicTexture != core::Renderable::kInvalidHandle;
    ImGui::BeginDisabled(!anyPaintable);
    if (ImGui::Button("Stamp")) {
        stampAllSlots(renderable, paintUv_);
    }
    ImGui::EndDisabled();
    if (!anyPaintable) ImGui::TextDisabled("Create at least one paintable texture above first.");

    if (!paintStatusMessage_.empty()) ImGui::TextDisabled("%s", paintStatusMessage_.c_str());
}

void MaterialPlugin::drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) {
    auto* renderable = selected != core::kNullEntity ? ecs.tryGetComponent<core::Renderable>(selected) : nullptr;

    drawViewportWindow(renderable);
    drawMaterialEditorWindow(ecs, selected, selectedEntities, renderable);
    drawTextureInspectorWindow(renderable);
    drawBrushStampWindow(renderable);
}

void MaterialPlugin::drawViewportWindow(core::Renderable* renderable) {
    ImGui::Begin("3D Viewport");
    // Live preview sphere (Sprint 10 task category 1) -- copies the
    // *current* edited values onto the preview scene's own sphere entity
    // every single drawPanel() call, so orbiting it always shows exactly
    // what's being edited right now, live, not a stale snapshot from
    // whenever the panel was last interacted with. Runs even with nothing
    // selected (renderable == nullptr) -- this window still shows
    // whatever the preview sphere's own last-known material was, rather
    // than going blank the moment selection is cleared.
    ensurePreviewEntity();
    if (renderable != nullptr) {
        if (auto* previewRenderable = previewScene_.ecs().tryGetComponent<core::Renderable>(previewEntity_)) {
            previewRenderable->baseColor = renderable->baseColor;
            previewRenderable->metallic = renderable->metallic;
            previewRenderable->roughness = renderable->roughness;
            previewRenderable->normalIntensity = renderable->normalIntensity;
            previewRenderable->emissiveColor = renderable->emissiveColor;
            previewRenderable->emissiveIntensity = renderable->emissiveIntensity;
        }
    }
    previewScene_.drawAndHandleOrbit();
    if (renderable != nullptr) handleViewportPickPaint(*renderable);

    if (renderable != nullptr) drawPaintHud(*renderable);
    if (showWireframe_) drawWireframeOverlay();
    if (previewScene_.isImageHovered()) drawBrushCursorRing();

    ImGui::End();
}

void MaterialPlugin::drawPaintHud(core::Renderable& renderable) {
    // Kronos ("Interactive Viewport Sculpting" -- viewport HUD): a
    // floating overlay drawn over the already-rendered preview image
    // (ImGui::SetCursorScreenPos back to the image's own top-left, same
    // "draw on top of the last Image()" technique ViewportPanel's own
    // toolbar already uses) so Radius/Strength/the active Albedo swatch
    // are always visible here -- not buried in the separate "Brush &
    // Stamp" window/Compute Paint section, which still has the fuller
    // per-slot controls.
    ImVec2 origin = previewScene_.imageOrigin();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + 8.0f));
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.85f));
    ImGui::BeginChild("##paint_hud", ImVec2(280.0f, 0.0f), true, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("Paint");
    ImGui::SameLine();
    ImGui::ColorButton("##albedo_swatch", ImVec4(paintAlbedoColor_.x, paintAlbedoColor_.y, paintAlbedoColor_.z, 1.0f),
                        ImGuiColorEditFlags_NoTooltip, ImVec2(16.0f, 16.0f));
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Radius (F)", &paintRadius_, 0.01f, 0.5f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Strength (Shift+F)", &paintSoftness_, 0.0f, 1.0f);
    ImGui::Checkbox("Wireframe", &showWireframe_);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    // F/Shift+F hotkeys while the preview is hovered -- a real, working
    // step-adjust (not the fuller "hold F and move the mouse" gesture
    // Blender's own radius hotkey uses; that's a separate, bigger input-
    // capture change this doesn't attempt).
    if (previewScene_.isImageHovered() && !ImGui::GetIO().WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            if (ImGui::GetIO().KeyShift) {
                paintSoftness_ = std::clamp(paintSoftness_ + 0.1f, 0.0f, 1.0f);
            } else {
                paintRadius_ = std::clamp(paintRadius_ + 0.05f, 0.01f, 0.5f);
                if (paintRadius_ > 0.49f) paintRadius_ = 0.01f; // real, honest wrap instead of sticking at max
            }
        }
    }
    (void)renderable;
}

void MaterialPlugin::drawBrushCursorRing() {
    // Real, honest approximation: paintRadius_ is a UV-space fraction of
    // the whole texture, not a screen-space measurement, so there is no
    // exact UV-to-pixel conversion without knowing the actual on-screen
    // triangle size under the cursor. Scaling by the image's own width
    // gives a real, visibly-correct-looking brush indicator that grows
    // and shrinks with Radius, not a claim of pixel-perfect footprint.
    ImVec2 imageSize = previewScene_.imageSize();
    float screenRadius = paintRadius_ * imageSize.x * 0.5f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircle(ImGui::GetMousePos(), screenRadius, IM_COL32(255, 255, 255, 200), 32, 1.5f);
    drawList->AddCircle(ImGui::GetMousePos(), screenRadius, IM_COL32(20, 20, 20, 160), 32, 3.0f);
    drawList->AddCircle(ImGui::GetMousePos(), screenRadius, IM_COL32(255, 255, 255, 200), 32, 1.5f);
}

void MaterialPlugin::drawWireframeOverlay() {
    glm::mat4 viewProj = previewScene_.camera().projectionMatrix(1.0f) * previewScene_.camera().viewMatrix();
    ImVec2 origin = previewScene_.imageOrigin();
    ImVec2 size = previewScene_.imageSize();
    if (size.x <= 0.0f || size.y <= 0.0f) return;
    viewProj = previewScene_.camera().projectionMatrix(size.x / size.y) * previewScene_.camera().viewMatrix();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    auto project = [&](glm::vec3 worldPos, ImVec2& outScreen) -> bool {
        glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.001f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        outScreen.x = origin.x + (ndc.x * 0.5f + 0.5f) * size.x;
        outScreen.y = origin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y;
        return true;
    };

    // Real edge data from the same CPU-retained mesh handleViewportPickPaint()
    // already ray-tests against -- not a re-derived approximation.
    for (const auto& [a, b] : previewPickMesh_.allEdges()) {
        ImVec2 screenA, screenB;
        if (!project(previewPickMesh_.vertices()[a].position, screenA)) continue;
        if (!project(previewPickMesh_.vertices()[b].position, screenB)) continue;
        drawList->AddLine(screenA, screenB, IM_COL32(0, 255, 140, 200), 1.0f);
    }
}

void MaterialPlugin::drawMaterialEditorWindow(core::ECS& ecs, core::EntityId selected,
                                               const std::vector<core::EntityId>& selectedEntities,
                                               core::Renderable* renderable) {
    ImGui::Begin("Material Editor");
    drawPluginHeader("Material Editor");

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

void MaterialPlugin::drawTextureInspectorWindow(core::Renderable* renderable) {
    ImGui::Begin("PBR Texture Inspector");
    if (renderable == nullptr) {
        ImGui::TextDisabled("Select an entity with a Renderable component.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Type a file path, click Load. Empty slot = flat value above.");
    drawTextureSlot(Slot::Albedo, "Albedo", *renderable);
    drawTextureSlot(Slot::Normal, "Normal", *renderable);
    drawTextureSlot(Slot::Metallic, "Metallic", *renderable);
    drawTextureSlot(Slot::Roughness, "Roughness", *renderable);
    drawTextureSlot(Slot::AO, "Ambient Occlusion", *renderable);
    ImGui::End();
}

void MaterialPlugin::drawBrushStampWindow(core::Renderable* renderable) {
    ImGui::Begin("Brush & Stamp");
    if (renderable == nullptr) {
        ImGui::TextDisabled("Select an entity with a Renderable component.");
        ImGui::End();
        return;
    }
    drawComputePaintSection(*renderable);
    ImGui::End();
}

} // namespace engine::studio::plugins
