#include "studio/plugins/ModelingModePlugin.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

#include <imgui.h>

#include <imgui_stdlib.h>

#include "core/KMeshFile.hpp"
#include "core/MeshUvPicking.hpp"
#include "core/ObjLoader.hpp"
#include "core/UvTools.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

ModelingModePlugin::ModelingModePlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                        core::MeshLibrary& meshLibrary)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary) {}

void ModelingModePlugin::reuploadMesh(core::EditableMeshComponent& component, core::Renderable& renderable) {
    // Kronos ("3D DCC Modeling Suite" -- real non-destructive modifier
    // stack): every real call site below (extrude/bevel/CSG/sub-object
    // translate/the script-driven update() sweep) already funnels through
    // here, so evaluating component.modifierStack on top of component.mesh
    // right at upload time is the one change that makes every one of them
    // respect the stack -- component.mesh itself stays exactly what every
    // topology operator/sub-object edit directly mutates.
    core::EditableMesh evaluated = component.modifierStack.evaluate(component.mesh);
    core::Mesh newMesh;
    (void)newMesh.uploadFromHost(allocator_, device_, cmdPool_, queue_, evaluated.vertices(), evaluated.indices());
    meshLibrary_->replaceMesh(renderable.meshHandle, std::move(newMesh), allocator_);
}

void ModelingModePlugin::update(float /*dt*/, core::ECS& ecs, core::EntityId /*selected*/,
                                 const std::vector<core::EntityId>& /*selectedEntities*/) {
    auto view = ecs.view<core::EditableMeshComponent, core::Renderable>();
    for (auto entity : view) {
        auto& component = view.get<core::EditableMeshComponent>(entity);
        uint64_t& lastApplied = lastAppliedEditVersion_[entity];
        if (component.editVersion == lastApplied) continue;
        reuploadMesh(component, view.get<core::Renderable>(entity));
        lastApplied = component.editVersion;
    }
}

void ModelingModePlugin::applyCsg(core::EditableMeshComponent& component, core::Renderable& renderable,
                                   core::CsgOperation op) {
    core::EditableMesh box = core::EditableMesh::createBox(csgBoxHalfExtents_, csgBoxOffset_);
    core::EditableMesh result = core::booleanOp(component.mesh, box, op);
    if (result.faceCount() == 0) {
        csgStatus_ = "CSG produced an empty result -- left the current mesh untouched. Try a smaller/overlapping "
                      "box offset, or see CsgMesh.hpp's own scope comment (exact coincident planes are a known "
                      "edge case).";
        return;
    }
    component.mesh = std::move(result);
    // Same "indices may have shifted" precedent Auto Unwrap's own button
    // already follows -- a CSG result has entirely new, unrelated
    // topology, so the old face/edge selection is stale by construction.
    component.selectedFace = 0;
    component.selectedEdge = {0, 0};
    reuploadMesh(component, renderable);
    csgStatus_ = "CSG applied.";
}

bool ModelingModePlugin::pickSubObject(const core::EditableMeshComponent& component, glm::vec3 localOrigin,
                                        glm::vec3 localDirection, float maxDistance) const {
    core::MeshUvPickResult hit = core::pickTriangleUv(component.mesh, localOrigin, localDirection, maxDistance);
    if (!hit.hit) return false;

    // const_cast is safe and deliberate here: `component` is only const in
    // this method's own signature (so callers reading a selection can pass
    // a const& too), but every real caller (ViewportPanel) already holds a
    // real, non-const EditableMeshComponent& from the ECS -- the selection
    // fields themselves aren't part of what makes this method logically
    // "const" (it reads the mesh, not the selection), just conveniently
    // declared alongside it.
    auto& mutableComponent = const_cast<core::EditableMeshComponent&>(component);
    const std::array<uint32_t, 3> faceVerts = component.mesh.faceVertexIndices(hit.faceIndex);
    const std::vector<core::Vertex>& vertices = component.mesh.vertices();

    switch (subObjectMode_) {
        case core::EditableMesh::SelectionMode::Face:
            mutableComponent.selectedFace = hit.faceIndex;
            break;
        case core::EditableMesh::SelectionMode::Vertex: {
            uint32_t closest = faceVerts[0];
            float closestDistSq = std::numeric_limits<float>::max();
            for (uint32_t idx : faceVerts) {
                glm::vec3 diff = vertices[idx].position - hit.point;
                float distSq = glm::dot(diff, diff);
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    closest = idx;
                }
            }
            mutableComponent.selectedVertex = closest;
            break;
        }
        case core::EditableMesh::SelectionMode::Edge: {
            std::pair<uint32_t, uint32_t> bestEdge{faceVerts[0], faceVerts[1]};
            float bestDistSq = std::numeric_limits<float>::max();
            for (int i = 0; i < 3; ++i) {
                uint32_t a = faceVerts[static_cast<size_t>(i)];
                uint32_t b = faceVerts[static_cast<size_t>((i + 1) % 3)];
                glm::vec3 pa = vertices[a].position;
                glm::vec3 ab = vertices[b].position - pa;
                float abLenSq = glm::max(glm::dot(ab, ab), 1e-8f);
                float t = glm::clamp(glm::dot(hit.point - pa, ab) / abLenSq, 0.0f, 1.0f);
                glm::vec3 closestPoint = pa + ab * t;
                glm::vec3 diff = closestPoint - hit.point;
                float distSq = glm::dot(diff, diff);
                if (distSq < bestDistSq) {
                    bestDistSq = distSq;
                    bestEdge = {std::min(a, b), std::max(a, b)};
                }
            }
            mutableComponent.selectedEdge = bestEdge;
            break;
        }
    }
    return true;
}

glm::vec3 ModelingModePlugin::subObjectAnchorLocal(const core::EditableMeshComponent& component) const {
    const core::EditableMesh& mesh = component.mesh;
    switch (subObjectMode_) {
        case core::EditableMesh::SelectionMode::Vertex:
            return component.selectedVertex < mesh.vertexCount() ? mesh.vertices()[component.selectedVertex].position
                                                                    : glm::vec3(0.0f);
        case core::EditableMesh::SelectionMode::Edge: {
            uint32_t v0 = component.selectedEdge.first, v1 = component.selectedEdge.second;
            if (v0 >= mesh.vertexCount() || v1 >= mesh.vertexCount()) return glm::vec3(0.0f);
            return (mesh.vertices()[v0].position + mesh.vertices()[v1].position) * 0.5f;
        }
        case core::EditableMesh::SelectionMode::Face:
        default:
            return component.selectedFace < mesh.faceCount() ? mesh.faceCentroid(component.selectedFace) : glm::vec3(0.0f);
    }
}

void ModelingModePlugin::translateSubObjectSelection(core::EditableMeshComponent& component,
                                                       core::Renderable& renderable, glm::vec3 localDelta) {
    core::EditableMesh& mesh = component.mesh;
    bool changed = false;
    switch (subObjectMode_) {
        case core::EditableMesh::SelectionMode::Vertex:
            if (component.selectedVertex < mesh.vertexCount()) {
                mesh.setVertexPosition(static_cast<uint32_t>(component.selectedVertex),
                                        mesh.vertices()[component.selectedVertex].position + localDelta);
                changed = true;
            }
            break;
        case core::EditableMesh::SelectionMode::Edge: {
            uint32_t v0 = component.selectedEdge.first, v1 = component.selectedEdge.second;
            if (v0 < mesh.vertexCount() && v1 < mesh.vertexCount()) {
                mesh.setVertexPosition(v0, mesh.vertices()[v0].position + localDelta);
                mesh.setVertexPosition(v1, mesh.vertices()[v1].position + localDelta);
                changed = true;
            }
            break;
        }
        case core::EditableMesh::SelectionMode::Face:
        default:
            if (component.selectedFace < mesh.faceCount()) {
                for (uint32_t idx : mesh.faceVertexIndices(component.selectedFace)) {
                    mesh.setVertexPosition(idx, mesh.vertices()[idx].position + localDelta);
                }
                changed = true;
            }
            break;
    }
    if (changed) reuploadMesh(component, renderable);
}

void ModelingModePlugin::drawModifierStackSection(core::EditableMeshComponent& component, core::Renderable& renderable) {
    helpMarker("Applied on top of the base mesh above, in order, at upload time only -- editing the base mesh above, "
               "or reordering/disabling/removing a modifier here, updates the result immediately without ever "
               "touching the base mesh itself.");

    std::vector<core::Modifier>& modifiers = component.modifierStack.modifiers();
    int removeIndex = -1;
    int swapWithNext = -1;
    bool changed = false;

    static const char* kTypeNames[] = {"Mirror", "Array", "Solidify", "Subdivision", "Boolean"};
    static const char* kAxisNames[] = {"X", "Y", "Z"};
    static const char* kOpNames[] = {"Union", "Subtract", "Intersect"};

    for (size_t i = 0; i < modifiers.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        core::Modifier& mod = modifiers[i];

        changed |= ImGui::Checkbox("##enabled", &mod.enabled);
        ImGui::SameLine();
        int typeIndex = static_cast<int>(mod.type);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("##type", &typeIndex, kTypeNames, IM_ARRAYSIZE(kTypeNames))) {
            mod.type = static_cast<core::ModifierType>(typeIndex);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && i > 0) swapWithNext = static_cast<int>(i) - 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("Down") && i + 1 < modifiers.size()) swapWithNext = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("X##removemod")) removeIndex = static_cast<int>(i);

        ImGui::Indent();
        switch (mod.type) {
            case core::ModifierType::Mirror: {
                ImGui::SetNextItemWidth(70.0f);
                changed |= ImGui::Combo("Axis##mirror", &mod.mirror.axis, kAxisNames, IM_ARRAYSIZE(kAxisNames));
                ImGui::SameLine();
                changed |= ImGui::Checkbox("Merge at center##mirror", &mod.mirror.mergeAtCenter);
                if (mod.mirror.mergeAtCenter) {
                    ImGui::SetNextItemWidth(120.0f);
                    changed |= ImGui::DragFloat("Merge threshold##mirror", &mod.mirror.mergeThreshold, 0.001f, 0.0001f,
                                                 1.0f, "%.4f");
                }
                break;
            }
            case core::ModifierType::Array: {
                ImGui::SetNextItemWidth(80.0f);
                changed |= ImGui::DragInt("Count##array", &mod.array.count, 1.0f, 1, 32);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                changed |= ImGui::DragFloat3("Offset##array", &mod.array.offset.x, 0.05f);
                break;
            }
            case core::ModifierType::Solidify: {
                ImGui::SetNextItemWidth(120.0f);
                changed |= ImGui::DragFloat("Thickness##solidify", &mod.solidify.thickness, 0.01f, -2.0f, 2.0f);
                ImGui::SameLine();
                helpMarker("Walls only real open boundary edges (shared by exactly 1 face, by real vertex index) -- "
                           "see SolidifyModifierParams's own comment on why an unwelded EditableMesh::createBox() "
                           "gets walled on every outer edge, same as bevelEdge()'s own real per-index scope.");
                break;
            }
            case core::ModifierType::Subdivision: {
                ImGui::SetNextItemWidth(80.0f);
                changed |= ImGui::DragInt("Levels##subdivision", &mod.subdivision.levels, 1.0f, 1, 4);
                ImGui::SameLine();
                helpMarker("Real flat (linear) 1-to-4 refinement per level -- not a true Catmull-Clark limit "
                           "surface (no smoothing pass over vertex positions).");
                break;
            }
            case core::ModifierType::Boolean: {
                int opIndex = static_cast<int>(mod.boolean.operation);
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::Combo("Operation##boolean", &opIndex, kOpNames, IM_ARRAYSIZE(kOpNames))) {
                    mod.boolean.operation = static_cast<core::CsgOperation>(opIndex);
                    changed = true;
                }
                ImGui::SetNextItemWidth(200.0f);
                changed |= ImGui::DragFloat3("Box Offset##boolean", &mod.boolean.boxOffset.x, 0.05f);
                ImGui::SetNextItemWidth(200.0f);
                changed |= ImGui::DragFloat3("Box Half-Extents##boolean", &mod.boolean.boxHalfExtents.x, 0.05f, 0.01f, 10.0f);
                break;
            }
        }
        ImGui::Unindent();
        ImGui::Separator();
        ImGui::PopID();
    }

    if (removeIndex >= 0) {
        component.modifierStack.removeModifier(static_cast<size_t>(removeIndex));
        changed = true;
    }
    if (swapWithNext >= 0) {
        component.modifierStack.swapModifiers(static_cast<size_t>(swapWithNext), static_cast<size_t>(swapWithNext) + 1);
        changed = true;
    }

    auto addModifier = [&](core::ModifierType type) {
        core::Modifier m;
        m.type = type;
        component.modifierStack.addModifier(m);
        changed = true;
    };
    if (ImGui::Button("Add Mirror##addmod")) addModifier(core::ModifierType::Mirror);
    ImGui::SameLine();
    if (ImGui::Button("Add Array##addmod")) addModifier(core::ModifierType::Array);
    ImGui::SameLine();
    if (ImGui::Button("Add Solidify##addmod")) addModifier(core::ModifierType::Solidify);
    ImGui::SameLine();
    if (ImGui::Button("Add Subdivision##addmod")) addModifier(core::ModifierType::Subdivision);
    ImGui::SameLine();
    if (ImGui::Button("Add Boolean##addmod")) addModifier(core::ModifierType::Boolean);

    if (changed) reuploadMesh(component, renderable);
}

void ModelingModePlugin::drawPanel(core::ECS& ecs, core::EntityId selected,
                                    const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin(name());
    drawPluginHeader("Modeling Mode");

    if (selected == core::kNullEntity) {
        ImGui::TextDisabled("Select an entity in the Viewport or Explorer to edit its mesh.");
        drawPluginFooter();
        ImGui::End();
        return;
    }

    auto* renderable = ecs.tryGetComponent<core::Renderable>(selected);
    auto* editable = ecs.tryGetComponent<core::EditableMeshComponent>(selected);

    if (editable == nullptr) {
        auto* meshSource = ecs.tryGetComponent<core::MeshSource>(selected);
        if (renderable == nullptr || meshSource == nullptr || meshSource->kind != core::MeshSourceKind::Box) {
            ImGui::TextWrapped(
                "This entity isn't editable yet -- Modeling Mode can only start from a Box-sourced mesh "
                "(a Block Builder Cube, or a box prop), the one real shape it knows how to seed identically. "
                "Sphere/Cylinder/Wedge/imported meshes aren't supported yet.");
            drawPluginFooter();
            ImGui::End();
            return;
        }
        ImGui::TextWrapped("This entity's current box can become a real, editable mesh.");
        if (ImGui::Button("Start Editing", ImVec2(160.0f, 0.0f))) {
            auto& component = ecs.addComponent<core::EditableMeshComponent>(selected);
            component.mesh = core::EditableMesh::createBox(meshSource->params);
            reuploadMesh(component, *renderable);
        }
        drawPluginFooter();
        ImGui::End();
        return;
    }

    // Real editing UI -- editable != nullptr, renderable must exist too
    // (EditableMeshComponent is only ever added alongside one above).
    core::EditableMesh& mesh = editable->mesh;
    ImGui::Text("%zu vertices, %zu faces", mesh.vertexCount(), mesh.faceCount());

    // Kronos ("3D DCC Modeling Suite" -- real sub-object raycast picking):
    // governs what a real viewport Ctrl+Click resolves to (see
    // ViewportPanel::drawSubObjectEditing()) -- the 3 lists below (and
    // their own click-to-select) work regardless of this mode, same as
    // before this pass.
    ImGui::Text("Viewport pick mode:");
    ImGui::SameLine();
    int mode = static_cast<int>(subObjectMode_);
    if (ImGui::RadioButton("Vertex", mode == static_cast<int>(core::EditableMesh::SelectionMode::Vertex))) {
        subObjectMode_ = core::EditableMesh::SelectionMode::Vertex;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Edge", mode == static_cast<int>(core::EditableMesh::SelectionMode::Edge))) {
        subObjectMode_ = core::EditableMesh::SelectionMode::Edge;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Face", mode == static_cast<int>(core::EditableMesh::SelectionMode::Face))) {
        subObjectMode_ = core::EditableMesh::SelectionMode::Face;
    }
    ImGui::SameLine();
    helpMarker("Ctrl+Click a real vertex/edge/face on this entity in the Viewport to select it there instead of "
               "from these lists -- both write the same real selectedVertex/selectedEdge/selectedFace this panel "
               "already uses, and a translate gizmo appears on the current selection either way.");
    ImGui::Spacing();

    ImGui::SeparatorText("Vertices");
    ImGui::BeginChild("VertexList", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders);
    for (size_t v = 0; v < mesh.vertexCount(); ++v) {
        glm::vec3 p = mesh.vertices()[v].position;
        char label[64];
        std::snprintf(label, sizeof(label), "Vertex %zu  (%.2f, %.2f, %.2f)##vertex", v, p.x, p.y, p.z);
        if (ImGui::Selectable(label, editable->selectedVertex == v)) editable->selectedVertex = v;
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::SeparatorText("Faces");
    ImGui::BeginChild("FaceList", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders);
    for (size_t f = 0; f < mesh.faceCount(); ++f) {
        glm::vec3 c = mesh.faceCentroid(f);
        char label[64];
        std::snprintf(label, sizeof(label), "Face %zu  (%.2f, %.2f, %.2f)##face", f, c.x, c.y, c.z);
        if (ImGui::Selectable(label, editable->selectedFace == f)) editable->selectedFace = f;
    }
    ImGui::EndChild();

    ImGui::DragFloat("Extrude Distance", &extrudeDistance_, 0.05f, -10.0f, 10.0f);
    if (ImGui::Button("Extrude##face") && editable->selectedFace < mesh.faceCount()) {
        if (mesh.extrudeFace(editable->selectedFace, extrudeDistance_)) reuploadMesh(*editable, *renderable);
    }
    ImGui::SameLine();
    if (ImGui::Button("Subdivide##face") && editable->selectedFace < mesh.faceCount()) {
        if (mesh.subdivideFace(editable->selectedFace)) reuploadMesh(*editable, *renderable);
    }
    ImGui::DragFloat("Inset Amount", &insetAmount_, 0.02f, 0.0f, 1.0f);
    ImGui::SameLine();
    if (ImGui::Button("Inset##face") && editable->selectedFace < mesh.faceCount()) {
        if (mesh.insetFace(editable->selectedFace, insetAmount_)) reuploadMesh(*editable, *renderable);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Edges");
    auto edges = mesh.allEdges();
    ImGui::BeginChild("EdgeList", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders);
    for (const auto& edge : edges) {
        char label[32];
        std::snprintf(label, sizeof(label), "Edge (%u, %u)##edge", edge.first, edge.second);
        bool isSelected = editable->selectedEdge == edge;
        if (ImGui::Selectable(label, isSelected)) editable->selectedEdge = edge;
    }
    ImGui::EndChild();
    ImGui::DragFloat("Bevel Amount", &bevelAmount_, 0.02f, 0.0f, 1.0f);
    if (ImGui::Button("Bevel##edge")) {
        if (mesh.bevelEdge(editable->selectedEdge.first, editable->selectedEdge.second, bevelAmount_)) {
            reuploadMesh(*editable, *renderable);
        }
    }
    ImGui::SameLine();
    helpMarker("Only works on a real interior edge (shared by exactly 2 faces) -- a real, honest no-op otherwise.");

    ImGui::Spacing();
    ImGui::SeparatorText("Whole Mesh");
    ImGui::DragFloat("Merge Threshold", &mergeThreshold_, 0.001f, 0.0001f, 1.0f, "%.4f");
    if (ImGui::Button("Merge Close Vertices")) {
        size_t merged = mesh.mergeVertices(mergeThreshold_);
        if (merged > 0) reuploadMesh(*editable, *renderable);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Boolean (CSG)");
    ImGui::DragFloat3("Box Offset##csg", &csgBoxOffset_.x, 0.05f);
    ImGui::DragFloat3("Box Half-Extents##csg", &csgBoxHalfExtents_.x, 0.05f, 0.01f, 10.0f);
    if (ImGui::Button("Union##csg")) applyCsg(*editable, *renderable, core::CsgOperation::Union);
    ImGui::SameLine();
    if (ImGui::Button("Subtract##csg")) applyCsg(*editable, *renderable, core::CsgOperation::Subtract);
    ImGui::SameLine();
    if (ImGui::Button("Intersect##csg")) applyCsg(*editable, *renderable, core::CsgOperation::Intersect);
    ImGui::SameLine();
    helpMarker("Combines the current mesh with a real box at the given offset/half-extents. Only correct for "
               "closed, manifold meshes with consistent winding -- see core::CsgMesh.hpp's own scope comment.");
    if (!csgStatus_.empty()) ImGui::TextWrapped("%s", csgStatus_.c_str());

    ImGui::Spacing();
    ImGui::SeparatorText("Modifiers (non-destructive)");
    drawModifierStackSection(*editable, *renderable);

    ImGui::Spacing();
    ImGui::SeparatorText("UV");
    if (ImGui::Button("Planar (X)##uv")) {
        core::applyPlanarProjection(mesh, core::ProjectionAxis::X);
        reuploadMesh(*editable, *renderable);
    }
    ImGui::SameLine();
    if (ImGui::Button("Planar (Y)##uv")) {
        core::applyPlanarProjection(mesh, core::ProjectionAxis::Y);
        reuploadMesh(*editable, *renderable);
    }
    ImGui::SameLine();
    if (ImGui::Button("Planar (Z)##uv")) {
        core::applyPlanarProjection(mesh, core::ProjectionAxis::Z);
        reuploadMesh(*editable, *renderable);
    }
    if (ImGui::Button("Cube Projection##uv")) {
        core::applyCubeProjection(mesh);
        reuploadMesh(*editable, *renderable);
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto Unwrap##uv")) {
        // Real vertex split, see applyAutoUnwrap()'s own comment --
        // reassigns the component's mesh outright rather than mutating
        // in place, then reselect a valid face/edge since indices may
        // have shifted.
        editable->mesh = core::applyAutoUnwrap(editable->mesh);
        editable->selectedFace = 0;
        editable->selectedEdge = {0, 0};
        reuploadMesh(*editable, *renderable);
    }
    ImGui::SameLine();
    helpMarker("Auto Unwrap is a real, simplified per-triangle unwrap (shape-preserving per face, more seams than "
               "a full conformal unwrapper) -- see UvTools.hpp's own comment.");

    ImGui::Spacing();
    ImGui::SeparatorText("Export / Import");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##ExportPath", &exportPathBuffer_);
    ImGui::SameLine();
    helpMarker("Written as \"<this>.obj\" and \"<this>.kmesh\" -- no extension needed here.");
    if (ImGui::Button("Export .obj")) {
        bool ok = core::saveObj(exportPathBuffer_ + ".obj", mesh.vertices(), mesh.indices());
        exportImportStatus_ = ok ? ("Exported " + exportPathBuffer_ + ".obj") : "Export .obj failed";
    }
    ImGui::SameLine();
    if (ImGui::Button("Export .kmesh")) {
        bool ok = core::saveKMesh(exportPathBuffer_ + ".kmesh", mesh.vertices(), mesh.indices());
        exportImportStatus_ = ok ? ("Exported " + exportPathBuffer_ + ".kmesh") : "Export .kmesh failed";
    }

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##ImportPath", &importPathBuffer_);
    ImGui::SameLine();
    if (ImGui::Button("Import .kmesh")) {
        core::KMeshLoadResult loaded = core::loadKMesh(importPathBuffer_);
        if (loaded.succeeded) {
            editable->mesh = core::EditableMesh::fromVertexData(std::move(loaded.vertices), std::move(loaded.indices));
            editable->selectedFace = 0;
            editable->selectedEdge = {0, 0};
            reuploadMesh(*editable, *renderable);
            exportImportStatus_ = "Imported " + importPathBuffer_;
        } else {
            exportImportStatus_ = "Import failed: " + loaded.error;
        }
    }
    if (!exportImportStatus_.empty()) ImGui::TextWrapped("%s", exportImportStatus_.c_str());

    drawPluginFooter("Every edit re-uploads to the GPU immediately -- what you see is the real, current mesh.");
    ImGui::End();
}

} // namespace engine::studio::plugins
