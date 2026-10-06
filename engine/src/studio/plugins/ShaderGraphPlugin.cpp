#include "studio/plugins/ShaderGraphPlugin.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>
#include <imnodes.h>

#include "core/Components.hpp"
#include "core/NativeFileDialog.hpp"
#include "studio/ShaderGraphCodegen.hpp"
#include "studio/SurfaceGraphMaterials.hpp"

namespace engine::studio::plugins {

namespace {

constexpr const char* kCategories[] = {"Inputs", "Constants", "Math", "Patterns", "Vectors", "Output"};
constexpr double kLiveDelaySeconds = 0.35;

ImU32 pinColor(ShaderDataType type) {
    switch (type) {
        case ShaderDataType::Float: return IM_COL32(160, 166, 178, 255);
        case ShaderDataType::Vec2: return IM_COL32(98, 200, 120, 255);
        case ShaderDataType::Vec3: return IM_COL32(232, 196, 82, 255);
        case ShaderDataType::Vec4: return IM_COL32(196, 112, 230, 255);
    }
    return IM_COL32_WHITE;
}

ImU32 titleColor(const char* category, int shade) {
    const std::string c = category;
    ImVec4 base{0.30f, 0.33f, 0.40f, 1.0f};
    if (c == "Inputs") base = {0.18f, 0.42f, 0.36f, 1.0f};
    if (c == "Constants") base = {0.40f, 0.32f, 0.18f, 1.0f};
    if (c == "Math") base = {0.22f, 0.32f, 0.52f, 1.0f};
    if (c == "Patterns") base = {0.45f, 0.22f, 0.42f, 1.0f};
    if (c == "Vectors") base = {0.30f, 0.30f, 0.34f, 1.0f};
    if (c == "Output") base = {0.55f, 0.22f, 0.22f, 1.0f};
    const float lift = 0.08f * static_cast<float>(shade);
    return ImGui::ColorConvertFloat4ToU32({base.x + lift, base.y + lift, base.z + lift, 1.0f});
}

// The graph minus node positions: dragging nodes around must not count
// as an edit that recompiles live materials.
std::string semanticKey(const ShaderGraph& graph) {
    ShaderGraph copy = graph;
    for (const ShaderNode& node : graph.nodes()) {
        ShaderNode* mutableNode = copy.findNode(node.id);
        mutableNode->positionX = 0.0f;
        mutableNode->positionY = 0.0f;
    }
    return copy.serialize();
}

} // namespace

ShaderGraphPlugin::ShaderGraphPlugin()
    : graph_(ShaderGraph::makeDefault()), editorContext_(ImNodes::EditorContextCreate()) {}

ShaderGraphPlugin::~ShaderGraphPlugin() { ImNodes::EditorContextFree(editorContext_); }

void ShaderGraphPlugin::setMaterials(SurfaceGraphMaterials* materials, const core::Renderer* renderer) {
    materials_ = materials;
    renderer_ = renderer;
}

void ShaderGraphPlugin::drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) {
    std::vector<core::EntityId> targets;
    auto consider = [&](core::EntityId entity) {
        if (!ecs.raw().valid(entity) || ecs.tryGetComponent<core::Renderable>(entity) == nullptr) return;
        if (std::find(targets.begin(), targets.end(), entity) == targets.end()) targets.push_back(entity);
    };
    for (core::EntityId entity : selectedEntities) consider(entity);
    consider(selected);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(1040.0f, 620.0f), ImGuiCond_FirstUseEver);
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

void ShaderGraphPlugin::setStatus(std::string message, bool error) {
    statusMessage_ = std::move(message);
    statusIsError_ = error;
}

void ShaderGraphPlugin::drawToolbar(core::ECS& ecs, const std::vector<core::EntityId>& targets) {
    if (ImGui::Button("+ Add Node")) {
        placeNextAtMouse_ = false;
        ImGui::OpenPopup("##add_node_toolbar");
    }
    if (ImGui::BeginPopup("##add_node_toolbar")) {
        drawAddNodeMenu();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        replaceGraph(ShaderGraph::makeDefault());
        linked_.clear();
        filePath_.clear();
        setStatus("New graph", false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        core::FileDialogOptions options;
        options.title = "Open Shader Graph";
        options.extensions = {"*.kshader"};
        options.filterName = "Kronos shader graphs";
        if (!core::openFileDialogAsync(options, [this](const std::string& path) { openFile(path); })) {
            setStatus(core::fileDialogError(), true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save...")) {
        core::FileDialogOptions options;
        options.title = "Save Shader Graph";
        options.extensions = {"*.kshader"};
        options.filterName = "Kronos shader graphs";
        options.save = true;
        options.defaultName =
            filePath_.empty() ? "material.kshader" : std::filesystem::path(filePath_).filename().string();
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
        if (ecs.tryGetComponent<core::SurfaceGraphMaterial>(entity) == nullptr) continue;
        if (withGraph++ == 0) firstWithGraph = entity;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(withGraph == 0);
    if (ImGui::Button("Load from Selection")) loadFrom(ecs, firstWithGraph);
    ImGui::SameLine();
    if (ImGui::Button("Remove from Selection")) {
        for (core::EntityId entity : targets) ecs.raw().remove<core::SurfaceGraphMaterial>(entity);
        setStatus("Removed the graph material from " + std::to_string(withGraph) + " object(s)", false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Live", &liveUpdate_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Recompile automatically while editing and update the objects this graph was applied to or loaded from");
    }

    if (!statusMessage_.empty()) {
        ImGui::TextColored(statusIsError_ ? ImVec4(0.95f, 0.42f, 0.42f, 1.0f) : ImVec4(0.42f, 0.86f, 0.52f, 1.0f), "%s",
                           statusMessage_.c_str());
    }

    if (targets.empty()) {
        ImGui::TextDisabled("No object selected. Select objects in the viewport to apply this material.");
    } else {
        ImGui::TextDisabled("%d selected object(s), %d using a graph material", static_cast<int>(targets.size()), withGraph);
        if (!linked_.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("  |  live-editing %d object(s)", static_cast<int>(linked_.size()));
        }
        if (materials_ != nullptr && withGraph > 0) {
            const auto* material = ecs.tryGetComponent<core::SurfaceGraphMaterial>(firstWithGraph);
            if (const std::string* error = materials_->errorFor(material->graph)) {
                ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.42f, 1.0f), "Selected material failed to compile: %s",
                                   error->c_str());
            }
        }
    }
    if (!filePath_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("  |  %s", filePath_.c_str());
    }

    if (!lastGeneratedGlsl_.empty()) {
        ImGui::Checkbox("Show generated GLSL", &showGeneratedGlsl_);
        if (showGeneratedGlsl_) {
            ImGui::BeginChild("##generated_glsl", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders);
            ImGui::TextUnformatted(lastGeneratedGlsl_.c_str());
            ImGui::EndChild();
        }
    }
}

void ShaderGraphPlugin::drawAddNodeMenu() {
    for (const char* category : kCategories) {
        if (!ImGui::BeginMenu(category)) continue;
        for (int i = 0; i < kShaderNodeKindCount; ++i) {
            const auto kind = static_cast<ShaderNodeKind>(i);
            if (std::string(shaderNodeKindCategory(kind)) != category) continue;
            if (!ImGui::MenuItem(shaderNodeKindName(kind))) continue;
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

void ShaderGraphPlugin::drawNodeEditor() {
    ImNodes::EditorContextSet(editorContext_);

    ImNodes::BeginNodeEditor();
    for (const ShaderNode& node : graph_.nodes()) drawNode(*graph_.findNode(node.id));
    for (const ShaderLink& link : graph_.links()) ImNodes::Link(link.id, link.outputPinId, link.inputPinId);
    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_TopRight);
    const bool openContextMenu = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                                 ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right);
    ImNodes::EndNodeEditor();

    if (openContextMenu) {
        placeNextAtMouse_ = true;
        nextNodeScreenX_ = ImGui::GetMousePos().x;
        nextNodeScreenY_ = ImGui::GetMousePos().y;
        ImGui::OpenPopup("##add_node_canvas");
    }
    if (ImGui::BeginPopup("##add_node_canvas")) {
        ImGui::TextDisabled("Add node");
        ImGui::Separator();
        drawAddNodeMenu();
        ImGui::EndPopup();
    }

    for (const ShaderNode& node : graph_.nodes()) {
        const ImVec2 position = ImNodes::GetNodeGridSpacePos(node.id);
        ShaderNode* mutableNode = graph_.findNode(node.id);
        mutableNode->positionX = position.x;
        mutableNode->positionY = position.y;
    }

    handleNewLinks();
    handleDeletion();
}

void ShaderGraphPlugin::drawNode(ShaderNode& node) {
    if (placedNodes_.insert(node.id).second) {
        ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.positionX, node.positionY));
    }
    const char* category = shaderNodeKindCategory(node.kind);
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleColor(category, 0));
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, titleColor(category, 1));
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, titleColor(category, 2));
    ImNodes::BeginNode(node.id);

    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(shaderNodeKindName(node.kind));
    ImNodes::EndNodeTitleBar();

    for (int pinId : node.pinIds) {
        const ShaderPin* pin = graph_.findPin(pinId);
        if (pin == nullptr) continue;
        ImNodes::PushColorStyle(ImNodesCol_Pin, pinColor(pin->type));
        ImNodes::PushColorStyle(ImNodesCol_PinHovered, IM_COL32_WHITE);
        if (pin->isOutput) {
            ImNodes::BeginOutputAttribute(pin->id);
            const float width = ImGui::CalcTextSize(pin->label.c_str()).x;
            ImGui::Indent(std::max(0.0f, 110.0f - width));
            ImGui::TextUnformatted(pin->label.c_str());
            ImNodes::EndOutputAttribute();
        } else {
            ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
            ImNodes::BeginInputAttribute(pin->id);
            ImGui::TextUnformatted(pin->label.c_str());
            ImNodes::EndInputAttribute();
            ImNodes::PopAttributeFlag();
        }
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    drawNodeContent(node);

    ImNodes::EndNode();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

void ShaderGraphPlugin::drawNodeContent(ShaderNode& node) {
    ImGui::PushID(node.id);
    ImGui::PushItemWidth(140.0f);
    switch (node.kind) {
        case ShaderNodeKind::ConstantFloat: ImGui::DragFloat("##value", &node.constantValue[0], 0.01f); break;
        case ShaderNodeKind::ConstantVec3: ImGui::DragFloat3("##value", node.constantValue, 0.01f); break;
        case ShaderNodeKind::ConstantVec4:
            ImGui::ColorEdit4("##value", node.constantValue, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            break;
        default: break;
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
}

void ShaderGraphPlugin::handleNewLinks() {
    int startedAtAttr = 0;
    int endedAtAttr = 0;
    if (!ImNodes::IsLinkCreated(&startedAtAttr, &endedAtAttr)) return;

    const ShaderPin* pinA = graph_.findPin(startedAtAttr);
    const ShaderPin* pinB = graph_.findPin(endedAtAttr);
    if (pinA == nullptr || pinB == nullptr) return;

    const int outputPin = pinA->isOutput ? startedAtAttr : endedAtAttr;
    const int inputPin = pinA->isOutput ? endedAtAttr : startedAtAttr;

    // Dropping onto an already-connected input replaces its link.
    if (const ShaderLink* existing = graph_.findLinkInto(inputPin)) graph_.removeLink(existing->id);

    std::string error;
    if (!graph_.addLink(outputPin, inputPin, error)) setStatus("Link rejected: " + error, true);
}

void ShaderGraphPlugin::handleDeletion() {
    int destroyedLinkId = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLinkId)) graph_.removeLink(destroyedLinkId);

    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || !ImGui::IsKeyPressed(ImGuiKey_Delete, false)) return;

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

bool ShaderGraphPlugin::compile() {
    if (materials_ != nullptr && renderer_ != nullptr) {
        SurfaceGraphMaterials::Compiled compiled = materials_->prepare(graph_.serialize(), *renderer_);
        lastGeneratedGlsl_ = compiled.glsl;
        if (!compiled.success) {
            setStatus("Compile failed: " + compiled.error, true);
            return false;
        }
        char message[160];
        std::snprintf(message, sizeof(message), "Compiled in %.0f ms (%zu SPIR-V words)", compiled.milliseconds,
                      compiled.spirv.size());
        setStatus(message, false);
        return true;
    }

    ShaderGraphCodegenResult codegen = generateFragmentShaderGlsl(graph_);
    if (!codegen.success) {
        setStatus("Codegen failed: " + codegen.errorMessage, true);
        lastGeneratedGlsl_.clear();
        return false;
    }
    lastGeneratedGlsl_ = codegen.glsl;
    RuntimeShaderCompiler::Result compiled =
        compiler_.compile(codegen.glsl, RuntimeShaderCompiler::ShaderStage::Fragment, "shader_graph_preview.frag");
    if (!compiled.success) {
        setStatus("Shader compile failed: " + compiled.errorMessage, true);
        return false;
    }
    setStatus("Compiled (" + std::to_string(compiled.spirv.size()) + " SPIR-V words)", false);
    return true;
}

void ShaderGraphPlugin::applyTo(core::ECS& ecs, const std::vector<core::EntityId>& targets) {
    if (!compile()) return;
    const std::string text = graph_.serialize();
    for (core::EntityId entity : targets) ecs.raw().emplace_or_replace<core::SurfaceGraphMaterial>(entity, text);
    linked_ = targets;
    lastLiveGraph_ = semanticKey(graph_);
    liveChangedAt_ = -1.0;
    setStatus(statusMessage_ + " -- applied to " + std::to_string(targets.size()) + " object(s)", false);
}

void ShaderGraphPlugin::loadFrom(core::ECS& ecs, core::EntityId entity) {
    const auto* material = ecs.tryGetComponent<core::SurfaceGraphMaterial>(entity);
    if (material == nullptr) return;
    ShaderGraph graph;
    std::string error;
    if (!ShaderGraph::deserialize(material->graph, graph, error)) {
        setStatus("Could not read the selected material: " + error, true);
        return;
    }
    const std::string source = material->graph;
    replaceGraph(std::move(graph));
    linked_.clear();
    for (auto [other, otherMaterial] : ecs.raw().view<core::SurfaceGraphMaterial>().each()) {
        if (otherMaterial.graph == source) linked_.push_back(other);
    }
    lastLiveGraph_ = semanticKey(graph_);
    setStatus("Loaded the graph (live-editing " + std::to_string(linked_.size()) + " object(s) that use it)", false);
}

void ShaderGraphPlugin::replaceGraph(ShaderGraph graph) {
    graph_ = std::move(graph);
    placedNodes_.clear();
    ImNodes::EditorContextSet(editorContext_);
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    liveChangedAt_ = -1.0;
}

void ShaderGraphPlugin::openFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setStatus("Could not open " + path, true);
        return;
    }
    std::ostringstream content;
    content << file.rdbuf();
    ShaderGraph graph;
    std::string error;
    if (!ShaderGraph::deserialize(content.str(), graph, error)) {
        setStatus("Could not read " + path + ": " + error, true);
        return;
    }
    replaceGraph(std::move(graph));
    linked_.clear();
    lastLiveGraph_ = semanticKey(graph_);
    filePath_ = path;
    setStatus("Opened " + std::filesystem::path(path).filename().string(), false);
}

void ShaderGraphPlugin::saveFile(const std::string& path) {
    std::string target = path;
    if (std::filesystem::path(target).extension().empty()) target += ".kshader";
    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file || !(file << graph_.serialize())) {
        setStatus("Could not save " + target, true);
        return;
    }
    filePath_ = target;
    setStatus("Saved " + std::filesystem::path(target).filename().string(), false);
}

void ShaderGraphPlugin::handleLiveUpdate(core::ECS& ecs) {
    linked_.erase(std::remove_if(linked_.begin(), linked_.end(),
                                 [&](core::EntityId entity) {
                                     return !ecs.raw().valid(entity) ||
                                            ecs.tryGetComponent<core::SurfaceGraphMaterial>(entity) == nullptr;
                                 }),
                  linked_.end());
    if (!liveUpdate_ || materials_ == nullptr || renderer_ == nullptr || linked_.empty()) return;

    const std::string key = semanticKey(graph_);
    if (key == lastLiveGraph_) {
        liveChangedAt_ = -1.0;
        return;
    }
    const double now = ImGui::GetTime();
    if (liveChangedAt_ < 0.0) liveChangedAt_ = now;
    // Wait for a pause in editing (e.g. dragging a value) before compiling.
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || now - liveChangedAt_ < kLiveDelaySeconds) return;

    lastLiveGraph_ = key;
    liveChangedAt_ = -1.0;
    if (!compile()) return;
    const std::string text = graph_.serialize();
    for (core::EntityId entity : linked_) ecs.raw().emplace_or_replace<core::SurfaceGraphMaterial>(entity, text);
    setStatus(statusMessage_ + " -- live updated " + std::to_string(linked_.size()) + " object(s)", false);
}

} // namespace engine::studio::plugins
