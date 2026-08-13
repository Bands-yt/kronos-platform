#include "migration/InstanceTreeBuilder.hpp"

namespace engine::migration {

std::vector<ImportedInstance> InstanceTreeBuilder::build(const XmlNode& root) {
    std::vector<ImportedInstance> result;
    for (const auto& child : root.children) {
        if (child.tag == "Item") {
            result.push_back(buildItem(child));
        }
        // Non-Item top-level children (e.g. <External>/<Meta> bookkeeping
        // nodes some .rbxlx exports include) are intentionally skipped --
        // they aren't Instances.
    }
    return result;
}

ImportedInstance InstanceTreeBuilder::buildItem(const XmlNode& itemNode) {
    ImportedInstance instance;
    if (const auto* cls = itemNode.attribute("class")) instance.className = *cls;
    if (const auto* ref = itemNode.attribute("referent")) instance.referent = *ref;

    for (const auto& child : itemNode.children) {
        if (child.tag == "Properties") {
            for (const auto& prop : child.children) {
                if (const auto* nameAttr = prop.attribute("name")) {
                    instance.properties[*nameAttr] = prop.text;
                }
            }
        } else if (child.tag == "Item") {
            instance.children.push_back(buildItem(child));
        }
    }

    auto nameIt = instance.properties.find("Name");
    instance.name = (nameIt != instance.properties.end() && !nameIt->second.empty())
                         ? nameIt->second
                         : instance.className; // Roblox's own default when an Instance has no explicit Name

    return instance;
}

} // namespace engine::migration
