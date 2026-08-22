#pragma once

#include <string>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::migration {

using PropertyMap = std::unordered_map<std::string, std::string>;

// Typed reads over ImportedInstance::properties.
//
// The map is flat strings by design (see ImportedInstance), and Roblox's
// typed properties arrive as nested XML that InstanceTreeBuilder flattens
// to "CFrame.X", "size.Y" and so on. This is the other half of that: the
// decoding rules, kept separate from both the parser and the hydrator so
// they can be tested against real .rbxlx property spellings without an
// ECS or a device.
//
// Every accessor takes a fallback and returns it when the property is
// missing OR unparseable. That is deliberate: an imported document is
// untrusted input, and a malformed <X>NaN</X> should place the part at
// the fallback rather than propagate a NaN into a transform, where it
// silently corrupts the whole scene graph downstream.

[[nodiscard]] bool hasProperty(const PropertyMap& properties, const std::string& name);

// The XML element tag the property was written as ("Vector3",
// "Color3uint8", "float", ...). Empty when unknown.
[[nodiscard]] std::string propertyType(const PropertyMap& properties, const std::string& name);

[[nodiscard]] float decodeFloat(const PropertyMap& properties, const std::string& name, float fallback = 0.0f);
[[nodiscard]] int decodeInt(const PropertyMap& properties, const std::string& name, int fallback = 0);
[[nodiscard]] bool decodeBool(const PropertyMap& properties, const std::string& name, bool fallback = false);
[[nodiscard]] std::string decodeString(const PropertyMap& properties, const std::string& name,
                                        const std::string& fallback = {});

// <Vector3 name="size"><X/><Y/><Z/></Vector3>
[[nodiscard]] glm::vec3 decodeVector3(const PropertyMap& properties, const std::string& name,
                                       glm::vec3 fallback = glm::vec3(0.0f));

// Handles both colour spellings Roblox emits:
//   <Color3 name="Color"><R>1</R><G>0</G><B>0</B></Color3>   -- 0..1 floats
//   <Color3uint8 name="Color3uint8">4294901760</Color3uint8>  -- packed ARGB
// Returns linear-ish 0..1 RGB either way.
[[nodiscard]] glm::vec3 decodeColor3(const PropertyMap& properties, const std::string& name,
                                      glm::vec3 fallback = glm::vec3(1.0f));

// Position component of <CoordinateFrame name="CFrame">.
[[nodiscard]] glm::vec3 decodeCFramePosition(const PropertyMap& properties, const std::string& name,
                                              glm::vec3 fallback = glm::vec3(0.0f));

// Rotation of a CFrame, from its R00..R22 basis.
//
// Roblox writes the basis as ROW vectors (R01 is row 0, column 1), which
// is the transpose of glm's column-major mat3 constructor order. Getting
// that backwards mirrors every rotation in the place and is invisible on
// anything axis-aligned -- which is most of a test scene -- so it is
// stated here rather than left to be rediscovered.
//
// Falls back to identity when the basis is absent or not a rotation
// (non-orthonormal, zero-length, or NaN).
[[nodiscard]] glm::quat decodeCFrameRotation(const PropertyMap& properties, const std::string& name,
                                              glm::quat fallback = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

} // namespace engine::migration
