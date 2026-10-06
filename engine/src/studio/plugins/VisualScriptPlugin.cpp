#include "studio/plugins/VisualScriptPlugin.hpp"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>
#include <imnodes.h>
#include <misc/cpp/imgui_stdlib.h>

#include "core/Components.hpp"
#include "core/NativeFileDialog.hpp"
#include "studio/VisualScriptCompiler.hpp"

namespace engine::studio::plugins {

namespace {

constexpr const char* kCategories[] = {"Events", "Flow", "Actions", "Objects", "Variables", "Values", "Math", "Vectors", "Logic"};
constexpr double kLiveDelaySeconds = 0.35;
constexpr double kTooltipDelaySeconds = 0.6;

ImU32 pinColor(VsType type) {
    switch (type) {
        case VsType::Exec: return IM_COL32(235, 235, 240, 255);
        case VsType::Number: return IM_COL32(110, 200, 120, 255);
        case VsType::Bool: return IM_COL32(220, 90, 90, 255);
        case VsType::String: return IM_COL32(230, 110, 200, 255);
        case VsType::Vector: return IM_COL32(232, 196, 82, 255);
        case VsType::Entity: return IM_COL32(90, 170, 240, 255);
        case VsType::Any: return IM_COL32(160, 166, 178, 255);
    }
    return IM_COL32_WHITE;
}

ImU32 titleColor(const std::string& category, int shade) {
    ImVec4 base{0.30f, 0.33f, 0.40f, 1.0f};
    if (category == "Events") base = {0.55f, 0.20f, 0.20f, 1.0f};
    if (category == "Flow") base = {0.32f, 0.32f, 0.36f, 1.0f};
    if (category == "Actions") base = {0.18f, 0.34f, 0.56f, 1.0f};
    if (category == "Objects") base = {0.14f, 0.42f, 0.48f, 1.0f};
    if (category == "Variables") base = {0.42f, 0.28f, 0.52f, 1.0f};
    if (category == "Values") base = {0.40f, 0.32f, 0.18f, 1.0f};
    if (category == "Math") base = {0.20f, 0.40f, 0.26f, 1.0f};
    if (category == "Vectors") base = {0.44f, 0.38f, 0.14f, 1.0f};
    if (category == "Logic") base = {0.46f, 0.24f, 0.30f, 1.0f};
    const float lift = 0.08f * static_cast<float>(shade);
    return ImGui::ColorConvertFloat4ToU32({base.x + lift, base.y + lift, base.z + lift, 1.0f});
}

std::string semanticKey(const VisualScriptGraph& graph) {
    VisualScriptGraph copy = graph;
    for (const VsNode& node : graph.nodes()) {
        VsNode* mutableNode = copy.findNode(node.id);
        mutableNode->positionX = 0.0f;
        mutableNode->positionY = 0.0f;
    }
    return copy.serialize();
}

bool ownsScript(core::ECS& ecs, core::EntityId entity) {
    const auto* script = ecs.tryGetComponent<core::Script>(entity);
    return script == nullptr || script->source.empty() || isGeneratedVisualScriptSource(script->source);
}

} // namespace

VisualScriptPlugin::VisualScriptPlugin()
    : graph_(VisualScriptGraph::makeDefault()), editorContext_(ImNodes::EditorContextCreate()) {
    lastLiveGraph_ = semanticKey(graph_);
}

VisualScriptPlugin::~VisualScriptPlugin() { ImNodes::EditorContextFree(editorContext_); }

void VisualScriptPlugin::drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) {
    std::vector<core::EntityId> targets;
    auto consider = [&](core::EntityId entity) {
        if (!ecs.raw().valid(entity)) return;
        if (std::find(targets.begin(), targets.end(), entity) == targets.end()) targets.push_back(entity);
    };
    for (core::EntityId entity : selectedEntities) consider(entity);
    consider(selected);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(1100.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f, 420.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin(name());
    drawToolbar(ecs, targets);
    ImGui::Separator();
    drawNodeEditor();
    ImGui::End();

    handleLiveUpdate(ecs);
}

void VisualScriptPlugin::setStatus(std::string message, bool error) {
    statusMessage_ = std::move(message);
    statusIsError_ = error;
}

void VisualScriptPlugin::drawToolbar(core::ECS& ecs, const std::vector<core::EntityId>& targets) {
    if (ImGui::Button("+ Add Node")) {
        placeNextAtMouse_ = false;
        ImGui::OpenPopup("##vs_add_toolbar");
    }
    if (ImGui::BeginPopup("##vs_add_toolbar")) {
        drawAddNodeMenu();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        replaceGraph(VisualScriptGraph::makeDefault());
        linked_.clear();
        filePath_.clear();
        setStatus("New visual script", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        core::FileDialogOptions options;
        options.title = "Open Visual Script";
        options.extensions = {"*.kvs"};
        options.filterName = "Kronos visual scripts";
        if (!core::openFileDialogAsync(options, [this](const std::string& path) { openFile(path); })) {
            setStatus(core::fileDialogError(), true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save...")) {
        core::FileDialogOptions options;
        options.title = "Save Visual Script";
        options.extensions = {"*.kvs"};
        options.filterName = "Kronos visual scripts";
        options.save = true;
        options.defaultName = filePath_.empty() ? "script.kvs" : std::filesystem::path(filePath_).filename().string();
        if (!core::openFileDialogAsync(options, [this](const std::string& path) { saveFile(path); })) {
            setStatus(core::fileDialogError(), true);
        }
    }

    ImGui::SameLine(0.0f, 18.0f);
    if (ImGui::Button("Compile")) compile();
    ImGui::SameLine();
    ImGui::BeginDisabled(targets.empty());
    if (ImGui::Button("Apply to Selection")) applyTo(ecs, targets);
    ImGui::EndDisabled();
    if (targets.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Select one or more objects in the viewport first");
    }

    int withGraph = 0;
    core::EntityId firstWithGraph = core::EntityId{entt::null};
    for (core::EntityId entity : targets) {
        if (ecs.tryGetComponent<core::VisualScript>(entity) == nullptr) continue;
        if (withGraph++ == 0) firstWithGraph = entity;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(withGraph == 0);
    if (ImGui::Button("Load from Selection")) loadFrom(ecs, firstWithGraph);
    ImGui::SameLine();
    if (ImGui::Button("Remove from Selection")) {
        for (core::EntityId entity : targets) {
            if (ecs.tryGetComponent<core::VisualScript>(entity) == nullptr) continue;
            ecs.raw().remove<core::VisualScript>(entity);
            if (const auto* script = ecs.tryGetComponent<core::Script>(entity);
                script != nullptr && isGeneratedVisualScriptSource(script->source)) {
                ecs.raw().remove<core::Script>(entity);
            }
        }
        setStatus("Removed the visual script from " + std::to_string(withGraph) + " object(s)", false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Live", &liveUpdate_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Recompile while editing and update the objects this script was applied to or loaded from.\n"
                          "During Play the running script restarts with the change.");
    }

    ImGui::TextColored(statusIsError_ ? ImVec4(0.95f, 0.42f, 0.42f, 1.0f) : ImVec4(0.42f, 0.86f, 0.52f, 1.0f), "%s",
                       statusMessage_.empty() ? "Not compiled yet" : statusMessage_.c_str());
    if (!warnings_.empty()) {
        std::string joined = warnings_.front();
        for (size_t i = 1; i < warnings_.size(); ++i) joined += "  |  " + warnings_[i];
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "%s", joined.c_str());
    }

    if (targets.empty()) {
        ImGui::TextDisabled("No object selected. Select objects in the viewport to give them this script.");
    } else {
        ImGui::TextDisabled("%d selected object(s), %d with a visual script", static_cast<int>(targets.size()), withGraph);
        if (!linked_.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("  |  live-editing %d object(s)", static_cast<int>(linked_.size()));
        }
    }
    if (!filePath_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("  |  %s", filePath_.c_str());
    }

    ImGui::BeginDisabled(lastLuau_.empty());
    ImGui::Checkbox("Show generated Luau", &showLuau_);
    ImGui::EndDisabled();
    {
        if (showLuau_ && !lastLuau_.empty()) {
            ImGui::BeginChild("##generated_luau", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders);
            ImGui::TextUnformatted(lastLuau_.c_str());
            ImGui::EndChild();
        }
    }
}

void VisualScriptPlugin::drawAddNodeMenu() {
    for (const char* category : kCategories) {
        if (!ImGui::BeginMenu(category)) continue;
        for (int i = 0; i < kVsNodeKindCount; ++i) {
            const auto kind = static_cast<VsNodeKind>(i);
            if (std::string(vsNodeKindCategory(kind)) != category) continue;
            const bool clicked = ImGui::MenuItem(vsNodeKindName(kind));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", vsNodeKindDescription(kind));
            if (!clicked) continue;
            const float offset = static_cast<float>(graph_.nodes().size() % 8) * 28.0f;
            const int id = graph_.addNode(kind, 60.0f + offset, 60.0f + offset);
            if (placeNextAtMouse_) {
                ImNodes::EditorContextSet(editorContext_);
                ImNodes::SetNodeScreenSpacePos(id, ImVec2(nextNodeScreenX_, nextNodeScreenY_));
                placedNodes_.insert(id);
            }
        }
        ImGui::EndMenu();
    }
}

void VisualScriptPlugin::drawNodeEditor() {
    ImNodes::EditorContextSet(editorContext_);

    ImNodes::BeginNodeEditor();
    for (const VsNode& node : graph_.nodes()) drawNode(*graph_.findNode(node.id));
    for (const VsLink& link : graph_.links()) {
        const VsPin* from = graph_.findPin(link.outputPinId);
        const bool exec = from != nullptr && from->type == VsType::Exec;
        ImNodes::PushColorStyle(ImNodesCol_Link, exec ? IM_COL32(225, 225, 232, 255) : pinColor(from ? from->type : VsType::Any));
        ImNodes::Link(link.id, link.outputPinId, link.inputPinId);
        ImNodes::PopColorStyle();
    }
    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_TopRight);
    const bool openContextMenu = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                                 ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right);
    ImNodes::EndNodeEditor();

    int hoveredNode = 0;
    if (ImNodes::IsNodeHovered(&hoveredNode) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (hoveredNode != tooltipNode_) {
            tooltipNode_ = hoveredNode;
            tooltipSince_ = ImGui::GetTime();
        }
        const VsNode* node = graph_.findNode(hoveredNode);
        if (node != nullptr && ImGui::GetTime() - tooltipSince_ > kTooltipDelaySeconds) {
            ImGui::SetTooltip("%s", vsNodeKindDescription(node->kind));
        }
    } else {
        tooltipNode_ = 0;
    }

    if (openContextMenu) {
        placeNextAtMouse_ = true;
        nextNodeScreenX_ = ImGui::GetMousePos().x;
        nextNodeScreenY_ = ImGui::GetMousePos().y;
        ImGui::OpenPopup("##vs_add_canvas");
    }
    if (ImGui::BeginPopup("##vs_add_canvas")) {
        ImGui::TextDisabled("Add node");
        ImGui::Separator();
        drawAddNodeMenu();
        ImGui::EndPopup();
    }

    for (const VsNode& node : graph_.nodes()) {
        const ImVec2 position = ImNodes::GetNodeGridSpacePos(node.id);
        VsNode* mutableNode = graph_.findNode(node.id);
        mutableNode->positionX = position.x;
        mutableNode->positionY = position.y;
    }

    handleNewLinks();
    handleDeletion();
}

void VisualScriptPlugin::drawLiteral(const VsPin& pin, VsLiteral& value) {
    switch (pin.type) {
        case VsType::Number:
            ImGui::SetNextItemWidth(64.0f);
            ImGui::DragFloat("##v", &value.x, 0.05f, 0.0f, 0.0f, "%.2f");
            break;
        case VsType::Bool: ImGui::Checkbox("##v", &value.flag); break;
        case VsType::String:
        case VsType::Any:
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputText("##v", &value.text);
            break;
        case VsType::Vector:
            if (pin.isColor) {
                float color[3] = {value.x, value.y, value.z};
                if (ImGui::ColorEdit3("##v", color, ImGuiColorEditFlags_NoInputs)) {
                    value.x = color[0];
                    value.y = color[1];
                    value.z = color[2];
                }
            } else {
                float v[3] = {value.x, value.y, value.z};
                ImGui::SetNextItemWidth(156.0f);
                if (ImGui::DragFloat3("##v", v, 0.05f, 0.0f, 0.0f, "%.1f")) {
                    value.x = v[0];
                    value.y = v[1];
                    value.z = v[2];
                }
            }
            break;
        case VsType::Entity: ImGui::TextDisabled("Self"); break;
        case VsType::Exec: break;
    }
}

void VisualScriptPlugin::drawNode(VsNode& node) {
    if (placedNodes_.insert(node.id).second) {
        ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.positionX, node.positionY));
    }
    const bool hasError = node.id == errorNodeId_;
    const std::string category = vsNodeKindCategory(node.kind);
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, hasError ? IM_COL32(200, 40, 40, 255) : titleColor(category, 0));
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, hasError ? IM_COL32(220, 60, 60, 255) : titleColor(category, 1));
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, hasError ? IM_COL32(235, 80, 80, 255) : titleColor(category, 2));
    ImNodes::PushColorStyle(ImNodesCol_NodeOutline, hasError ? IM_COL32(255, 80, 80, 255) : IM_COL32(90, 90, 100, 255));
    ImNodes::BeginNode(node.id);

    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(vsNodeKindName(node.kind));
    ImNodes::EndNodeTitleBar();

    ImGui::PushID(node.id);
    if (vsNodeKindHasField(node.kind)) {
        ImGui::TextUnformatted("Name");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputText("##field", &node.field);
    }
    const bool isConstant = node.kind == VsNodeKind::Number || node.kind == VsNodeKind::Boolean ||
                            node.kind == VsNodeKind::Text || node.kind == VsNodeKind::VectorConstant;
    if (isConstant) {
        ImGui::PushID("constant");
        drawLiteral(*graph_.findPin(node.pinIds[0]), node.literals[0]);
        ImGui::PopID();
    }

    for (size_t i = 0; i < node.pinIds.size(); ++i) {
        const VsPin* pin = graph_.findPin(node.pinIds[i]);
        if (pin == nullptr) continue;
        const ImNodesPinShape shape = pin->type == VsType::Exec ? ImNodesPinShape_TriangleFilled : ImNodesPinShape_CircleFilled;
        ImNodes::PushColorStyle(ImNodesCol_Pin, pinColor(pin->type));
        ImNodes::PushColorStyle(ImNodesCol_PinHovered, IM_COL32_WHITE);
        ImGui::PushID(static_cast<int>(i));
        if (pin->isOutput) {
            ImNodes::BeginOutputAttribute(pin->id, shape);
            const char* label = pin->label.empty() ? " " : pin->label.c_str();
            const float width = ImGui::CalcTextSize(label).x;
            ImGui::Indent(std::max(0.0f, 120.0f - width));
            ImGui::TextUnformatted(label);
            ImNodes::EndOutputAttribute();
        } else {
            ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
            ImNodes::BeginInputAttribute(pin->id, shape);
            ImGui::TextUnformatted(pin->label.empty() ? " " : pin->label.c_str());
            if (pin->type != VsType::Exec && graph_.findLinkInto(pin->id) == nullptr) {
                ImGui::SameLine();
                drawLiteral(*pin, node.literals[i]);
            }
            ImNodes::EndInputAttribute();
            ImNodes::PopAttributeFlag();
        }
        ImGui::PopID();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }
    ImGui::PopID();

    ImNodes::EndNode();
    for (int i = 0; i < 4; ++i) ImNodes::PopColorStyle();
}

void VisualScriptPlugin::handleNewLinks() {
    int startedAtAttr = 0;
    int endedAtAttr = 0;
    if (!ImNodes::IsLinkCreated(&startedAtAttr, &endedAtAttr)) return;
    const VsPin* pinA = graph_.findPin(startedAtAttr);
    const VsPin* pinB = graph_.findPin(endedAtAttr);
    if (pinA == nullptr || pinB == nullptr) return;
    const int outputPin = pinA->isOutput ? startedAtAttr : endedAtAttr;
    const int inputPin = pinA->isOutput ? endedAtAttr : startedAtAttr;
    std::string error;
    if (!graph_.addLink(outputPin, inputPin, error)) setStatus("Link rejected: " + error, true);
}

void VisualScriptPlugin::handleDeletion() {
    int destroyedLinkId = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLinkId)) graph_.removeLink(destroyedLinkId);

    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || ImGui::GetIO().WantTextInput ||
        !ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        return;
    }
    const int selectedNodeCount = ImNodes::NumSelectedNodes();
    if (selectedNodeCount > 0) {
        std::vector<int> selectedNodes(static_cast<size_t>(selectedNodeCount));
        ImNodes::GetSelectedNodes(selectedNodes.data());
        for (int id : selectedNodes) {
            graph_.removeNode(id);
            placedNodes_.erase(id);
        }
        ImNodes::ClearNodeSelection();
    }
    const int selectedLinkCount = ImNodes::NumSelectedLinks();
    if (selectedLinkCount > 0) {
        std::vector<int> selectedLinks(static_cast<size_t>(selectedLinkCount));
        ImNodes::GetSelectedLinks(selectedLinks.data());
        for (int id : selectedLinks) graph_.removeLink(id);
        ImNodes::ClearLinkSelection();
    }
}

bool VisualScriptPlugin::compile() {
    const VisualScriptCompileResult result = compileVisualScript(graph_);
    warnings_ = result.warnings;
    errorNodeId_ = result.errorNodeId;
    if (!result.luau.empty()) lastLuau_ = result.luau;
    if (!result.success) {
        setStatus("Compile failed: " + result.error, true);
        return false;
    }
    lastBytecodeSize_ = result.bytecode.size();
    setStatus("Compiled to " + std::to_string(lastBytecodeSize_) + " bytes of Luau bytecode", false);
    return true;
}

int VisualScriptPlugin::applyTo(core::ECS& ecs, const std::vector<core::EntityId>& targets) {
    if (!compile()) return 0;
    const std::string text = graph_.serialize();
    std::vector<core::EntityId> applied;
    int skipped = 0;
    for (core::EntityId entity : targets) {
        if (!ownsScript(ecs, entity)) {
            ++skipped;
            continue;
        }
        ecs.raw().emplace_or_replace<core::VisualScript>(entity, text);
        auto& script = ecs.raw().get_or_emplace<core::Script>(entity);
        script.source = lastLuau_;
        script.autoRun = true;
        applied.push_back(entity);
    }
    linked_ = applied;
    lastLiveGraph_ = semanticKey(graph_);
    liveChangedAt_ = -1.0;
    if (skipped > 0) {
        setStatus("Applied to " + std::to_string(applied.size()) + " object(s); skipped " + std::to_string(skipped) +
                      " that already have a hand-written script (remove it in the Script Editor first)",
                  true);
    } else {
        setStatus(statusMessage_ + " -- applied to " + std::to_string(applied.size()) + " object(s)", false);
    }
    return static_cast<int>(applied.size());
}

void VisualScriptPlugin::loadFrom(core::ECS& ecs, core::EntityId entity) {
    const auto* visual = ecs.tryGetComponent<core::VisualScript>(entity);
    if (visual == nullptr) return;
    VisualScriptGraph graph;
    std::string error;
    if (!VisualScriptGraph::deserialize(visual->graph, graph, error)) {
        setStatus("Could not read the selected visual script: " + error, true);
        return;
    }
    const std::string source = visual->graph;
    replaceGraph(std::move(graph));
    linked_.clear();
    for (auto [other, otherScript] : ecs.raw().view<core::VisualScript>().each()) {
        if (otherScript.graph == source) linked_.push_back(other);
    }
    lastLiveGraph_ = semanticKey(graph_);
    compile();
    setStatus("Loaded the script (live-editing " + std::to_string(linked_.size()) + " object(s) that use it)", false);
}

void VisualScriptPlugin::replaceGraph(VisualScriptGraph graph) {
    graph_ = std::move(graph);
    placedNodes_.clear();
    errorNodeId_ = 0;
    warnings_.clear();
    ImNodes::EditorContextSet(editorContext_);
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    liveChangedAt_ = -1.0;
}

void VisualScriptPlugin::openFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setStatus("Could not open " + path, true);
        return;
    }
    std::ostringstream content;
    content << file.rdbuf();
    VisualScriptGraph graph;
    std::string error;
    if (!VisualScriptGraph::deserialize(content.str(), graph, error)) {
        setStatus("Could not read " + path + ": " + error, true);
        return;
    }
    replaceGraph(std::move(graph));
    linked_.clear();
    lastLiveGraph_ = semanticKey(graph_);
    filePath_ = path;
    setStatus("Opened " + std::filesystem::path(path).filename().string(), false);
}

void VisualScriptPlugin::saveFile(const std::string& path) {
    std::string target = path;
    if (std::filesystem::path(target).extension().empty()) target += ".kvs";
    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file || !(file << graph_.serialize())) {
        setStatus("Could not save " + target, true);
        return;
    }
    filePath_ = target;
    setStatus("Saved " + std::filesystem::path(target).filename().string(), false);
}

void VisualScriptPlugin::handleLiveUpdate(core::ECS& ecs) {
    linked_.erase(std::remove_if(linked_.begin(), linked_.end(),
                                 [&](core::EntityId entity) {
                                     return !ecs.raw().valid(entity) ||
                                            ecs.tryGetComponent<core::VisualScript>(entity) == nullptr;
                                 }),
                  linked_.end());
    const std::string key = semanticKey(graph_);
    if (key == lastLiveGraph_) {
        liveChangedAt_ = -1.0;
        return;
    }
    const double now = ImGui::GetTime();
    if (liveChangedAt_ < 0.0) liveChangedAt_ = now;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::GetIO().WantTextInput || now - liveChangedAt_ < kLiveDelaySeconds) {
        return;
    }
    lastLiveGraph_ = key;
    liveChangedAt_ = -1.0;
    if (!compile() || !liveUpdate_ || linked_.empty()) return;
    const std::string text = graph_.serialize();
    for (core::EntityId entity : linked_) {
        ecs.raw().emplace_or_replace<core::VisualScript>(entity, text);
        ecs.raw().get_or_emplace<core::Script>(entity).source = lastLuau_;
    }
    setStatus(statusMessage_ + " -- live updated " + std::to_string(linked_.size()) + " object(s)", false);
}

} // namespace engine::studio::plugins
