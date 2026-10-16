#include "core/RobloxCollisionGroups.hpp"

#include <array>

namespace engine::core::collisiongroups {
namespace {

struct Registry {
    std::array<std::string, kMaxGroups> names;
    std::array<uint32_t, kMaxGroups> rows;
    uint32_t version = 1;
    Registry() {
        names[0] = "Default";
        rows.fill(0xFFFFFFFFu);
    }
};

Registry& of(ECS& ecs) {
    auto* found = ecs.raw().ctx().find<Registry>();
    return found != nullptr ? *found : ecs.raw().ctx().emplace<Registry>();
}

} // namespace

int indexOf(ECS& ecs, const std::string& name) {
    const Registry& r = of(ecs);
    for (int i = 0; i < kMaxGroups; ++i) {
        if (!r.names[i].empty() && r.names[i] == name) return i;
    }
    return -1;
}

std::string nameOf(ECS& ecs, int index) {
    return index >= 0 && index < kMaxGroups ? of(ecs).names[index] : std::string();
}

bool registerGroup(ECS& ecs, const std::string& name, std::string& error) {
    Registry& r = of(ecs);
    if (name.empty()) {
        error = "Collision group name can't be empty";
        return false;
    }
    if (indexOf(ecs, name) >= 0) {
        error = "Collision group '" + name + "' is already registered";
        return false;
    }
    for (int i = 1; i < kMaxGroups; ++i) {
        if (!r.names[i].empty()) continue;
        r.names[i] = name;
        for (int j = 0; j < kMaxGroups; ++j) {
            r.rows[i] |= 1u << j;
            r.rows[j] |= 1u << i;
        }
        ++r.version;
        return true;
    }
    error = "Too many collision groups (the limit is 32)";
    return false;
}

bool unregisterGroup(ECS& ecs, const std::string& name, std::string& error) {
    Registry& r = of(ecs);
    const int index = indexOf(ecs, name);
    if (index < 0) {
        error = "Collision group '" + name + "' is not registered";
        return false;
    }
    if (index == 0) {
        error = "The Default collision group can't be removed";
        return false;
    }
    r.names[index].clear();
    r.rows[index] = 0xFFFFFFFFu;
    for (int j = 0; j < kMaxGroups; ++j) r.rows[j] |= 1u << index;
    ++r.version;
    return true;
}

bool renameGroup(ECS& ecs, const std::string& from, const std::string& to, std::string& error) {
    Registry& r = of(ecs);
    const int index = indexOf(ecs, from);
    if (index < 0) {
        error = "Collision group '" + from + "' is not registered";
        return false;
    }
    if (to.empty() || indexOf(ecs, to) >= 0) {
        error = to.empty() ? "Collision group name can't be empty" : "Collision group '" + to + "' is already registered";
        return false;
    }
    r.names[index] = to;
    ++r.version;
    return true;
}

bool setCollidable(ECS& ecs, const std::string& a, const std::string& b, bool collide, std::string& error) {
    Registry& r = of(ecs);
    const int i = indexOf(ecs, a);
    const int j = indexOf(ecs, b);
    if (i < 0 || j < 0) {
        error = "Collision group '" + (i < 0 ? a : b) + "' is not registered";
        return false;
    }
    if (collide) {
        r.rows[i] |= 1u << j;
        r.rows[j] |= 1u << i;
    } else {
        r.rows[i] &= ~(1u << j);
        r.rows[j] &= ~(1u << i);
    }
    ++r.version;
    return true;
}

bool collidable(ECS& ecs, int a, int b) {
    if (a < 0 || b < 0 || a >= kMaxGroups || b >= kMaxGroups) return true;
    return ((of(ecs).rows[a] >> b) & 1u) != 0;
}

void reset(ECS& ecs) { ecs.raw().ctx().erase<Registry>(); }

const uint32_t* rows(ECS& ecs) { return of(ecs).rows.data(); }
uint32_t version(ECS& ecs) { return of(ecs).version; }

int groupCount(ECS& ecs) {
    const Registry& r = of(ecs);
    int count = 0;
    for (const std::string& name : r.names) count += name.empty() ? 0 : 1;
    return count;
}

} // namespace engine::core::collisiongroups
