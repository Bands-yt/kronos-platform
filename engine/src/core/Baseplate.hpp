#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"

namespace engine::core {

class MeshLibrary;
class TextureLibrary;
class Physics;

// Smooth Studio-style grid baseplate, 500x1x500, top surface at Y=0, no
// stud geometry -- the grid is a tiled texture, not mesh detail. `physics`
// mirrors SceneManager::loadScene's optional-Physics convention: null in
// Studio edit-mode (no live Jolt world there), non-null for a real
// runtime session. Either way the collider is authored via ColliderShape
// so PhysicsPreviewPlugin::play() picks it up on its own.
[[nodiscard]] EntityId spawnDefaultBaseplate(ECS& ecs, MeshLibrary& meshLibrary, TextureLibrary& textureLibrary,
                                              VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                              VkQueue queue, Physics* physics = nullptr);

} // namespace engine::core
