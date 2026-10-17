#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace engine::core {

// "X needs Y" rules, so Studio can warn before a component is left without what it relies on.
struct ComponentRule {
    std::string component;
    std::string needs;
    std::string why;
};

inline const std::vector<ComponentRule>& componentRules() {
    static const std::vector<ComponentRule> rules = {
        {"RigidBody", "Collider", "A body with no collider falls through everything."},
        {"AudioSource", "Transform", "Sounds in the world need a position."},
        {"MeshRenderer", "Mesh", "There is nothing to draw without a mesh."},
        {"Light", "Transform", "A light needs a position."},
    };
    return rules;
}

// Warnings for one object's component names.
inline std::vector<std::string> componentWarnings(const std::set<std::string>& present) {
    std::vector<std::string> out;
    for (const ComponentRule& r : componentRules()) {
        if (present.count(r.component) && !present.count(r.needs)) {
            out.push_back(r.component + " needs " + r.needs + ". " + r.why);
        }
    }
    return out;
}

} // namespace engine::core
