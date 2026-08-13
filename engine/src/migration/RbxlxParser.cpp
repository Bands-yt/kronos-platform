#include "migration/RbxlxParser.hpp"

#include <cctype>

namespace engine::migration {

namespace {

// Cursor-based recursive-descent scanner over the source string. Kept as
// a free-function set closed over `src`/`pos` rather than a class -- there
// is exactly one entry point (parseDocument) and no reason to expose
// intermediate parser state.
struct Cursor {
    const std::string& src;
    size_t pos = 0;

    [[nodiscard]] bool eof() const { return pos >= src.size(); }
    [[nodiscard]] char peek() const { return eof() ? '\0' : src[pos]; }

    void skipWhitespace() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) ++pos;
    }

    bool consume(const char* literal) {
        size_t len = std::char_traits<char>::length(literal);
        if (src.compare(pos, len, literal) == 0) {
            pos += len;
            return true;
        }
        return false;
    }

    void skipUntil(const char* terminator) {
        size_t end = src.find(terminator, pos);
        pos = (end == std::string::npos) ? src.size() : end + std::char_traits<char>::length(terminator);
    }
};

std::string decodeEntities(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '&') {
            if (raw.compare(i, 4, "&lt;") == 0) { out += '<'; i += 3; continue; }
            if (raw.compare(i, 4, "&gt;") == 0) { out += '>'; i += 3; continue; }
            if (raw.compare(i, 5, "&amp;") == 0) { out += '&'; i += 4; continue; }
            if (raw.compare(i, 6, "&quot;") == 0) { out += '"'; i += 5; continue; }
            if (raw.compare(i, 6, "&apos;") == 0) { out += '\''; i += 5; continue; }
        }
        out += raw[i];
    }
    return out;
}

std::string readTagName(Cursor& c) {
    size_t start = c.pos;
    while (!c.eof() && (std::isalnum(static_cast<unsigned char>(c.peek())) || c.peek() == '_' || c.peek() == '-' || c.peek() == ':')) {
        ++c.pos;
    }
    return c.src.substr(start, c.pos - start);
}

bool parseElement(Cursor& c, XmlNode& outNode); // fwd decl (mutually recursive with children parsing)

void parseChildren(Cursor& c, XmlNode& node) {
    for (;;) {
        c.skipWhitespace();
        if (c.eof()) return;

        if (c.src.compare(c.pos, 4, "<!--") == 0) {
            c.skipUntil("-->");
            continue;
        }
        if (c.src.compare(c.pos, 2, "</") == 0) {
            return; // closing tag -- let the caller consume it
        }
        if (c.peek() == '<') {
            XmlNode child;
            if (!parseElement(c, child)) return;
            node.children.push_back(std::move(child));
            continue;
        }

        // Text content up to the next '<'.
        size_t start = c.pos;
        while (!c.eof() && c.peek() != '<') ++c.pos;
        node.text += decodeEntities(c.src.substr(start, c.pos - start));
    }
}

bool parseElement(Cursor& c, XmlNode& outNode) {
    if (!c.consume("<")) return false;
    if (c.peek() == '?') { c.skipUntil("?>"); return false; } // <?xml ... ?> prolog -- not an element
    if (c.src.compare(c.pos, 3, "!--") == 0) { c.skipUntil("-->"); return false; }

    outNode.tag = readTagName(c);
    if (outNode.tag.empty()) return false;

    for (;;) {
        c.skipWhitespace();
        if (c.peek() == '/' && c.src.compare(c.pos, 2, "/>") == 0) {
            c.pos += 2;
            return true; // self-closing, no children
        }
        if (c.peek() == '>') {
            ++c.pos;
            break;
        }
        if (c.eof()) return false; // malformed

        std::string attrName = readTagName(c);
        if (attrName.empty()) return false;
        c.skipWhitespace();
        if (!c.consume("=")) return false;
        c.skipWhitespace();
        char quote = c.peek();
        if (quote != '"' && quote != '\'') return false;
        ++c.pos;
        size_t start = c.pos;
        while (!c.eof() && c.peek() != quote) ++c.pos;
        std::string attrValue = decodeEntities(c.src.substr(start, c.pos - start));
        if (!c.eof()) ++c.pos; // closing quote
        outNode.attributes.push_back({std::move(attrName), std::move(attrValue)});
    }

    parseChildren(c, outNode);

    // Expect and consume the matching </tag>.
    if (c.consume("</")) {
        readTagName(c);
        c.skipWhitespace();
        c.consume(">");
    }
    return true;
}

} // namespace

std::optional<XmlNode> RbxlxParser::parse(const std::string& xmlSource) {
    Cursor cursor{xmlSource, 0};
    cursor.skipWhitespace();

    // Skip a leading <?xml ...?> prolog if present.
    if (cursor.src.compare(cursor.pos, 5, "<?xml") == 0) {
        cursor.skipUntil("?>");
        cursor.skipWhitespace();
    }

    XmlNode root;
    if (!parseElement(cursor, root) || root.tag.empty()) {
        return std::nullopt;
    }
    return root;
}

} // namespace engine::migration
