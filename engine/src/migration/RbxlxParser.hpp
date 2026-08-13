#pragma once

#include <optional>
#include <string>
#include <vector>

namespace engine::migration {

struct XmlAttribute {
    std::string name;
    std::string value;
};

// A minimal generic XML DOM node -- deliberately not a general-purpose XML
// library (no namespaces, no CDATA, no DTD, minimal entity decoding). This
// is sized exactly for what .rbxlx documents actually use: nested
// <Item class="..." referent="..."> elements containing <Properties> with
// typed leaf tags (<string name="Name">...</string>, etc). Pulling in a
// full XML dependency for that shape felt like the wrong trade for a
// stub; if real-world .rbxlx corpora turn up features this doesn't handle,
// swapping in pugixml/tinyxml2 behind this same struct is a contained
// change, not a redesign.
struct XmlNode {
    std::string tag;
    std::vector<XmlAttribute> attributes;
    std::string text;
    std::vector<XmlNode> children;

    [[nodiscard]] const std::string* attribute(const std::string& name) const {
        for (const auto& a : attributes) {
            if (a.name == name) return &a.value;
        }
        return nullptr;
    }
};

// Parses the *XML* Roblox place/model format (.rbxlx / .rbxmx) per
// docs/ARCHITECTURE.md §7. Returns std::nullopt on malformed input.
//
// Roblox's *binary* format (.rbxl / .rbxm) is not an XML variant at all --
// it's a separate chunked binary container (documented by community tools
// like Rojo's binary reader). A real importer needs an entirely different
// parser for it; that's out of scope here, matching the implementation
// task's explicit ".rbxlx XML parser stub" wording rather than silently
// also claiming binary support this doesn't have.
class RbxlxParser {
public:
    [[nodiscard]] static std::optional<XmlNode> parse(const std::string& xmlSource);
};

} // namespace engine::migration
