#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/ECS.hpp"

namespace engine::core {

// A saved object's permanent id (random 128-bit). Survives renames, save/load and undo;
// copies get a new one. Also an ECS component.
struct Guid {
    uint64_t hi = 0;
    uint64_t lo = 0;

    [[nodiscard]] bool valid() const { return hi != 0 || lo != 0; }
    [[nodiscard]] std::string toString() const; // 8-4-4-4-12 lowercase hex
    [[nodiscard]] static std::optional<Guid> parse(std::string_view text);
    [[nodiscard]] static Guid generate();
    friend bool operator==(const Guid&, const Guid&) = default;
};

struct GuidHash {
    size_t operator()(const Guid& guid) const { return static_cast<size_t>(guid.hi ^ (guid.lo * 0x9E3779B97F4A7C15ull)); }
};

namespace guids {
// The entity's id, giving it a new one first if it has none.
Guid ensure(ECS& ecs, EntityId entity);
[[nodiscard]] Guid of(const ECS& ecs, EntityId entity);
[[nodiscard]] EntityId find(ECS& ecs, const Guid& guid);
} // namespace guids

} // namespace engine::core
