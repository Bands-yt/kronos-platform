#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "migration/RbxlxParser.hpp"

namespace engine::migration {

// The migration layer's own intermediate representation of an imported
// Instance -- deliberately *not* engine::core's real Instance/ECS type.
// docs/ARCHITECTURE.md §6's Instance-is-a-view-over-the-ECS translation
// layer doesn't exist yet (see Scripting.hpp's TODO), so this struct is
// where "preserve name/properties/hierarchy verbatim" (§7) actually lands
// today. Wiring ImportedInstance -> real ECS entities is a follow-on pass
// once that layer exists, not a redesign of this one.
struct ImportedInstance {
    std::string className;   // from Item's `class` attribute, e.g. "Script", "Part"
    std::string name;        // from the Name property if present, else className
    std::string referent;    // Roblox's internal cross-reference id, kept for asset/script re-linking
    std::unordered_map<std::string, std::string> properties; // raw string values -- typed decoding (Vector3/CFrame/etc, §6) is a TODO
    std::vector<ImportedInstance> children;
};

class InstanceTreeBuilder {
public:
    // Walks a parsed .rbxlx document's <roblox> root and reconstructs the
    // <Item> hierarchy into ImportedInstance nodes.
    [[nodiscard]] static std::vector<ImportedInstance> build(const XmlNode& root);

private:
    static ImportedInstance buildItem(const XmlNode& itemNode);
};

} // namespace engine::migration
