#include "migration/InstanceTreeBuilder.hpp"

#include <algorithm>

#include "core/OAuthPkce.hpp"

namespace engine::migration {

std::string decodeBase64(std::string text) {
    std::erase_if(text, [](char c) { return c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t'; });
    std::replace(text.begin(), text.end(), '+', '-');
    std::replace(text.begin(), text.end(), '/', '_');
    return core::base64UrlDecode(text);
}

std::vector<ImportedInstance> InstanceTreeBuilder::build(const XmlNode& root) {
    // SharedString properties hold a key into the file's <SharedStrings> table.
    SharedStrings shared;
    for (const auto& child : root.children) {
        if (child.tag != "SharedStrings") continue;
        for (const auto& entry : child.children) {
            const auto* key = entry.attribute("md5");
            if (entry.tag == "SharedString" && key != nullptr) shared[*key] = decodeBase64(entry.text);
        }
    }
    std::vector<ImportedInstance> result;
    for (const auto& child : root.children) {
        if (child.tag == "Item") result.push_back(buildItem(child, shared));
    }
    return result;
}

ImportedInstance InstanceTreeBuilder::buildItem(const XmlNode& itemNode, const SharedStrings& shared) {
    ImportedInstance instance;
    if (const auto* cls = itemNode.attribute("class")) instance.className = *cls;
    if (const auto* ref = itemNode.attribute("referent")) instance.referent = *ref;

    for (const auto& child : itemNode.children) {
        if (child.tag == "Properties") {
            for (const auto& prop : child.children) {
                const auto* nameAttr = prop.attribute("name");
                if (nameAttr == nullptr) continue;
                instance.properties[*nameAttr] = prop.text;
                if (prop.tag == "SharedString") {
                    std::string key = prop.text;
                    std::erase_if(key, [](char c) { return c == '\n' || c == '\r' || c == ' ' || c == '\t'; });
                    const auto found = shared.find(key);
                    instance.properties[*nameAttr] = found != shared.end() ? found->second : std::string();
                }
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
            instance.children.push_back(buildItem(child, shared));
        }
    }

    auto nameIt = instance.properties.find("Name");
    instance.name = (nameIt != instance.properties.end() && !nameIt->second.empty())
                         ? nameIt->second
                         : instance.className; // Roblox's own default when an Instance has no explicit Name

    return instance;
}

} // namespace engine::migration
