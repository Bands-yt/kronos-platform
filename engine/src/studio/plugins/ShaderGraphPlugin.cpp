#include "studio/plugins/ShaderGraphPlugin.hpp"

#include <cstdio>

#include <imgui.h>
#include <imnodes.h>

#include "studio/ShaderGraphCodegen.hpp"

namespace engine::studio::plugins {

namespace {
// Real, addable node kinds in the toolbar's own combo -- every real
// ShaderNodeKind (ShaderGraph.hpp), in a fixed, sensible authoring
// order (inputs, then constants, then math, then sampling, then the
// one real sink last).
constexpr ShaderNodeKind kAddableKinds[] = {
    ShaderNodeKind::InputWorldPosition, ShaderNodeKind::InputWorldNormal, ShaderNodeKind::InputUV,
    ShaderNodeKind::ConstantFloat,      ShaderNodeKind::ConstantVec3,     ShaderNodeKind::ConstantVec4,
    ShaderNodeKind::AddFloat,           ShaderNodeKind::AddVec4,          ShaderNodeKind::MultiplyFloat,
    ShaderNodeKind::MultiplyVec4,       ShaderNodeKind::TextureSample,    ShaderNodeKind::PbrOutput,
};
constexpr int kAddableKindCount = static_cast<int>(sizeof(kAddableKinds) / sizeof(ShaderNodeKind));
} // namespace

ShaderGraphPlugin::ShaderGraphPlugin() : editorContext_(ImNodes::EditorContextCreate()) {}

ShaderGraphPlugin::~ShaderGraphPlugin() { ImNodes::EditorContextFree(editorContext_); }

void ShaderGraphPlugin::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawToolbar();
    ImGui::Separator();
    drawNodeEditor();
    ImGui::End();
}

void ShaderGraphPlugin::drawToolbar() {
    const char* kindNames[kAddableKindCount];
    for (int i = 0; i < kAddableKindCount; ++i) kindNames[i] = shaderNodeKindName(kAddableKinds[i]);

    ImGui::SetNextItemWidth(200.0f);
    ImGui::Combo("##add_node_kind", &addNodeKindIndex_, kindNames, kAddableKindCount);
    ImGui::SameLine();
    if (ImGui::Button("Add Node")) {
        // Real, deterministic cascade so repeated adds don't stack
        // exactly on top of each other -- not meaningful placement,
        // just enough spread that a newly-added node is immediately
        // visible and draggable to wherever the creator actually wants it.
        float offset = static_cast<float>(graph_.nodes().size() % 10) * 24.0f;
        graph_.addNode(kAddableKinds[addNodeKindIndex_], 40.0f + offset, 40.0f + offset);
    }
    ImGui::SameLine();
    if (ImGui::Button("Compile")) compile();

    if (!statusMessage_.empty()) {
        ImGui::TextColored(statusIsError_ ? ImVec4(0.9f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "%s",
                            statusMessage_.c_str());
    }
    if (!lastGeneratedGlsl_.empty()) {
        ImGui::SameLine();
        ImGui::Checkbox("Show Generated GLSL", &showGeneratedGlsl_);
    }
    if (showGeneratedGlsl_ && !lastGeneratedGlsl_.empty()) {
        ImGui::BeginChild("##generated_glsl", ImVec2(0.0f, 140.0f), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(lastGeneratedGlsl_.c_str());
        ImGui::EndChild();
    }
    ImGui::TextDisabled(
        "Delete: remove selected node(s)/link(s). Drag from a pin to another pin of the same type to connect.");
}

void ShaderGraphPlugin::drawNodeEditor() {
    ImNodes::EditorContextSet(editorContext_);
    ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);

    ImNodes::BeginNodeEditor();
    for (const ShaderNode& node : graph_.nodes()) {
        // graph_.nodes() is const here (drawNode() below needs mutable
        // access for constant-value drag widgets) -- findNode() gets a
        // real mutable pointer to the same node by id, not a copy.
        drawNode(*graph_.findNode(node.id));
    }
    for (const ShaderLink& link : graph_.links()) {
        ImNodes::Link(link.id, link.outputPinId, link.inputPinId);
    }
    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_TopRight);
    ImNodes::EndNodeEditor();

    ImNodes::PopAttributeFlag();

    handleNewLinks();
    handleDeletion();
}

void ShaderGraphPlugin::drawNode(const ShaderNode& constNode) {
    ShaderNode& node = *graph_.findNode(constNode.id); // see drawNodeEditor()'s own comment on why this is safe/intended
    ImNodes::BeginNode(node.id);

    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(shaderNodeKindName(node.kind));
    ImNodes::EndNodeTitleBar();

    for (int pinId : node.pinIds) {
        const ShaderPin* pin = graph_.findPin(pinId);
        if (pin == nullptr) continue;
        if (pin->isOutput) {
            ImNodes::BeginOutputAttribute(pin->id);
            // Real right-alignment for output pins -- ImNodes renders
            // output pins on the node's right edge, so left-aligned
            // text would sit oddly far from its own pin circle.
            ImGui::Indent(60.0f);
            ImGui::TextUnformatted(pin->label.c_str());
            ImNodes::EndOutputAttribute();
        } else {
            ImNodes::BeginInputAttribute(pin->id);
            ImGui::TextUnformatted(pin->label.c_str());
            ImNodes::EndInputAttribute();
        }
    }

    drawNodeContent(node);

    ImNodes::EndNode();
}

void ShaderGraphPlugin::drawNodeContent(ShaderNode& node) {
    ImGui::PushItemWidth(120.0f);
    switch (node.kind) {
        case ShaderNodeKind::ConstantFloat: ImGui::DragFloat("##value", &node.constantValue[0], 0.01f); break;
        case ShaderNodeKind::ConstantVec3: ImGui::DragFloat3("##value", node.constantValue, 0.01f); break;
        case ShaderNodeKind::ConstantVec4:
            // ColorEdit4, not DragFloat4 -- Vec4 constants in this
            // node set are overwhelmingly used to feed PbrOutput's own
            // baseColor input (see ShaderGraph.hpp's own "Vector4 /
            // Color" node name), so a real color picker is the more
            // useful real authoring widget here.
            ImGui::ColorEdit4("##value", node.constantValue);
            break;
        default: break; // every other kind has no authored content, just its pins
    }
    ImGui::PopItemWidth();
}

void ShaderGraphPlugin::handleNewLinks() {
    int startedAtAttr = 0;
    int endedAtAttr = 0;
    if (!ImNodes::IsLinkCreated(&startedAtAttr, &endedAtAttr)) return;

    const ShaderPin* pinA = graph_.findPin(startedAtAttr);
    const ShaderPin* pinB = graph_.findPin(endedAtAttr);
    if (pinA == nullptr || pinB == nullptr) return;

    // ImNodes::IsLinkCreated() reports the drag's real start/end
    // attribute ids, which can be either direction (a creator can drag
    // from an input pin to an output pin just as validly as the
    // reverse) -- resolve which one is the real output before calling
    // ShaderGraph::addLink(), which requires (outputPinId, inputPinId)
    // in that fixed order regardless of drag direction.
    int outputPin = pinA->isOutput ? startedAtAttr : endedAtAttr;
    int inputPin = pinA->isOutput ? endedAtAttr : startedAtAttr;

    std::string error;
    if (!graph_.addLink(outputPin, inputPin, error)) {
        statusMessage_ = "Link rejected: " + error;
        statusIsError_ = true;
    }
}

void ShaderGraphPlugin::handleDeletion() {
    int destroyedLinkId = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLinkId)) {
        graph_.removeLink(destroyedLinkId);
    }

    // Gated on this panel's own window focus -- Delete shouldn't remove
    // a selected node while some unrelated Studio panel has keyboard
    // focus, same "only act while this window genuinely has focus"
    // convention ScriptEditorPanel's own Ctrl+S handling already uses.
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || !ImGui::IsKeyPressed(ImGuiKey_Delete, false)) return;

    int selectedNodeCount = ImNodes::NumSelectedNodes();
    if (selectedNodeCount > 0) {
        std::vector<int> selectedNodes(static_cast<size_t>(selectedNodeCount));
        ImNodes::GetSelectedNodes(selectedNodes.data());
        for (int id : selectedNodes) graph_.removeNode(id);
        ImNodes::ClearNodeSelection();
    }

    int selectedLinkCount = ImNodes::NumSelectedLinks();
    if (selectedLinkCount > 0) {
        std::vector<int> selectedLinks(static_cast<size_t>(selectedLinkCount));
        ImNodes::GetSelectedLinks(selectedLinks.data());
        for (int id : selectedLinks) graph_.removeLink(id);
        ImNodes::ClearLinkSelection();
    }
}

void ShaderGraphPlugin::compile() {
    ShaderGraphCodegenResult codegen = generateFragmentShaderGlsl(graph_);
    if (!codegen.success) {
        statusMessage_ = "Codegen failed: " + codegen.errorMessage;
        statusIsError_ = true;
        lastGeneratedGlsl_.clear();
        return;
    }
    lastGeneratedGlsl_ = codegen.glsl;

    RuntimeShaderCompiler::Result compiled =
        compiler_.compile(codegen.glsl, RuntimeShaderCompiler::ShaderStage::Fragment, "shader_graph_preview.frag");
    if (!compiled.success) {
        statusMessage_ = "Shader compile failed: " + compiled.errorMessage;
        statusIsError_ = true;
        return;
    }

    statusMessage_ = "Compiled successfully -- " + std::to_string(compiled.spirv.size()) + " SPIR-V words";
    if (compiled.warningCount > 0) statusMessage_ += " (" + std::to_string(compiled.warningCount) + " warnings)";
    statusIsError_ = false;
}

} // namespace engine::studio::plugins
