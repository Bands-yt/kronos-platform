#include "core/Guid.hpp"

#include <cstdio>
#include <mutex>
#include <random>

namespace engine::core {

std::string Guid::toString() const {
    char text[37];
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(hi >> 32),
                  static_cast<unsigned>((hi >> 16) & 0xFFFF), static_cast<unsigned>(hi & 0xFFFF),
                  static_cast<unsigned>(lo >> 48), static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFull));
    return text;
}

std::optional<Guid> Guid::parse(std::string_view text) {
    if (text.size() != 36) return std::nullopt;
    Guid guid;
    int digits = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return std::nullopt;
            continue;
        }
        int value = 0;
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
        else return std::nullopt;
        uint64_t& half = digits < 16 ? guid.hi : guid.lo;
        half = (half << 4) | static_cast<uint64_t>(value);
        ++digits;
    }
    if (!guid.valid()) return std::nullopt;
    return guid;
}

Guid Guid::generate() {
    static std::mutex mutex;
    static std::mt19937_64 engine{[] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }()};
    std::lock_guard lock(mutex);
    Guid guid;
    do {
        guid.hi = engine();
        guid.lo = engine();
    } while (!guid.valid());
    // RFC 4122 version 4 / variant bits, so tools reading the text form recognise it.
    guid.hi = (guid.hi & ~0xF000ull) | 0x4000ull;
    guid.lo = (guid.lo & ~(0xC000ull << 48)) | (0x8000ull << 48);
    return guid;
}

namespace guids {

Guid ensure(ECS& ecs, EntityId entity) {
    if (const Guid* existing = ecs.tryGetComponent<Guid>(entity); existing && existing->valid()) return *existing;
    const Guid guid = Guid::generate();
    ecs.raw().emplace_or_replace<Guid>(entity, guid);
    return guid;
}

Guid of(const ECS& ecs, EntityId entity) {
    const Guid* guid = ecs.raw().valid(entity) ? ecs.raw().try_get<Guid>(entity) : nullptr;
    return guid ? *guid : Guid{};
}

EntityId find(ECS& ecs, const Guid& guid) {
    if (!guid.valid()) return kNullEntity;
    for (auto [entity, id] : ecs.raw().view<Guid>().each()) {
        if (id == guid) return entity;
    }
    return kNullEntity;
}

} // namespace guids

} // namespace engine::core
