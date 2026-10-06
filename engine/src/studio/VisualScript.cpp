#include "studio/VisualScript.hpp"

#include <algorithm>
#include <locale>
#include <map>
#include <sstream>

namespace engine::studio {

const char* vsTypeName(VsType type) {
    switch (type) {
        case VsType::Exec: return "Exec";
        case VsType::Number: return "Number";
        case VsType::Bool: return "Boolean";
        case VsType::String: return "Text";
        case VsType::Vector: return "Vector";
        case VsType::Entity: return "Object";
        case VsType::Any: return "Any";
    }
    return "Any";
}

namespace {

struct PinSpec {
    bool isOutput = false;
    VsType type = VsType::Number;
    const char* label = "";
    VsLiteral value;
    bool isColor = false;
};

struct KindInfo {
    const char* name;
    const char* key;
    const char* category;
    const char* description;
    std::vector<PinSpec> pins;
};

PinSpec execIn() { return {false, VsType::Exec, ""}; }
PinSpec execOut(const char* label = "") { return {true, VsType::Exec, label}; }
PinSpec in(VsType type, const char* label) { return {false, type, label}; }
PinSpec out(VsType type, const char* label) { return {true, type, label}; }
PinSpec number(const char* label, float value) {
    PinSpec spec{false, VsType::Number, label};
    spec.value.x = value;
    return spec;
}
PinSpec vec(const char* label, float x, float y, float z, bool color = false) {
    PinSpec spec{false, VsType::Vector, label};
    spec.value.x = x;
    spec.value.y = y;
    spec.value.z = z;
    spec.isColor = color;
    return spec;
}
PinSpec anyText(const char* label, const char* text) {
    PinSpec spec{false, VsType::Any, label};
    spec.value.text = text;
    return spec;
}
PinSpec text(const char* label, const char* value) {
    PinSpec spec{false, VsType::String, label};
    spec.value.text = value;
    return spec;
}

KindInfo binaryMath(const char* name, const char* key, const char* description, float a, float b) {
    return {name, key, "Math", description, {number("A", a), number("B", b), out(VsType::Number, "Result")}};
}
KindInfo unaryMath(const char* name, const char* key, const char* description) {
    return {name, key, "Math", description, {number("In", 0.0f), out(VsType::Number, "Result")}};
}
KindInfo entityAction(const char* name, const char* key, const char* description, PinSpec value) {
    return {name, key, "Actions", description, {execIn(), in(VsType::Entity, "Target"), value, execOut("Then")}};
}

const std::vector<KindInfo>& kindInfos() {
    using T = VsType;
    static const std::vector<KindInfo> kInfos = [] {
        std::vector<KindInfo> infos = {
            {"On Start", "on_start", "Events", "Runs once when the game starts.", {execOut()}},
            {"On Update", "on_update", "Events", "Runs every frame. Delta Time is the seconds since the last frame.",
             {execOut(), out(T::Number, "Delta Time")}},
            {"On Touched", "on_touched", "Events", "Runs when another object bumps into this one.",
             {execOut(), out(T::Entity, "Other")}},
            {"On Interact", "on_interact", "Events", "Runs when a player interacts with this object.",
             {execOut(), out(T::Entity, "Interactor")}},
            {"On Player Joined", "on_player_joined", "Events", "Runs when a player joins the game.",
             {execOut(), out(T::Number, "Player Id"), out(T::String, "Name")}},
            {"On Player Left", "on_player_left", "Events", "Runs when a player leaves the game.",
             {execOut(), out(T::Number, "Player Id"), out(T::String, "Name")}},
            {"Every Interval", "every_interval", "Events", "Runs again and again, waiting Seconds in between.",
             {number("Seconds", 1.0f), execOut()}},
            {"Branch", "branch", "Flow", "Goes one way if Condition is true, the other way if not.",
             {execIn(), in(T::Bool, "Condition"), execOut("True"), execOut("False")}},
            {"Sequence", "sequence", "Flow", "Runs First, then Second, then Third.",
             {execIn(), execOut("First"), execOut("Second"), execOut("Third")}},
            {"Wait", "wait", "Flow", "Pauses for Seconds, then carries on.",
             {execIn(), number("Seconds", 1.0f), execOut("Then")}},
            {"For Loop", "for_loop", "Flow", "Runs Body once for every number from First to Last.",
             {execIn(), number("First", 1.0f), number("Last", 10.0f), execOut("Body"), out(T::Number, "Index"),
              execOut("Completed")}},
            {"Print", "print", "Actions", "Writes a message to the output log.",
             {execIn(), anyText("Value", "Hello!"), execOut("Then")}},
            entityAction("Set Position", "set_position", "Moves an object to a position.", vec("Position", 0, 0, 0)),
            entityAction("Move By", "move_by", "Moves an object by an offset.", vec("Offset", 0, 1, 0)),
            entityAction("Set Rotation", "set_rotation", "Turns an object to face an angle (degrees).",
                         vec("Degrees", 0, 0, 0)),
            entityAction("Rotate By", "rotate_by", "Turns an object a little more (degrees).", vec("Degrees", 0, 1, 0)),
            entityAction("Set Size", "set_scale", "Changes an object's size.", vec("Size", 1, 1, 1)),
            entityAction("Set Color", "set_color", "Paints an object a colour.", vec("Color", 1.0f, 0.3f, 0.3f, true)),
            {"Set Glow", "set_emissive", "Actions", "Makes an object glow.",
             {execIn(), in(T::Entity, "Target"), vec("Color", 1, 1, 1, true), number("Strength", 1.0f), execOut("Then")}},
            entityAction("Push", "apply_impulse", "Gives a physics object a push.", vec("Force", 0, 5, 0)),
            entityAction("Set Velocity", "set_velocity", "Sets how fast a physics object is moving.",
                         vec("Velocity", 0, 0, 0)),
            {"Destroy", "destroy", "Actions", "Removes an object from the game.",
             {execIn(), in(T::Entity, "Target"), execOut("Then")}},
            {"Spawn Box", "spawn_box", "Actions", "Creates a new physics box.",
             {execIn(), vec("Position", 0, 5, 0), vec("Size", 1, 1, 1), number("Mass", 1.0f),
              vec("Color", 0.8f, 0.8f, 0.8f, true), execOut("Then"), out(T::Entity, "Box")}},
            {"Set Variable", "set_variable", "Variables", "Remembers a value under a name.",
             {execIn(), anyText("Value", "0"), execOut("Then")}},
            {"Self", "self", "Objects", "The object this script is attached to.", {out(T::Entity, "Self")}},
            {"Find Object", "find_object", "Objects", "Finds an object by its name.",
             {text("Name", "Part"), out(T::Entity, "Object")}},
            {"Get Position", "get_position", "Objects", "Where an object is.",
             {in(T::Entity, "Target"), out(T::Vector, "Position")}},
            {"Get Rotation", "get_rotation", "Objects", "Which way an object is turned (degrees).",
             {in(T::Entity, "Target"), out(T::Vector, "Degrees")}},
            {"Get Variable", "get_variable", "Variables", "Reads a value remembered with Set Variable.",
             {out(T::Any, "Value")}},
            {"Time", "time", "Values", "Seconds since the script started.", {out(T::Number, "Seconds")}},
            {"Number", "number", "Values", "A number.", {out(T::Number, "Value")}},
            {"Boolean", "boolean", "Values", "True or false.", {out(T::Bool, "Value")}},
            {"Text", "text", "Values", "Some text.", {out(T::String, "Value")}},
            {"Vector", "vector", "Values", "Three numbers: X, Y and Z.", {out(T::Vector, "Value")}},
            binaryMath("Add", "add", "A + B", 0.0f, 0.0f),
            binaryMath("Subtract", "subtract", "A - B", 0.0f, 0.0f),
            binaryMath("Multiply", "multiply", "A x B", 0.0f, 1.0f),
            binaryMath("Divide", "divide", "A / B (dividing by zero gives zero)", 0.0f, 1.0f),
            binaryMath("Remainder", "modulo", "What is left over after dividing A by B", 0.0f, 2.0f),
            binaryMath("Power", "power", "A to the power of B", 0.0f, 2.0f),
            unaryMath("Absolute", "abs", "Removes the minus sign"),
            binaryMath("Min", "min", "The smaller of A and B", 0.0f, 0.0f),
            binaryMath("Max", "max", "The bigger of A and B", 0.0f, 0.0f),
            {"Clamp", "clamp", "Math", "Keeps Value between Min and Max.",
             {number("Value", 0.0f), number("Min", 0.0f), number("Max", 1.0f), out(T::Number, "Result")}},
            {"Lerp", "lerp", "Math", "Blends from A to B; T = 0 gives A, T = 1 gives B.",
             {number("A", 0.0f), number("B", 1.0f), number("T", 0.5f), out(T::Number, "Result")}},
            unaryMath("Sine", "sin", "Smooth wave between -1 and 1 (input in radians)"),
            unaryMath("Cosine", "cos", "Smooth wave between -1 and 1 (input in radians)"),
            unaryMath("Floor", "floor", "Rounds down"),
            unaryMath("Round", "round", "Rounds to the nearest whole number"),
            {"Random", "random", "Math", "A random number between Min and Max.",
             {number("Min", 0.0f), number("Max", 1.0f), out(T::Number, "Result")}},
            {"Make Vector", "make_vector", "Vectors", "Builds a vector from X, Y and Z.",
             {number("X", 0.0f), number("Y", 0.0f), number("Z", 0.0f), out(T::Vector, "Vector")}},
            {"Break Vector", "break_vector", "Vectors", "Splits a vector into X, Y and Z.",
             {vec("Vector", 0, 0, 0), out(T::Number, "X"), out(T::Number, "Y"), out(T::Number, "Z")}},
            {"Add Vectors", "add_vectors", "Vectors", "A + B",
             {vec("A", 0, 0, 0), vec("B", 0, 0, 0), out(T::Vector, "Result")}},
            {"Subtract Vectors", "subtract_vectors", "Vectors", "A - B",
             {vec("A", 0, 0, 0), vec("B", 0, 0, 0), out(T::Vector, "Result")}},
            {"Scale Vector", "scale_vector", "Vectors", "Multiplies every part of a vector by Scale.",
             {vec("Vector", 0, 0, 0), number("Scale", 1.0f), out(T::Vector, "Result")}},
            {"Vector Length", "vector_length", "Vectors", "How long a vector is.",
             {vec("Vector", 0, 0, 0), out(T::Number, "Length")}},
            {"Distance", "distance", "Vectors", "How far apart two positions are.",
             {vec("A", 0, 0, 0), vec("B", 0, 0, 0), out(T::Number, "Distance")}},
            {"Normalize", "normalize", "Vectors", "Same direction, length 1.",
             {vec("Vector", 0, 1, 0), out(T::Vector, "Result")}},
            {"Greater Than", "greater", "Logic", "True when A > B.",
             {number("A", 0.0f), number("B", 0.0f), out(T::Bool, "Result")}},
            {"Less Than", "less", "Logic", "True when A < B.",
             {number("A", 0.0f), number("B", 0.0f), out(T::Bool, "Result")}},
            {"Equal", "equal", "Logic", "True when A and B are the same.",
             {anyText("A", "0"), anyText("B", "0"), out(T::Bool, "Result")}},
            {"And", "and", "Logic", "True when both are true.",
             {in(T::Bool, "A"), in(T::Bool, "B"), out(T::Bool, "Result")}},
            {"Or", "or", "Logic", "True when either is true.",
             {in(T::Bool, "A"), in(T::Bool, "B"), out(T::Bool, "Result")}},
            {"Not", "not", "Logic", "Flips true and false.", {in(T::Bool, "In"), out(T::Bool, "Result")}},
            {"Join Text", "join_text", "Values", "Sticks two values together as text.",
             {anyText("A", "Score: "), anyText("B", ""), out(T::String, "Text")}},
        };
        return infos;
    }();
    return kInfos;
}

const KindInfo& kindInfo(VsNodeKind kind) { return kindInfos()[static_cast<size_t>(kind)]; }

std::string hexEncode(const std::string& text) {
    if (text.empty()) return "-";
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : text) {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 15]);
    }
    return out;
}

bool hexDecode(const std::string& hex, std::string& out) {
    out.clear();
    if (hex == "-") return true;
    if (hex.size() % 2 != 0) return false;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = digit(hex[i]);
        const int lo = digit(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>(hi * 16 + lo));
    }
    return true;
}

constexpr const char* kSerializedHeader = "kronos-visual-script 1";

std::vector<VsLiteral> initialLiterals(VsNodeKind kind) {
    std::vector<VsLiteral> literals;
    for (const PinSpec& spec : kindInfo(kind).pins) literals.push_back(spec.value);
    if (kind == VsNodeKind::Number) literals[0].x = 1.0f;
    if (kind == VsNodeKind::Boolean) literals[0].flag = true;
    if (kind == VsNodeKind::Text) literals[0].text = "Hello!";
    return literals;
}

} // namespace

const char* vsNodeKindName(VsNodeKind kind) { return kindInfo(kind).name; }
const char* vsNodeKindKey(VsNodeKind kind) { return kindInfo(kind).key; }
const char* vsNodeKindCategory(VsNodeKind kind) { return kindInfo(kind).category; }
const char* vsNodeKindDescription(VsNodeKind kind) { return kindInfo(kind).description; }

bool vsNodeKindFromKey(const std::string& key, VsNodeKind& out) {
    for (int i = 0; i < kVsNodeKindCount; ++i) {
        if (key == kindInfo(static_cast<VsNodeKind>(i)).key) {
            out = static_cast<VsNodeKind>(i);
            return true;
        }
    }
    return false;
}

bool vsNodeKindIsEvent(VsNodeKind kind) { return std::string(kindInfo(kind).category) == "Events"; }

bool vsNodeKindHasField(VsNodeKind kind) { return kind == VsNodeKind::SetVariable || kind == VsNodeKind::GetVariable; }

int VisualScriptGraph::addNode(VsNodeKind kind, float positionX, float positionY) {
    static_assert(kVsNodeKindCount == 65);
    VsNode node;
    node.id = nextId_++;
    node.kind = kind;
    node.positionX = positionX;
    node.positionY = positionY;
    if (vsNodeKindHasField(kind)) node.field = "score";
    for (const PinSpec& spec : kindInfo(kind).pins) {
        VsPin pin;
        pin.id = nextId_++;
        pin.nodeId = node.id;
        pin.isOutput = spec.isOutput;
        pin.type = spec.type;
        pin.isColor = spec.isColor;
        pin.label = spec.label;
        node.pinIds.push_back(pin.id);
        pins_.push_back(pin);
    }
    node.literals = initialLiterals(kind);
    nodes_.push_back(std::move(node));
    return nodes_.back().id;
}

void VisualScriptGraph::removeNode(int nodeId) {
    const VsNode* node = findNode(nodeId);
    if (node == nullptr) return;
    const std::vector<int> pinIds = node->pinIds;
    auto touches = [&](int pinId) { return std::find(pinIds.begin(), pinIds.end(), pinId) != pinIds.end(); };
    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [&](const VsLink& link) { return touches(link.outputPinId) || touches(link.inputPinId); }),
                 links_.end());
    pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [&](const VsPin& pin) { return pin.nodeId == nodeId; }),
                pins_.end());
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const VsNode& n) { return n.id == nodeId; }),
                 nodes_.end());
}

bool VisualScriptGraph::canConnect(VsType from, VsType to) {
    if (from == VsType::Exec || to == VsType::Exec) return from == to;
    return from == to || from == VsType::Any || to == VsType::Any;
}

bool VisualScriptGraph::addLink(int outputPinId, int inputPinId, std::string& outError) {
    const VsPin* outputPin = findPin(outputPinId);
    const VsPin* inputPin = findPin(inputPinId);
    if (outputPin == nullptr || inputPin == nullptr) {
        outError = "one or both pins do not exist";
        return false;
    }
    if (!outputPin->isOutput || inputPin->isOutput) {
        outError = "links go from an output pin to an input pin";
        return false;
    }
    if (outputPin->nodeId == inputPin->nodeId) {
        outError = "cannot connect a node to itself";
        return false;
    }
    if (!canConnect(outputPin->type, inputPin->type)) {
        outError = std::string("cannot connect ") + vsTypeName(outputPin->type) + " to " + vsTypeName(inputPin->type);
        return false;
    }
    const bool exec = outputPin->type == VsType::Exec;
    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [&](const VsLink& link) {
                                    return exec ? link.outputPinId == outputPinId : link.inputPinId == inputPinId;
                                }),
                 links_.end());
    VsLink link;
    link.id = nextId_++;
    link.outputPinId = outputPinId;
    link.inputPinId = inputPinId;
    links_.push_back(link);
    return true;
}

void VisualScriptGraph::removeLink(int linkId) {
    links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const VsLink& link) { return link.id == linkId; }),
                 links_.end());
}

void VisualScriptGraph::clear() {
    nextId_ = 1;
    nodes_.clear();
    pins_.clear();
    links_.clear();
}

VsNode* VisualScriptGraph::findNode(int nodeId) {
    for (auto& node : nodes_) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const VsNode* VisualScriptGraph::findNode(int nodeId) const {
    for (const auto& node : nodes_) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const VsPin* VisualScriptGraph::findPin(int pinId) const {
    for (const auto& pin : pins_) {
        if (pin.id == pinId) return &pin;
    }
    return nullptr;
}

const VsLink* VisualScriptGraph::findLinkInto(int inputPinId) const {
    for (const auto& link : links_) {
        if (link.inputPinId == inputPinId) return &link;
    }
    return nullptr;
}

const VsLink* VisualScriptGraph::findLinkFrom(int outputPinId) const {
    for (const auto& link : links_) {
        if (link.outputPinId == outputPinId) return &link;
    }
    return nullptr;
}

int VisualScriptGraph::pinIndex(int pinId) const {
    const VsPin* pin = findPin(pinId);
    if (pin == nullptr) return -1;
    const VsNode* node = findNode(pin->nodeId);
    if (node == nullptr) return -1;
    auto it = std::find(node->pinIds.begin(), node->pinIds.end(), pinId);
    return it == node->pinIds.end() ? -1 : static_cast<int>(it - node->pinIds.begin());
}

VsLiteral* VisualScriptGraph::literal(int pinId) {
    const VsPin* pin = findPin(pinId);
    VsNode* node = pin != nullptr ? findNode(pin->nodeId) : nullptr;
    const int index = pinIndex(pinId);
    if (node == nullptr || index < 0) return nullptr;
    return &node->literals[static_cast<size_t>(index)];
}

const VsLiteral* VisualScriptGraph::literal(int pinId) const {
    return const_cast<VisualScriptGraph*>(this)->literal(pinId);
}

std::string VisualScriptGraph::serialize() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(9);
    out << kSerializedHeader << '\n';
    for (const VsNode& node : nodes_) {
        out << "node " << node.id << ' ' << vsNodeKindKey(node.kind) << ' ' << node.positionX << ' ' << node.positionY
            << ' ' << hexEncode(node.field) << '\n';
        const std::vector<VsLiteral> defaults = initialLiterals(node.kind);
        for (size_t i = 0; i < node.literals.size(); ++i) {
            const VsLiteral& value = node.literals[i];
            const VsLiteral& base = defaults[i];
            if (value.x == base.x && value.y == base.y && value.z == base.z && value.flag == base.flag &&
                value.text == base.text) {
                continue;
            }
            out << "value " << node.id << ' ' << i << ' ' << value.x << ' ' << value.y << ' ' << value.z << ' '
                << (value.flag ? 1 : 0) << ' ' << hexEncode(value.text) << '\n';
        }
    }
    for (const VsLink& link : links_) {
        const VsPin* from = findPin(link.outputPinId);
        const VsPin* to = findPin(link.inputPinId);
        if (from == nullptr || to == nullptr) continue;
        out << "link " << from->nodeId << ' ' << pinIndex(from->id) << ' ' << to->nodeId << ' ' << pinIndex(to->id)
            << '\n';
    }
    return out.str();
}

bool VisualScriptGraph::deserialize(const std::string& text, VisualScriptGraph& out, std::string& outError) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(in, line) || line != kSerializedHeader) {
        outError = "not a Kronos visual script (missing \"kronos-visual-script 1\" header)";
        return false;
    }
    VisualScriptGraph graph;
    std::map<int, int> nodeIdMap;
    int lineNumber = 1;
    auto fail = [&](const std::string& message) {
        outError = "line " + std::to_string(lineNumber) + ": " + message;
        return false;
    };
    auto nodeFor = [&](int oldId) -> VsNode* {
        auto it = nodeIdMap.find(oldId);
        return it == nodeIdMap.end() ? nullptr : graph.findNode(it->second);
    };
    while (std::getline(in, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        std::string tag;
        fields >> tag;
        if (tag == "node") {
            int id = 0;
            std::string key, field;
            float x = 0.0f, y = 0.0f;
            fields >> id >> key >> x >> y >> field;
            VsNodeKind kind{};
            if (fields.fail() || !vsNodeKindFromKey(key, kind)) return fail("bad node \"" + key + "\"");
            const int newId = graph.addNode(kind, x, y);
            if (!hexDecode(field, graph.findNode(newId)->field)) return fail("bad node field");
            nodeIdMap[id] = newId;
        } else if (tag == "value") {
            int nodeId = 0, flag = 0;
            size_t index = 0;
            VsLiteral value;
            std::string hex;
            fields >> nodeId >> index >> value.x >> value.y >> value.z >> flag >> hex;
            VsNode* node = nodeFor(nodeId);
            if (fields.fail() || node == nullptr || index >= node->literals.size() || !hexDecode(hex, value.text)) {
                return fail("bad value");
            }
            value.flag = flag != 0;
            node->literals[index] = value;
        } else if (tag == "link") {
            int fromNode = 0, fromPin = 0, toNode = 0, toPin = 0;
            fields >> fromNode >> fromPin >> toNode >> toPin;
            auto pinOf = [&](int oldNode, int index) -> int {
                const VsNode* node = nodeFor(oldNode);
                if (node == nullptr || index < 0 || index >= static_cast<int>(node->pinIds.size())) return 0;
                return node->pinIds[static_cast<size_t>(index)];
            };
            const int outputPin = pinOf(fromNode, fromPin);
            const int inputPin = pinOf(toNode, toPin);
            std::string linkError;
            if (fields.fail() || outputPin == 0 || inputPin == 0 || !graph.addLink(outputPin, inputPin, linkError)) {
                return fail("bad link" + (linkError.empty() ? std::string{} : " (" + linkError + ")"));
            }
        } else {
            return fail("unknown entry \"" + tag + "\"");
        }
    }
    out = std::move(graph);
    return true;
}

VisualScriptGraph VisualScriptGraph::makeDefault() {
    VisualScriptGraph graph;
    const int update = graph.addNode(VsNodeKind::OnUpdate, 40.0f, 60.0f);
    const int scale = graph.addNode(VsNodeKind::ScaleVector, 260.0f, 180.0f);
    const int rotate = graph.addNode(VsNodeKind::RotateBy, 500.0f, 60.0f);
    graph.findNode(scale)->literals[0] = VsLiteral{0.0f, 90.0f, 0.0f, false, {}};
    std::string error;
    const VsNode& u = *graph.findNode(update);
    const VsNode& s = *graph.findNode(scale);
    const VsNode& r = *graph.findNode(rotate);
    (void)graph.addLink(u.pinIds[0], r.pinIds[0], error);
    (void)graph.addLink(u.pinIds[1], s.pinIds[1], error);
    (void)graph.addLink(s.pinIds[2], r.pinIds[2], error);
    return graph;
}

} // namespace engine::studio
