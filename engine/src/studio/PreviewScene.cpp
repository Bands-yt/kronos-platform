#include "studio/PreviewScene.hpp"

#include <algorithm>

#include <imgui.h>

#include "core/Renderer.hpp"

namespace engine::studio {

namespace {
// A flat, bright, neutral "studio lightbox" setup -- deliberately not
// the main scene's atmospheric directional light (docs task item 4:
// "Add material override for preview lighting"). Ambient is raised well
// above the main scene's default on both the sky and ground terms so an
// item reads clearly from every angle while the orbit camera is turning
// it, the way a real product-photography lightbox avoids hard shadows
// hiding half the subject.
const core::SceneLighting& previewLighting() {
    static const core::SceneLighting kPreviewLighting{
        glm::vec3(-0.35f, -0.85f, -0.4f), glm::vec3(1.0f, 1.0f, 1.0f), 3.4f, glm::vec3(0.35f, 0.35f, 0.38f),
        glm::vec3(0.25f, 0.24f, 0.22f),
    };
    return kPreviewLighting;
}
} // namespace

void PreviewScene::render(VkCommandBuffer cmd, core::Renderer& renderer, core::MeshLibrary& meshLibrary,
                           core::TextureLibrary& textureLibrary, core::RiggedMeshLibrary* riggedMeshLibrary,
                           const core::SceneLighting* lightingOverride) {
    if (desiredExtent_.width == 0 || desiredExtent_.height == 0) return;

    if (auxiliaryScene_ == core::Renderer::kInvalidAuxiliaryScene) {
        auxiliaryScene_ = renderer.createAuxiliaryScene();
        if (auxiliaryScene_ == core::Renderer::kInvalidAuxiliaryScene) return; // logged by createAuxiliaryScene() itself
    }

    target_.ensureSize(renderer.allocator(), renderer.device(), renderer.swapchainFormat(), renderer.depthFormat(),
                        desiredExtent_);
    if (!target_.isValid()) return;

    // Renderer's lighting is one shared field read once per
    // drawSceneInto() call, not passed as a parameter -- swapping it
    // around just this call (and restoring immediately after) is how
    // one Renderer instance can render the main Viewport under its own
    // scene lighting and this preview under studio lighting within the
    // same pre-pass callback, without touching drawSceneInto()'s
    // signature or affecting any other caller.
    core::SceneLighting previousLighting = renderer.lighting();
    renderer.setLighting(lightingOverride != nullptr ? *lightingOverride : previewLighting());

    // The AuxiliarySceneHandle overload -- NOT the plain one -- so this
    // scene's UBO/shadow-map/HDR-bloom targets are its own, independent
    // of the main Viewport's and any other open PreviewScene's. See
    // AuxiliarySceneHandle's doc comment for the real device-lost crash
    // calling the plain overload from here used to cause.
    renderer.drawSceneInto(auxiliaryScene_, cmd, target_.colorImage(), target_.colorView(), target_.depthImage(),
                            target_.depthView(), target_.extent(), camera_, ecs_, meshLibrary, particleSystem_,
                            textureLibrary, riggedMeshLibrary);
    renderer.transitionImage(cmd, target_.colorImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    renderer.setLighting(previousLighting);
}

void PreviewScene::drawAndHandleOrbit() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    desiredExtent_ = {static_cast<uint32_t>(std::max(1.0f, avail.x)), static_cast<uint32_t>(std::max(1.0f, avail.y))};

    if (target_.isValid()) {
        ImGui::Image(target_.imguiTextureId(),
                     ImVec2(static_cast<float>(target_.extent().width), static_cast<float>(target_.extent().height)));
    } else {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(18, 18, 24, 255));
        ImGui::Dummy(avail);
    }

    bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        orbitYawDegrees_ += delta.x * 0.4f;
        orbitPitchDegrees_ = std::clamp(orbitPitchDegrees_ - delta.y * 0.4f, -85.0f, 85.0f);
    }
    if (hovered) {
        orbitDistance_ = std::clamp(orbitDistance_ - ImGui::GetIO().MouseWheel * 0.4f, kMinOrbitDistance, kMaxOrbitDistance);
    }

    camera_.yawDegrees = orbitYawDegrees_;
    camera_.pitchDegrees = orbitPitchDegrees_;
    camera_.position = focusPoint_ - camera_.forward() * orbitDistance_;
}

void PreviewScene::reset() {
    ecs_.raw().clear();
    focusPoint_ = glm::vec3(0.0f, 1.0f, 0.0f);
    orbitYawDegrees_ = -60.0f;
    orbitPitchDegrees_ = -12.0f;
    orbitDistance_ = 3.0f;
}

void PreviewScene::destroy(core::Renderer& renderer, VmaAllocator allocator, VkDevice device) {
    target_.destroy(allocator, device);
    if (auxiliaryScene_ != core::Renderer::kInvalidAuxiliaryScene) {
        renderer.destroyAuxiliaryScene(auxiliaryScene_);
        auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;
    }
}

} // namespace engine::studio
