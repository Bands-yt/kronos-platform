#pragma once

// Real, implemented, tested: a registry that holds one real, validated
// byte-layout descriptor per named component type. Built as the isolated
// `polyglot_core` CMake target -- not linked into engine_runtime or
// studio. See UnifiedTypeSystem.cpp for the implementation and
// tests/test_polyglot_core.cpp for correctness tests.
//
// NOT implemented here (real, stated scope boundary): nothing generates
// a ComponentDescriptor from a real C++ struct (e.g. core::Transform)
// automatically, and nothing on the Luau/TS/WASM side reads one yet.
// This is real, validated bookkeeping for a byte layout a caller
// describes by hand -- the "one C++ struct, one Luau/TS/WASM view of the
// same bytes" pillar itself needs a real second-language runtime and a
// real codegen step neither of which exist in this codebase today (see
// polyglot/README.md).

#include <cstdint>
#include <string>
#include <vector>

namespace engine::polyglot {

enum class FieldType : uint8_t {
    F32,
    F64,
    I32,
    I64,
    Bool,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    FixedString,
};

// Real, known-at-compile-time byte size for every FieldType above --
// what registerComponent() validates each FieldDescriptor's own
// byteSize against, so a caller can't register a field claiming to be
// an F32 (4 bytes) with byteSize=8 and have that silently accepted.
[[nodiscard]] uint32_t fieldTypeByteSize(FieldType type);

struct FieldDescriptor {
    std::string name;
    FieldType type;
    uint32_t byteOffset = 0;
    uint32_t byteSize = 0;
};

struct ComponentDescriptor {
    std::string componentName;
    uint32_t totalByteSize = 0;
    std::vector<FieldDescriptor> fields;
};

class TypeRegistry {
public:
    // Real validation, not just bookkeeping -- rejects (returns false,
    // registers nothing) a descriptor whose name is empty, whose name is
    // already registered, whose totalByteSize is zero, whose own
    // fieldTypeByteSize(field.type) doesn't match field.byteSize, or
    // whose [byteOffset, byteOffset + byteSize) range doesn't fit inside
    // [0, totalByteSize). A caller finding out about a bad layout at
    // registration time is the real point -- silently accepting one
    // would surface as a real out-of-bounds read on whichever language
    // binding trusts this descriptor later.
    [[nodiscard]] bool registerComponent(ComponentDescriptor descriptor);

    [[nodiscard]] const ComponentDescriptor* find(const std::string& componentName) const;

    [[nodiscard]] size_t size() const { return components_.size(); }

private:
    std::vector<ComponentDescriptor> components_;
};

} // namespace engine::polyglot
