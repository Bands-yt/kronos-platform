// kronos_lookdev: renders fixed look-development scenes offscreen and writes
// PNGs, so shading/shadow/IBL changes can be compared frame-for-frame.
//
//   kronos_lookdev <outputDir> [frames]
//
// `frames` > 1 renders each view that many times before capturing (lets
// temporal passes converge; the capture is always the last frame).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/Camera.hpp"
#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Renderer.hpp"
#include "core/Texture.hpp"
#include "core/Window.hpp"
#include "trailer/CaptureRig.hpp"

using namespace engine;

namespace {

struct View {
    const char* name;
    glm::vec3 position;
    float yaw;
    float pitch;
    bool night;
};

core::SceneLighting dayLighting() {
    core::SceneLighting l;
    l.directionWS = glm::normalize(glm::vec3(-0.45f, -0.55f, -0.35f));
    l.intensity = 3.0f;
    return l;
}

core::SceneLighting nightLighting() {
    core::SceneLighting l;
    l.directionWS = glm::normalize(glm::vec3(0.3f, -0.8f, 0.2f));
    l.color = {0.55f, 0.65f, 1.0f};
    l.intensity = 0.08f;
    l.ambient = {0.015f, 0.02f, 0.035f};
    l.ambientGround = {0.01f, 0.01f, 0.012f};
    l.skyZenithColor = {0.01f, 0.015f, 0.04f};
    l.skyHorizonColor = {0.03f, 0.035f, 0.06f};
    return l;
}

core::EntityId spawn(core::ECS& ecs, uint32_t mesh, glm::vec3 pos, glm::vec3 scale, glm::vec3 color, float metallic,
                     float roughness) {
    core::EntityId e = ecs.createEntity();
    auto& t = *ecs.tryGetComponent<core::Transform>(e);
    t.position = pos;
    t.scale = scale;
    core::Renderable r;
    r.meshHandle = mesh;
    r.baseColor = glm::vec4(color, 1.0f);
    r.metallic = metallic;
    r.roughness = roughness;
    ecs.addComponent<core::Renderable>(e, r);
    return e;
}

} // namespace

int main(int argc, char** argv) {
    std::string outDir = argc > 1 ? argv[1] : "lookdev_out";
    int frames = argc > 2 ? std::max(1, std::atoi(argv[2])) : 1;
    std::filesystem::create_directories(outDir);

    core::Window window;
    core::Window::CreateInfo windowInfo;
    windowInfo.title = "Kronos Lookdev";
    windowInfo.width = 320;
    windowInfo.height = 180;
    if (!window.initialize(windowInfo)) return 1;

    core::Renderer renderer;
    core::Renderer::CreateInfo rendererInfo;
    rendererInfo.window = &window;
    rendererInfo.enableValidation = std::getenv("KRONOS_LOOKDEV_VALIDATION") != nullptr;
    if (!renderer.initialize(rendererInfo)) return 1;
    renderer.setAutoExposureEnabled(false);

    VmaAllocator alloc = renderer.allocator();
    VkDevice device = renderer.device();
    VkCommandPool pool = renderer.commandPool();
    VkQueue queue = renderer.graphicsQueue();

    core::MeshLibrary meshes;
    core::TextureLibrary textures;
    uint32_t sphere = meshes.registerMesh(core::Mesh::createCapsule(alloc, device, pool, queue, 0.5f, 0.0f, 48, 24));
    uint32_t box = meshes.registerMesh(core::Mesh::createBox(alloc, device, pool, queue, glm::vec3(0.5f)));
    uint32_t plane = meshes.registerMesh(core::Mesh::createPlane(alloc, device, pool, queue, 60.0f, 60.0f));
    uint32_t torus = meshes.registerMesh(core::Mesh::createTorus(alloc, device, pool, queue, 0.6f, 0.2f, 48, 24));

    core::ECS ecs;
    spawn(ecs, plane, {0, 0, 0}, glm::vec3(1), {0.5f, 0.5f, 0.5f}, 0.0f, 0.7f);

    constexpr int kColumns = 7;
    for (int i = 0; i < kColumns; ++i) {
        float rough = static_cast<float>(i) / (kColumns - 1);
        float x = (static_cast<float>(i) - (kColumns - 1) * 0.5f) * 1.2f;
        spawn(ecs, sphere, {x, 0.5f, 0.0f}, glm::vec3(1), {0.8f, 0.1f, 0.08f}, 0.0f, rough);
        spawn(ecs, sphere, {x, 0.5f, -1.4f}, glm::vec3(1), {1.0f, 0.78f, 0.34f}, 1.0f, rough);
        spawn(ecs, sphere, {x, 0.5f, -2.8f}, glm::vec3(1), {0.95f, 0.95f, 0.95f}, 1.0f, rough);
    }

    spawn(ecs, torus, {-5.5f, 0.8f, 1.5f}, glm::vec3(1), {0.2f, 0.4f, 0.9f}, 0.0f, 0.35f);
    spawn(ecs, box, {5.5f, 1.0f, 1.0f}, glm::vec3(1.0f, 2.0f, 1.0f), {0.7f, 0.7f, 0.65f}, 0.0f, 0.8f);
    // Distant pillars and a thin fence exercise cascade transitions and fine-caster stability.
    for (int i = 0; i < 12; ++i) {
        float z = -6.0f - static_cast<float>(i) * 6.0f;
        spawn(ecs, box, {-4.0f, 2.0f, z}, glm::vec3(0.6f, 4.0f, 0.6f), {0.6f, 0.58f, 0.55f}, 0.0f, 0.9f);
        spawn(ecs, box, {4.0f, 2.0f, z}, glm::vec3(0.6f, 4.0f, 0.6f), {0.6f, 0.58f, 0.55f}, 0.0f, 0.9f);
    }
    for (int i = 0; i < 24; ++i) {
        spawn(ecs, box, {-7.0f, 0.6f, 3.0f - static_cast<float>(i) * 0.25f}, glm::vec3(0.03f, 1.2f, 0.03f),
              {0.3f, 0.3f, 0.3f}, 0.0f, 0.6f);
    }

    std::vector<core::EntityId> nightLights;
    const glm::vec3 lightColors[] = {{1, 0.3f, 0.2f}, {0.3f, 1, 0.4f}, {0.3f, 0.5f, 1}, {1, 0.8f, 0.3f}};
    for (int i = 0; i < 48; ++i) {
        core::EntityId e = ecs.createEntity();
        auto& t = *ecs.tryGetComponent<core::Transform>(e);
        t.position = {(i % 8 - 3.5f) * 2.4f, 0.35f + (i % 3) * 0.3f, 2.0f - static_cast<float>(i / 8) * 5.0f};
        core::Light light;
        light.color = lightColors[i % 4];
        light.intensity = 3.0f;
        light.radius = 4.0f;
        light.enabled = false;
        ecs.addComponent<core::Light>(e, light);
        nightLights.push_back(e);
    }

    trailer::CaptureRig rig;
    if (!rig.initialize(renderer, VkExtent2D{1280, 720})) return 1;

    const View views[] = {
        {"materials", {0.0f, 1.6f, 5.2f}, -90.0f, -14.0f, false},
        {"wide", {9.0f, 6.0f, 9.0f}, -130.0f, -24.0f, false},
        {"grazing", {-1.0f, 1.2f, 6.0f}, -95.0f, -4.0f, false},
        {"night", {0.0f, 4.0f, 8.0f}, -90.0f, -22.0f, true},
    };

    for (const View& view : views) {
        renderer.setLighting(view.night ? nightLighting() : dayLighting());
        for (core::EntityId e : nightLights) ecs.tryGetComponent<core::Light>(e)->enabled = view.night;

        core::Camera camera;
        camera.position = view.position;
        camera.yawDegrees = view.yaw;
        camera.pitchDegrees = view.pitch;

        for (int f = 0; f < frames; ++f) {
            std::string dir = outDir + "/" + view.name;
            if (f + 1 < frames) dir = outDir + "/.warmup";
            std::filesystem::create_directories(dir);
            if (!rig.captureFrame(renderer, ecs, meshes, textures, camera, dir, 0, true)) {
                std::fprintf(stderr, "lookdev: capture failed for %s\n", view.name);
                return 1;
            }
        }
        std::printf("lookdev: wrote %s\n", view.name);
    }
    std::filesystem::remove_all(outDir + "/.warmup");

    vkDeviceWaitIdle(device);
    rig.shutdown(renderer);
    meshes.destroyAll(alloc);
    textures.destroyAll(alloc, device);
    renderer.shutdown();
    return 0;
}
