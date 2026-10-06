#include "studio/VisualScriptCompiler.hpp"

#include <cmath>
#include <cstdio>
#include <locale>
#include <set>
#include <sstream>

#include <Luau/Compiler.h>

namespace engine::studio {

bool isGeneratedVisualScriptSource(const std::string& source) {
    return source.rfind(kVisualScriptSourceHeader, 0) == 0;
}

namespace {

using K = VsNodeKind;

constexpr int kMaxStatements = 4000;

std::string number(double value) {
    if (!std::isfinite(value)) return "0";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(9);
    out << value;
    return out.str();
}

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 32 || c == 127) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\%03u", static_cast<unsigned>(c));
                    out += escaped;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out + "\"";
}

std::string vectorLiteral(const VsLiteral& value) {
    return "vector.create(" + number(value.x) + ", " + number(value.y) + ", " + number(value.z) + ")";
}

// Any-typed literals are typed as text; numbers and true/false keep their type.
std::string anyLiteral(const std::string& text) {
    if (text == "true" || text == "false") return text;
    if (!text.empty()) {
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        double value = 0.0;
        in >> value;
        if (!in.fail() && in.eof()) return number(value);
    }
    return quote(text);
}

struct Helper {
    const char* name;
    const char* code;
    const char* dependsOn;
};

const std::vector<Helper>& helperTable() {
    static const std::vector<Helper> kHelpers = {
        {"time", "local __startTime = os.clock()\n", nullptr},
        {"getPosition",
         "local function __getPosition(target)\n"
         "    if target == nil then return vector.create(0, 0, 0) end\n"
         "    local x, y, z = world.getPosition(target)\n"
         "    if x == nil then return vector.create(0, 0, 0) end\n"
         "    return vector.create(x, y, z)\n"
         "end\n",
         nullptr},
        {"getRotation",
         "local function __getRotation(target)\n"
         "    if target == nil then return vector.create(0, 0, 0) end\n"
         "    local x, y, z = world.getRotation(target)\n"
         "    if x == nil then return vector.create(0, 0, 0) end\n"
         "    return vector.create(x, y, z)\n"
         "end\n",
         nullptr},
        {"setPosition",
         "local function __setPosition(target, v)\n"
         "    if target ~= nil then world.setPosition(target, v.x, v.y, v.z) end\n"
         "end\n",
         nullptr},
        {"moveBy",
         "local function __moveBy(target, v)\n"
         "    if target == nil then return end\n"
         "    local p = __getPosition(target)\n"
         "    world.setPosition(target, p.x + v.x, p.y + v.y, p.z + v.z)\n"
         "end\n",
         "getPosition"},
        {"setRotation",
         "local function __setRotation(target, v)\n"
         "    if target ~= nil then world.setRotation(target, v.x, v.y, v.z) end\n"
         "end\n",
         nullptr},
        {"rotateBy",
         "local function __rotateBy(target, v)\n"
         "    if target ~= nil then world.rotateBy(target, v.x, v.y, v.z) end\n"
         "end\n",
         nullptr},
        {"setScale",
         "local function __setScale(target, v)\n"
         "    if target ~= nil then world.setScale(target, v.x, v.y, v.z) end\n"
         "end\n",
         nullptr},
        {"setColor",
         "local function __setColor(target, c)\n"
         "    if target ~= nil then world.setColor(target, c.x, c.y, c.z) end\n"
         "end\n",
         nullptr},
        {"setGlow",
         "local function __setGlow(target, c, strength)\n"
         "    if target ~= nil then world.setEmissive(target, c.x, c.y, c.z, strength) end\n"
         "end\n",
         nullptr},
        {"push",
         "local function __push(target, v)\n"
         "    if target ~= nil then world.applyImpulse(target, v.x, v.y, v.z) end\n"
         "end\n",
         nullptr},
        {"setVelocity",
         "local function __setVelocity(target, v)\n"
         "    if target ~= nil then world.setVelocity(target, v.x, v.y, v.z) end\n"
         "end\n",
         nullptr},
        {"destroy",
         "local function __destroy(target)\n"
         "    if target ~= nil then world.destroy(target) end\n"
         "end\n",
         nullptr},
        {"spawnBox",
         "local function __spawnBox(p, s, mass, c)\n"
         "    return world.spawnDynamicBox(p.x, p.y, p.z, s.x * 0.5, s.y * 0.5, s.z * 0.5, mass, c.x, c.y, c.z)\n"
         "end\n",
         nullptr},
        {"div",
         "local function __div(a, b)\n"
         "    if b == 0 then return 0 end\n"
         "    return a / b\n"
         "end\n",
         nullptr},
        {"mod",
         "local function __mod(a, b)\n"
         "    if b == 0 then return 0 end\n"
         "    return a % b\n"
         "end\n",
         nullptr},
        {"clamp",
         "local function __clamp(v, lo, hi)\n"
         "    if lo > hi then lo, hi = hi, lo end\n"
         "    return math.clamp(v, lo, hi)\n"
         "end\n",
         nullptr},
        {"normalize",
         "local function __normalize(v)\n"
         "    local length = vector.magnitude(v)\n"
         "    if length < 1e-9 then return v end\n"
         "    return v / length\n"
         "end\n",
         nullptr},
    };
    return kHelpers;
}

class Generator {
public:
    explicit Generator(const VisualScriptGraph& graph) : graph_(graph) {}

    VisualScriptCompileResult run() {
        VisualScriptCompileResult result;
        std::vector<std::string> deferred;
        std::set<int> reachedAnywhere;
        int eventCount = 0;

        for (const VsNode& node : graph_.nodes()) {
            if (!vsNodeKindIsEvent(node.kind)) continue;
            ++eventCount;
            emitEvent(node, deferred, reachedAnywhere);
            if (failed_) break;
        }

        if (failed_) {
            result.error = error_;
            result.errorNodeId = errorNode_;
            return result;
        }

        int unreached = 0;
        for (const VsNode& node : graph_.nodes()) {
            if (hasExecPins(node) && !vsNodeKindIsEvent(node.kind) && reachedAnywhere.count(node.id) == 0) ++unreached;
        }
        if (eventCount == 0) result.warnings.push_back("Add an event such as On Start or On Update so the script runs.");
        if (unreached > 0) {
            result.warnings.push_back(std::to_string(unreached) +
                                      " action node(s) are not connected to an event, so they never run.");
        }

        std::ostringstream source;
        source << kVisualScriptSourceHeader << "\n";
        source << "local self = script and script.entity\n";
        source << "local vars = {}\n";
        for (const Helper& helper : helperTable()) {
            if (helpers_.count(helper.name) != 0) source << helper.code;
        }
        source << body_.str();
        for (const std::string& line : deferred) source << line << "\n";
        result.luau = source.str();
        result.success = true;
        return result;
    }

private:
    struct Handler {
        std::set<std::string> declared;
    };

    const VisualScriptGraph& graph_;
    std::ostringstream body_;
    std::string indent_;
    std::set<std::string> helpers_;
    Handler* handler_ = nullptr;
    std::vector<std::string> pre_;
    std::set<int> pureDone_;
    std::set<int> pureVisiting_;
    std::set<int> execStack_;
    int statements_ = 0;
    bool failed_ = false;
    std::string error_;
    int errorNode_ = 0;

    void fail(int nodeId, const std::string& message) {
        if (failed_) return;
        failed_ = true;
        errorNode_ = nodeId;
        const VsNode* node = graph_.findNode(nodeId);
        error_ = node != nullptr ? std::string(vsNodeKindName(node->kind)) + ": " + message : message;
    }

    void line(const std::string& text) { body_ << indent_ << text << "\n"; }
    void push() { indent_ += "    "; }
    void pop() { indent_.resize(indent_.size() >= 4 ? indent_.size() - 4 : 0); }

    void useHelper(const std::string& name) {
        for (const Helper& helper : helperTable()) {
            if (name == helper.name) {
                helpers_.insert(name);
                if (helper.dependsOn != nullptr) helpers_.insert(helper.dependsOn);
            }
        }
    }

    [[nodiscard]] const VsPin& pinAt(const VsNode& node, size_t index) const {
        return *graph_.findPin(node.pinIds[index]);
    }

    [[nodiscard]] bool hasExecPins(const VsNode& node) const {
        for (int pinId : node.pinIds) {
            if (graph_.findPin(pinId)->type == VsType::Exec) return true;
        }
        return false;
    }

    [[nodiscard]] int firstExecOutput(const VsNode& node) const {
        for (size_t i = 0; i < node.pinIds.size(); ++i) {
            const VsPin& pin = pinAt(node, i);
            if (pin.isOutput && pin.type == VsType::Exec) return static_cast<int>(i);
        }
        return -1;
    }

    [[nodiscard]] const VsNode* nextNode(const VsNode& node, int outputIndex) const {
        if (outputIndex < 0) return nullptr;
        const VsLink* link = graph_.findLinkFrom(node.pinIds[static_cast<size_t>(outputIndex)]);
        if (link == nullptr) return nullptr;
        const VsPin* target = graph_.findPin(link->inputPinId);
        return target != nullptr ? graph_.findNode(target->nodeId) : nullptr;
    }

    static std::string valueName(int nodeId, size_t index) {
        return "v" + std::to_string(nodeId) + "_" + std::to_string(index);
    }
    static std::string pureName(int nodeId, size_t index) {
        return "p" + std::to_string(nodeId) + "_" + std::to_string(index);
    }

    std::string literal(const VsPin& pin, const VsLiteral& value) {
        switch (pin.type) {
            case VsType::Number: return number(value.x);
            case VsType::Bool: return value.flag ? "true" : "false";
            case VsType::String: return quote(value.text);
            case VsType::Vector: return vectorLiteral(value);
            case VsType::Entity: return "self";
            case VsType::Any: return anyLiteral(value.text);
            case VsType::Exec: return "nil";
        }
        return "nil";
    }

    std::string input(const VsNode& node, size_t index) {
        const VsPin& pin = pinAt(node, index);
        const VsLink* link = graph_.findLinkInto(pin.id);
        if (link == nullptr) return literal(pin, node.literals[index]);
        const VsPin* source = graph_.findPin(link->outputPinId);
        const VsNode* sourceNode = source != nullptr ? graph_.findNode(source->nodeId) : nullptr;
        if (sourceNode == nullptr) return "nil";
        const auto sourceIndex = static_cast<size_t>(graph_.pinIndex(source->id));
        if (hasExecPins(*sourceNode)) {
            const std::string name = valueName(sourceNode->id, sourceIndex);
            if (handler_ == nullptr || handler_->declared.count(name) == 0) {
                fail(node.id, "\"" + pin.label + "\" uses " + vsNodeKindName(sourceNode->kind) + "'s \"" + source->label +
                                  "\", which only exists while that node's event is running.");
                return "nil";
            }
            return name;
        }
        emitPure(*sourceNode);
        return pureName(sourceNode->id, sourceIndex);
    }

    void emitPure(const VsNode& node) {
        if (pureDone_.count(node.id) != 0) return;
        if (pureVisiting_.count(node.id) != 0) {
            fail(node.id, "its value depends on itself. Break the loop of connections.");
            return;
        }
        pureVisiting_.insert(node.id);
        for (const auto& [index, expression] : pureExpressions(node)) {
            pre_.push_back("local " + pureName(node.id, index) + " = " + expression);
        }
        pureVisiting_.erase(node.id);
        pureDone_.insert(node.id);
    }

    std::vector<std::pair<size_t, std::string>> pureExpressions(const VsNode& node) {
        auto a = [&] { return input(node, 0); };
        auto binary = [&](const std::string& op) { return "(" + input(node, 0) + " " + op + " " + input(node, 1) + ")"; };
        auto call1 = [&](const std::string& fn) { return fn + "(" + input(node, 0) + ")"; };
        auto call2 = [&](const std::string& fn) { return fn + "(" + input(node, 0) + ", " + input(node, 1) + ")"; };
        const VsLiteral& own = node.literals.empty() ? VsLiteral{} : node.literals[0];
        switch (node.kind) {
            case K::Self: return {{0, "self"}};
            case K::FindEntity: return {{1, "world.findByName(tostring(" + a() + "))"}};
            case K::GetPosition: useHelper("getPosition"); return {{1, call1("__getPosition")}};
            case K::GetRotation: useHelper("getRotation"); return {{1, call1("__getRotation")}};
            case K::GetVariable: return {{0, "vars[" + quote(node.field) + "]"}};
            case K::Time: useHelper("time"); return {{0, "(os.clock() - __startTime)"}};
            case K::Number: return {{0, number(own.x)}};
            case K::Boolean: return {{0, own.flag ? "true" : "false"}};
            case K::Text: return {{0, quote(own.text)}};
            case K::VectorConstant: return {{0, vectorLiteral(own)}};
            case K::Add: return {{2, binary("+")}};
            case K::Subtract: return {{2, binary("-")}};
            case K::Multiply: return {{2, binary("*")}};
            case K::Divide: useHelper("div"); return {{2, call2("__div")}};
            case K::Modulo: useHelper("mod"); return {{2, call2("__mod")}};
            case K::Power: return {{2, binary("^")}};
            case K::Abs: return {{1, call1("math.abs")}};
            case K::Min: return {{2, call2("math.min")}};
            case K::Max: return {{2, call2("math.max")}};
            case K::Clamp:
                useHelper("clamp");
                return {{3, "__clamp(" + input(node, 0) + ", " + input(node, 1) + ", " + input(node, 2) + ")"}};
            case K::Lerp: {
                const std::string from = input(node, 0);
                return {{3, "(" + from + " + (" + input(node, 1) + " - " + from + ") * " + input(node, 2) + ")"}};
            }
            case K::Sin: return {{1, call1("math.sin")}};
            case K::Cos: return {{1, call1("math.cos")}};
            case K::Floor: return {{1, call1("math.floor")}};
            case K::Round: return {{1, call1("math.round")}};
            case K::RandomRange: {
                const std::string low = input(node, 0);
                return {{2, "(" + low + " + math.random() * (" + input(node, 1) + " - " + low + "))"}};
            }
            case K::MakeVector:
                return {{3, "vector.create(" + input(node, 0) + ", " + input(node, 1) + ", " + input(node, 2) + ")"}};
            case K::BreakVector: {
                const std::string v = a();
                return {{1, v + ".x"}, {2, v + ".y"}, {3, v + ".z"}};
            }
            case K::AddVectors: return {{2, binary("+")}};
            case K::SubtractVectors: return {{2, binary("-")}};
            case K::ScaleVector: return {{2, binary("*")}};
            case K::VectorLength: return {{1, call1("vector.magnitude")}};
            case K::Distance: return {{2, "vector.magnitude(" + binary("-") + ")"}};
            case K::Normalize: useHelper("normalize"); return {{1, call1("__normalize")}};
            case K::Greater: return {{2, binary(">")}};
            case K::Less: return {{2, binary("<")}};
            case K::Equal: return {{2, binary("==")}};
            case K::And: return {{2, binary("and")}};
            case K::Or: return {{2, binary("or")}};
            case K::Not: return {{1, "(not " + a() + ")"}};
            case K::JoinText: return {{2, "(tostring(" + input(node, 0) + ") .. tostring(" + input(node, 1) + "))"}};
            default: return {};
        }
    }

    bool openScope() {
        if (pre_.empty()) return false;
        line("do");
        push();
        for (const std::string& text : pre_) line(text);
        pre_.clear();
        return true;
    }

    void closeScope(bool opened) {
        if (!opened) return;
        pop();
        line("end");
    }

    void beginStatement() {
        pre_.clear();
        pureDone_.clear();
    }

    std::string actionCall(const char* helper, const VsNode& node, size_t argumentCount) {
        useHelper(helper);
        std::string call = std::string("__") + helper + "(";
        for (size_t i = 1; i <= argumentCount; ++i) call += (i > 1 ? ", " : "") + input(node, i);
        return call + ")";
    }

    void chain(const VsNode& node, int outputIndex) {
        if (const VsNode* next = nextNode(node, outputIndex)) emitNode(*next);
    }

    void emitNode(const VsNode& node) {
        if (failed_) return;
        if (++statements_ > kMaxStatements) {
            fail(node.id, "the script is too large (paths that rejoin are copied; try simplifying).");
            return;
        }
        if (execStack_.count(node.id) != 0) {
            fail(node.id, "this path loops back on itself. Use a For Loop or Every Interval to repeat things.");
            return;
        }
        execStack_.insert(node.id);
        beginStatement();
        const std::string tag = "  -- " + std::string(vsNodeKindName(node.kind)) + " #" + std::to_string(node.id);

        switch (node.kind) {
            case K::Branch: {
                const std::string condition = input(node, 1);
                const bool scoped = openScope();
                line("if " + condition + " then" + tag);
                push();
                chain(node, 2);
                pop();
                line("else");
                push();
                chain(node, 3);
                pop();
                line("end");
                closeScope(scoped);
                break;
            }
            case K::Sequence:
                chain(node, 1);
                chain(node, 2);
                chain(node, 3);
                break;
            case K::ForLoop: {
                const std::string first = input(node, 1);
                const std::string last = input(node, 2);
                const bool scoped = openScope();
                const std::string counter = "__i" + std::to_string(node.id);
                line("for " + counter + " = " + first + ", " + last + " do" + tag);
                push();
                line(valueName(node.id, 4) + " = " + counter);
                chain(node, 3);
                pop();
                line("end");
                closeScope(scoped);
                chain(node, 5);
                break;
            }
            default: {
                const std::string statement = simpleStatement(node);
                const bool scoped = openScope();
                line(statement + tag);
                closeScope(scoped);
                chain(node, firstExecOutput(node));
                break;
            }
        }
        execStack_.erase(node.id);
    }

    std::string simpleStatement(const VsNode& node) {
        switch (node.kind) {
            case K::Print: return "print(" + input(node, 1) + ")";
            case K::Wait: return "task.wait(math.max(0, " + input(node, 1) + "))";
            case K::SetPosition: return actionCall("setPosition", node, 2);
            case K::MoveBy: return actionCall("moveBy", node, 2);
            case K::SetRotation: return actionCall("setRotation", node, 2);
            case K::RotateBy: return actionCall("rotateBy", node, 2);
            case K::SetScale: return actionCall("setScale", node, 2);
            case K::SetColor: return actionCall("setColor", node, 2);
            case K::SetEmissive: return actionCall("setGlow", node, 3);
            case K::ApplyImpulse: return actionCall("push", node, 2);
            case K::SetVelocity: return actionCall("setVelocity", node, 2);
            case K::Destroy: return actionCall("destroy", node, 1);
            case K::SpawnBox: return valueName(node.id, 6) + " = " + actionCall("spawnBox", node, 4);
            case K::SetVariable: return "vars[" + quote(node.field) + "] = " + input(node, 1);
            default: return "-- (no code)";
        }
    }

    void collectReach(const VsNode& node, std::set<int>& reach, bool& yields) {
        if (!reach.insert(node.id).second) return;
        if (node.kind == K::Wait) yields = true;
        for (size_t i = 0; i < node.pinIds.size(); ++i) {
            const VsPin& pin = pinAt(node, i);
            if (!pin.isOutput || pin.type != VsType::Exec) continue;
            if (const VsNode* next = nextNode(node, static_cast<int>(i))) collectReach(*next, reach, yields);
        }
    }

    void emitEvent(const VsNode& event, std::vector<std::string>& deferred, std::set<int>& reachedAnywhere) {
        const int execOut = firstExecOutput(event);
        if (nextNode(event, execOut) == nullptr) return;

        Handler handler;
        handler_ = &handler;
        std::set<int> reach;
        bool yields = false;
        collectReach(event, reach, yields);
        reachedAnywhere.insert(reach.begin(), reach.end());

        std::vector<std::string> locals;
        for (int nodeId : reach) {
            const VsNode& node = *graph_.findNode(nodeId);
            for (size_t i = 0; i < node.pinIds.size(); ++i) {
                const VsPin& pin = pinAt(node, i);
                if (!pin.isOutput || pin.type == VsType::Exec) continue;
                handler.declared.insert(valueName(node.id, i));
                if (nodeId != event.id) locals.push_back(valueName(node.id, i));
            }
        }

        const std::string name = "__event" + std::to_string(event.id);
        const std::string id = std::to_string(event.id);
        std::string params;
        switch (event.kind) {
            case K::OnUpdate: params = valueName(event.id, 1); break;
            case K::OnTouched: params = "a, b"; break;
            case K::OnInteract: params = "target, interactor"; break;
            case K::OnPlayerJoined:
            case K::OnPlayerLeft: params = valueName(event.id, 1) + ", " + valueName(event.id, 2); break;
            default: break;
        }
        line("");
        line("-- " + std::string(vsNodeKindName(event.kind)) + " #" + id);
        line("local function " + name + "(" + params + ")");
        push();
        if (event.kind == K::OnTouched) {
            const std::string other = valueName(event.id, 1);
            line("local " + other);
            line("if a == self then " + other + " = b elseif b == self then " + other + " = a elseif self == nil then " +
                 other + " = b else return end");
        } else if (event.kind == K::OnInteract) {
            line("if self ~= nil and target ~= self then return end");
            line("local " + valueName(event.id, 1) + " = interactor");
        }
        if (!locals.empty()) {
            std::string declaration = "local ";
            for (size_t i = 0; i < locals.size(); ++i) declaration += (i > 0 ? ", " : "") + locals[i];
            line(declaration);
        }
        chain(event, execOut);
        pop();
        line("end");

        const std::string callback = yields ? "function(...) task.spawn(" + name + ", ...) end" : name;
        switch (event.kind) {
            case K::OnUpdate: line("events.onUpdate(" + callback + ")"); break;
            case K::OnTouched: line("events.onCollision(" + callback + ")"); break;
            case K::OnInteract: line("events.onInteract(" + callback + ")"); break;
            case K::OnPlayerJoined: line("events.onPlayerJoin(" + callback + ")"); break;
            case K::OnPlayerLeft: line("events.onPlayerLeave(" + callback + ")"); break;
            case K::OnStart: deferred.push_back(yields ? "task.spawn(" + name + ")" : name + "()"); break;
            case K::EveryInterval: {
                beginStatement();
                const std::string seconds = input(event, 0);
                std::string loop = "task.spawn(function()\n    while true do\n";
                for (const std::string& text : pre_) loop += "        " + text + "\n";
                loop += "        task.wait(math.max(0.01, " + seconds + "))\n        " + name + "()\n    end\nend)";
                pre_.clear();
                deferred.push_back(loop);
                break;
            }
            default: break;
        }
        handler_ = nullptr;
    }
};

} // namespace

VisualScriptCompileResult generateVisualScriptLuau(const VisualScriptGraph& graph) { return Generator(graph).run(); }

VisualScriptCompileResult compileVisualScript(const VisualScriptGraph& graph) {
    VisualScriptCompileResult result = generateVisualScriptLuau(graph);
    if (!result.success) return result;
    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 1;
    std::string bytecode = Luau::compile(result.luau, options);
    if (bytecode.empty() || bytecode[0] == 0) {
        result.success = false;
        result.error = "Luau rejected the generated code: " + (bytecode.empty() ? std::string("no output") : bytecode.substr(1));
        return result;
    }
    result.bytecode = std::move(bytecode);
    return result;
}

} // namespace engine::studio
