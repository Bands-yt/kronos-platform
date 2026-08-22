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
                const auto* nameAttr = prop.attribute("name");
                if (nameAttr == nullptr) continue;
                instance.properties[*nameAttr] = prop.text;
                // Roblox's typed properties are nested elements, not text:
                //   <Vector3 name="size"><X>4</X><Y>1</Y><Z>2</Z></Vector3>
                //   <CoordinateFrame name="CFrame"><X>..</X>..<R22>..</R22></CoordinateFrame>
                // Reading only prop.text drops every position, size,
                // rotation and colour in the document -- i.e. everything
                // needed to actually place an imported part. Child values
                // are flattened as "<prop>.<field>" so ImportedInstance
                // stays a plain string map (see its declaration) and
                // PropertyDecoder does the typing.
                for (const auto& field : prop.children) {
                    instance.properties[*nameAttr + "." + field.tag] = field.text;
                }
                // The XML element's own tag carries the type, which the
                // decoder needs to tell Color3 (0..1 floats) from
                // Color3uint8 (a packed integer).
                instance.properties["@type." + *nameAttr] = prop.tag;
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
