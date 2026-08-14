#include "studio/plugins/ModelingModePlugin.hpp"

#include <cstdio>

#include <imgui.h>

#include <imgui_stdlib.h>

#include "core/KMeshFile.hpp"
#include "core/ObjLoader.hpp"
#include "core/UvTools.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

ModelingModePlugin::ModelingModePlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                        core::MeshLibrary& meshLibrary)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary) {}

void ModelingModePlugin::reuploadMesh(core::EditableMeshComponent& component, core::Renderable& renderable) {
    core::Mesh newMesh;
    (void)newMesh.uploadFromHost(allocator_, device_, cmdPool_, queue_, component.mesh.vertices(),
                                  component.mesh.indices());
    meshLibrary_->replaceMesh(renderable.meshHandle, std::move(newMesh), allocator_);
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
