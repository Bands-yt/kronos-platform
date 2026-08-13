#include "studio/ThumbnailCameraRig.hpp"

#include <cstdio>

#include "publishing/ThumbnailCapture.hpp"

namespace engine::studio {

void ThumbnailCameraRig::render(VkCommandBuffer cmd, core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                 core::TextureLibrary& textureLibrary) {
    if (desiredExtent.width == 0 || desiredExtent.height == 0) return;

    if (auxiliaryScene_ == core::Renderer::kInvalidAuxiliaryScene) {
        auxiliaryScene_ = renderer.createAuxiliaryScene();
        if (auxiliaryScene_ == core::Renderer::kInvalidAuxiliaryScene) return; // logged by createAuxiliaryScene() itself
    }

    target_.ensureSize(renderer.allocator(), renderer.device(), renderer.swapchainFormat(), renderer.depthFormat(),
                        desiredExtent);
    if (!target_.isValid()) return;

    // Renders the real, live ECS (the actual world being published),
    // under the scene's own real lighting -- unlike PreviewScene, this
    // is deliberately NOT swapped to a flat studio-lightbox setup, since
    // a thumbnail should show what the published world actually looks
    // like, not a product-photography rendition of it.
    renderer.drawSceneInto(auxiliaryScene_, cmd, target_.colorImage(), target_.colorView(), target_.depthImage(),
                            target_.depthView(), target_.extent(), camera, ecs, meshLibrary, particleSystem_, textureLibrary);
    renderer.transitionImage(cmd, target_.colorImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

bool ThumbnailCameraRig::captureToFile(core::Renderer& renderer, const std::string& outputPath) const {
    if (!target_.isValid()) {
        std::fprintf(stderr, "ThumbnailCameraRig::captureToFile: no rendered frame yet -- call render() first.\n");
        return false;
    }
    return publishing::captureThumbnailToFile(renderer, target_.colorImage(), renderer.swapchainFormat(), target_.extent(),
                                                outputPath);
}

void ThumbnailCameraRig::destroy(core::Renderer& renderer, VmaAllocator allocator, VkDevice device) {
    target_.destroy(allocator, device);
    if (auxiliaryScene_ != core::Renderer::kInvalidAuxiliaryScene) {
        renderer.destroyAuxiliaryScene(auxiliaryScene_);
        auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;
    }
}

} // namespace engine::studio
