#pragma once

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Terrain.hpp"

namespace engine::housedemo {

// Kronos ("house-demo"): the real, live house-build deliverable --
// "a standard house with a working door, grass on either side and
// rolling hills... front door, 2 windows, a fireplace, and a kitchen
// (fireplace in the living room)." Real content, real code, not a
// hand-authored .scene file -- core::SceneFile can't serialize
// core::Terrain (no heightmap accessor/serialization exists anywhere in
// the engine, see that header's own comment) nor WorldProp/Interactable/
// Door, so this is built the same way main.cpp's own bring-up scene is:
// live C++ against a live ECS + Vulkan device.
//
// `terrain` must already be created (core::Terrain::create()) and have
// RollingHills applied -- this function flattens a real, honest
// footprint under the house first (Terrain::flatten()) so the floor
// sits level rather than clipping through the hills, then spawns
// everything at `worldOriginXZ` (house-local space from
// HouseLayout.hpp's computeHouseLayout(), offset by this and the real
// terrain height there).
void buildHouseDemoScene(core::ECS& ecs, core::MeshLibrary& meshLibrary, core::Terrain& terrain,
                          VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                          glm::vec2 worldOriginXZ);

} // namespace engine::housedemo
