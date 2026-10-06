#include "studio/panels/ViewportPanel.hpp"

#include "studio/plugins/ModelImporterPlugin.hpp"
#include "studio/plugins/ModelingModePlugin.hpp"
#include "studio/plugins/MovieModePlugin.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <ImGuizmo.h>

#include "core/Components.hpp"
#include "core/EditableMeshComponent.hpp"
#include "core/Hierarchy.hpp"
#include "core/Mesh.hpp"
#include "core/Renderer.hpp"
#include "core/ScenePicking.hpp"
#include "core/Terrain.hpp"
#include "core/UIWidgets.hpp"
#include "core/WorldProp.hpp"
#include "studio/CreatorToolsSpawning.hpp"
#include "studio/StudioIcons.hpp"
#include "studio/plugins/PhysicsPreviewPlugin.hpp"
#include "studio/plugins/TerrainEditorPlugin.hpp"

namespace engine::studio::panels {

namespace {

constexpr float kViewCubeSize = 96.0f;

ImVec2 viewCubeOrigin(ImVec2 imageOrigin, ImVec2 imageSize) {
    return ImVec2(imageOrigin.x + imageSize.x - kViewCubeSize - 8.0f, imageOrigin.y + 8.0f);
}

// The world matrix of `entity`'s *parent* (identity if it has none) --
// every caller below needs this to convert an ImGuizmo-edited world
// matrix back into the entity's own local Transform, since
// core::hierarchy::setParent() (e.g. via ExplorerPanel's drag-to-parent,
// now reachable in 3D Maker too) makes a parented entity's Transform mean
// local space, not world space -- see Components.hpp's Hierarchy comment.
glm::mat4 parentWorldMatrix(core::ECS& ecs, core::EntityId entity) {
    auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity);
    core::EntityId parent = hierarchy ? hierarchy->parent : core::kNullEntity;
    if (parent == core::kNullEntity) return glm::mat4(1.0f);
    return core::hierarchy::computeWorldMatrix(ecs, parent);
}
} // namespace

void ViewportPanel::updateFreeFly(float deltaTime) {
    bool hovered = ImGui::IsWindowHovered();

    // Scroll to dolly, independent of right-click-drag -- Blender's own
    // default scroll-wheel behavior (up = closer, down = further).
    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            camera_.position += camera_.forward() * (wheel * 1.5f);
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        dragging_ = true;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        dragging_ = false;
    }

    if (dragging_) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        camera_.yawDegrees += delta.x * 0.15f;
        camera_.pitchDegrees -= delta.y * 0.15f;
        camera_.pitchDegrees = std::clamp(camera_.pitchDegrees, -89.0f, 89.0f);

        glm::vec3 forward = camera_.forward();
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
        float speed = 5.0f * deltaTime;

        if (ImGui::IsKeyDown(ImGuiKey_W)) camera_.position += forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) camera_.position -= forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) camera_.position += right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) camera_.position -= right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) camera_.position.y += speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) camera_.position.y -= speed;
    }
}

void ViewportPanel::drawGizmo(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& allSelected,
                               ImVec2 imageOrigin, ImVec2 imageSize) {
    auto* transform = ecs.tryGetComponent<core::Transform>(selected);
    if (!transform || imageSize.x <= 0.0f || imageSize.y <= 0.0f) return;
    glm::vec3 positionBeforeDrag = transform->position; // for the group-move delta below

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y);

    glm::mat4 view = renderCamera_.viewMatrix();
    glm::mat4 proj = renderCamera_.projectionMatrix(imageSize.x / imageSize.y);
    // Real world matrix, not transform->matrix() alone -- Renderer.cpp
    // renders this entity at hierarchy::computeWorldMatrix()'s result
    // (push.model), so a parented entity's gizmo must be built and
    // manipulated in that same world space or its handles land wherever
    // the entity's *local* transform alone would put it -- offset from
    // the actual rendered mesh by exactly its parent's world transform.
    // Byte-identical to transform->matrix() for the common unparented
    // case (parentWorld below is then identity).
    glm::mat4 parentWorld = parentWorldMatrix(ecs, selected);
    glm::mat4 model = parentWorld * transform->matrix();

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    switch (gizmoOperation_) {
        case GizmoOperation::Translate: op = ImGuizmo::TRANSLATE; break;
        case GizmoOperation::Rotate: op = ImGuizmo::ROTATE; break;
        case GizmoOperation::Scale: op = ImGuizmo::SCALE; break;
    }
    // Scale is always LOCAL regardless of the space toggle -- a world-
    // space scale gizmo on a rotated object would shear it (scaling along
    // world axes instead of the object's own), which is never what a user
    // wants; every real DCC/editor hard-codes this same exception.
    ImGuizmo::MODE mode =
        (gizmoOperation_ != GizmoOperation::Scale && gizmoSpace_ == GizmoSpace::World) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

    // Real grid/angle snapping, not a cosmetic checkbox -- ImGuizmo applies
    // this internally during the drag itself (not a post-hoc round), so
    // dragged values land exactly on-grid rather than needing a second
    // manual snap pass.
    float snapValues[3] = {translateSnap_, translateSnap_, translateSnap_};
    bool snapEnabledForThisOp = gridSnapEnabled_;
    if (gizmoOperation_ == GizmoOperation::Rotate) {
        snapValues[0] = snapValues[1] = snapValues[2] = rotateSnapDegrees_;
        snapEnabledForThisOp = angleSnapEnabled_;
    } else if (gizmoOperation_ == GizmoOperation::Scale) {
        snapValues[0] = snapValues[1] = snapValues[2] = scaleSnap_;
        snapEnabledForThisOp = scaleSnapEnabled_;
    }

    // ImGuizmo's own worldToPos() applies an OpenGL-convention "1.0f - t"
    // Y flip internally, expecting an unflipped (world-up -> ndc.y=+1)
    // projection -- handing it renderCamera_'s Vulkan-flipped projection
    // (see Camera.hpp) double-flips it, mirroring the gizmo vertically.
    // Un-flip just for this call; `proj` itself stays Vulkan-correct for
    // every other use in this function.
    glm::mat4 gizmoProj = proj;
    gizmoProj[1][1] *= -1.0f;
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(gizmoProj), op, mode, glm::value_ptr(model), nullptr,
                          snapEnabledForThisOp ? snapValues : nullptr);

    if (ImGuizmo::IsUsing()) {
        // ImGuizmo::Manipulate() edited `model` (the WORLD matrix) in
        // place -- convert back to the entity's own LOCAL space before
        // decomposing into transform->position/rotation/scale (that
        // struct's fields are always local-space once parented, see
        // Components.hpp's Hierarchy comment). Identity parentWorld (the
        // unparented case) makes this a no-op inverse, so decomposedLocal
        // == model exactly as before this fix.
        glm::mat4 decomposedLocal = glm::inverse(parentWorld) * model;
        glm::vec3 translation, scale, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        if (glm::decompose(decomposedLocal, scale, rotation, translation, skew, perspective)) {
            // Real gizmo-stability fix: glm::decompose() can return a
            // near-zero or negative scale component while a scale drag is
            // passing through/near the origin (the gizmo doesn't clamp
            // its own handle position), which feeds a degenerate model
            // matrix into next frame's normal-matrix inverse
            // (scene.vert's transpose(inverse(mat3(model)))) -- NaN
            // normals, an entity that silently vanishes or flips inside
            // out. Clamping here, not in the shader, keeps the fix at the
            // one place a human actually typed the input.
            constexpr float kMinScale = 0.001f;
            scale = glm::max(scale, glm::vec3(kMinScale));

            // NOTE: glm::decompose() has a long-documented quirk where the
            // returned quaternion is the conjugate of what the matrix
            // actually represents -- conjugating back here is the known,
            // widely-used workaround, not a guess. If a future glm
            // release fixes decompose() upstream, this line is what needs
            // removing.
            transform->position = translation;
            transform->rotation = glm::conjugate(rotation);
            transform->scale = scale;

            // Group move: Translate only (see this method's own doc
            // comment on why Rotate/Scale don't apply here -- rotating or
            // scaling several objects together around a shared pivot is a
            // meaningfully different, more complex feature than shifting
            // them by a common offset). Every other selected entity keeps
            // its own relative offset from the primary selection, not
            // snapped to one shared point.
            if (gizmoOperation_ == GizmoOperation::Translate && allSelected.size() > 1) {
                glm::vec3 delta = transform->position - positionBeforeDrag;
                if (delta != glm::vec3(0.0f)) {
                    for (core::EntityId other : allSelected) {
                        if (other == selected) continue;
                        if (auto* otherTransform = ecs.tryGetComponent<core::Transform>(other)) {
                            otherTransform->position += delta;
                        }
                    }
                }
            }
        }
    }
}

void ViewportPanel::drawSelectionHighlight(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                            const std::vector<core::EntityId>& selectedEntities, ImVec2 imageOrigin,
                                            ImVec2 imageSize) {
    if (selectedEntities.empty() || imageSize.x <= 0.0f || imageSize.y <= 0.0f) return;

    glm::mat4 viewProj = renderCamera_.projectionMatrix(imageSize.x / imageSize.y) * renderCamera_.viewMatrix();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    // A real, deliberately different color from every gizmo axis color
    // (red/green/blue) and from ImGuizmo's own yellow hover highlight --
    // this box means "selected", not "this is the axis you're
    // dragging".
    constexpr ImU32 kHighlightColor = IM_COL32(255, 165, 0, 220); // orange
    constexpr float kLineThickness = 1.5f;

    // Real projection, same inverse-mapping convention
    // computeMouseRay() already establishes (Vulkan clip-space
    // GLM_FORCE_DEPTH_ZERO_TO_ONE, screen-Y-down vs. NDC-Y-up) --
    // returns false (and leaves outScreen untouched) for a point behind
    // the camera (clip.w <= 0), the one real case naive perspective
    // division would otherwise wrap around into a garbage on-screen
    // position.
    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& outScreen) -> bool {
        glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0001f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        outScreen.x = imageOrigin.x + ((ndc.x + 1.0f) * 0.5f) * imageSize.x;
        // renderCamera_.projectionMatrix() already applies Vulkan's Y flip
        // (see Camera.hpp), so ndc.y == -1 is the TOP of the viewport here,
        // not the bottom -- an extra "1.0f - ndc.y" on top of that flip was
        // mirroring every projected point about the horizontal centerline.
        outScreen.y = imageOrigin.y + ((ndc.y + 1.0f) * 0.5f) * imageSize.y;
        return true;
    };

    for (core::EntityId entity : selectedEntities) {
        auto* transform = ecs.tryGetComponent<core::Transform>(entity);
        auto* renderable = ecs.tryGetComponent<core::Renderable>(entity);
        if (transform == nullptr || renderable == nullptr) continue;
        const core::Mesh* mesh = meshLibrary.get(renderable->meshHandle);
        if (mesh == nullptr) continue;

        glm::vec3 lo = mesh->localBoundsMin();
        glm::vec3 hi = mesh->localBoundsMax();
        // Real world matrix -- see parentWorldMatrix()'s own comment above
        // (drawGizmo() uses the identical fix for the identical reason);
        // a parented entity's highlight box must match where it actually
        // renders, not just its own local transform.
        glm::mat4 model = core::hierarchy::computeWorldMatrix(ecs, entity);

        // The 8 real corners of the local AABB, each transformed to
        // world space by this entity's own real model matrix -- a
        // rotated/scaled entity's highlight box rotates/scales with it,
        // not an axis-aligned-in-world approximation.
        glm::vec3 worldCorners[8];
        int i = 0;
        for (float x : {lo.x, hi.x}) {
            for (float y : {lo.y, hi.y}) {
                for (float z : {lo.z, hi.z}) {
                    worldCorners[i++] = glm::vec3(model * glm::vec4(x, y, z, 1.0f));
                }
            }
        }
        // Corner index bit layout matches the loop above: bit2=x, bit1=y, bit0=z.
        ImVec2 screenCorners[8];
        bool valid[8];
        for (int c = 0; c < 8; ++c) valid[c] = projectToScreen(worldCorners[c], screenCorners[c]);

        auto drawEdge = [&](int a, int b) {
            if (valid[a] && valid[b]) drawList->AddLine(screenCorners[a], screenCorners[b], kHighlightColor, kLineThickness);
        };
        // 4 bottom edges (y=lo, corners 0,1,4,5), 4 top edges (y=hi,
        // corners 2,3,6,7), 4 verticals connecting them.
        drawEdge(0, 1); drawEdge(1, 5); drawEdge(5, 4); drawEdge(4, 0);
        drawEdge(2, 3); drawEdge(3, 7); drawEdge(7, 6); drawEdge(6, 2);
        drawEdge(0, 2); drawEdge(1, 3); drawEdge(4, 6); drawEdge(5, 7);
    }
}

void ViewportPanel::computeMouseRay(ImVec2 mousePos, ImVec2 imageOrigin, ImVec2 imageSize, glm::vec3& outOrigin,
                                     glm::vec3& outDirection) const {
    float ndcX = ((mousePos.x - imageOrigin.x) / imageSize.x) * 2.0f - 1.0f;
    // renderCamera_.projectionMatrix()'s Vulkan Y flip means ndc.y == -1 is
    // the TOP of the viewport (see Camera.hpp / projectToScreen()'s own
    // comment above) -- this must be the exact inverse of that mapping, not
    // the OpenGL-convention "1.0f - t" flip it used to have.
    float ndcY = ((mousePos.y - imageOrigin.y) / imageSize.y) * 2.0f - 1.0f;

    glm::mat4 proj = renderCamera_.projectionMatrix(imageSize.x / imageSize.y);
    glm::mat4 view = renderCamera_.viewMatrix();
    glm::mat4 invViewProj = glm::inverse(proj * view);

    // NDC z=0 is the near plane, z=1 the far plane -- this project's
    // Vulkan clip-space convention (GLM_FORCE_DEPTH_ZERO_TO_ONE, see
    // Renderer.cpp/CMakeLists.txt's comment on why that define exists),
    // not GLM's own OpenGL-style default. Using the wrong convention here
    // would still *compile* and still produce *a* ray, just not one that
    // actually passes through the cursor -- exactly the kind of bug that
    // motivated finding and fixing that define codebase-wide earlier.
    glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    nearPoint /= nearPoint.w;
    glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    farPoint /= farPoint.w;

    outOrigin = glm::vec3(nearPoint);
    outDirection = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));
}

void ViewportPanel::dropSelectedToGround(core::ECS& ecs, core::MeshLibrary& meshLibrary, core::EntityId selected) {
    auto* transform = ecs.tryGetComponent<core::Transform>(selected);
    if (transform == nullptr) return;

    constexpr float kMaxDropDistance = 1000.0f;
    core::ScenePickResult result = core::pickEntity(ecs, meshLibrary, transform->position, glm::vec3(0.0f, -1.0f, 0.0f),
                                                      kMaxDropDistance, selected);
    if (!result.hit) return;

    // Real, stated scope simplification: only Transform::scale.y is
    // applied to the mesh's own local-space bottom extent, not the full
    // rotation -- correct for the overwhelmingly common "unrotated or
    // Y-axis-only-rotated prop" case this shortcut targets, and a
    // meaningfully harder problem (rotating the local AABB itself) for
    // an arbitrarily-tilted entity, which real DCC "drop to floor" tools
    // usually don't attempt either without a full mesh-vs-mesh contact
    // solve.
    float bottomOffset = 0.0f;
    if (auto* renderable = ecs.tryGetComponent<core::Renderable>(selected)) {
        if (const core::Mesh* mesh = meshLibrary.get(renderable->meshHandle)) {
            bottomOffset = mesh->localBoundsMin().y * transform->scale.y;
        }
    }
    transform->position.y = result.point.y - bottomOffset;
}

void ViewportPanel::handleSelection(core::ECS& ecs, core::MeshLibrary& meshLibrary, ExplorerPanel& explorer,
                                     ImVec2 imageOrigin, ImVec2 imageSize) {
    constexpr float kDragThresholdPixels = 4.0f; // below this, treat mouse-down+up as a click, not a drag
    constexpr float kMaxPickDistance = 1000.0f;

    ImGuiIO& io = ImGui::GetIO();
    bool hovered = ImGui::IsWindowHovered();
    bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

    // The exact click that brings an unfocused viewport to focus is the
    // one IsMouseClicked() can miss: a click-to-focus window manager (or
    // ImGui's own docking focus handling) can consume that press as a
    // pure focus/z-order event before ImGui's edge-detected "clicked"
    // state is ever set for this window, so on that specific frame
    // io.MouseDown[Left] can already read true with no preceding
    // "clicked" edge for IsMouseClicked() to report -- and by the same
    // logic, the later release has no matching press edge either, so the
    // IsMouseReleased() fallback below can't catch this case (it only
    // catches the window-hover-order-lag case, a different failure mode).
    // Detecting the focus-acquired transition directly and treating an
    // already-down button as "the click just started" closes that gap:
    // real focus-changed edge, not a fragile timing/frame-count guess.
    bool focusedNow = ImGui::IsWindowFocused();
    bool justFocusedWithMouseDown = focusedNow && !viewportWasFocused_ && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    viewportWasFocused_ = focusedNow;

    if (hovered && !overGizmo && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || justFocusedWithMouseDown)) {
        dragSelectActive_ = true;
        dragSelectStart_ = io.MousePos;
    }

    if (dragSelectActive_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImVec2 delta(io.MousePos.x - dragSelectStart_.x, io.MousePos.y - dragSelectStart_.y);
        float distSq = delta.x * delta.x + delta.y * delta.y;
        if (distSq > kDragThresholdPixels * kDragThresholdPixels) {
            // Past the threshold -- this is a drag, draw the live
            // selection rectangle so the user can see what they're about
            // to select before releasing.
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 rectMin(std::min(dragSelectStart_.x, io.MousePos.x), std::min(dragSelectStart_.y, io.MousePos.y));
            ImVec2 rectMax(std::max(dragSelectStart_.x, io.MousePos.x), std::max(dragSelectStart_.y, io.MousePos.y));
            drawList->AddRectFilled(rectMin, rectMax, IM_COL32(90, 150, 255, 40));
            drawList->AddRect(rectMin, rectMax, IM_COL32(120, 180, 255, 200));
        }
    }

    // Raycast-pick whatever's closest under the cursor (core::pickEntity(),
    // a physics-independent ray-vs-mesh-bounds query -- see its header for
    // why this isn't Physics::raycast()).
    auto pickAtCursor = [&]() {
        glm::vec3 rayOrigin, rayDir;
        computeMouseRay(io.MousePos, imageOrigin, imageSize, rayOrigin, rayDir);
        core::ScenePickResult result = core::pickEntity(ecs, meshLibrary, rayOrigin, rayDir, kMaxPickDistance);

        if (result.hit) {
            if (io.KeyCtrl) {
                explorer.toggleSelection(result.entity);
            } else {
                explorer.setSelected(result.entity);
            }
        } else if (!io.KeyCtrl) {
            // Clicked empty space -- clears selection, matching every
            // other editor's viewport. Ctrl-clicking empty space
            // deliberately leaves the selection alone (there's
            // nothing to toggle).
            explorer.setSelected(core::kNullEntity);
        }
    };

    if (!dragSelectActive_ && hovered && !overGizmo && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // The mouse-down edge was missed (e.g. this same click just gave
        // the panel focus, so IsMouseClicked() never fired above) --
        // treat the release as a plain click instead of dropping it.
        pickAtCursor();
    }

    if (dragSelectActive_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragSelectActive_ = false;
        ImVec2 delta(io.MousePos.x - dragSelectStart_.x, io.MousePos.y - dragSelectStart_.y);
        float distSq = delta.x * delta.x + delta.y * delta.y;

        if (distSq > kDragThresholdPixels * kDragThresholdPixels) {
            // A real drag: select every entity whose world position
            // projects inside the screen-space rectangle. Position-only
            // (not full projected-AABB coverage) -- simple and correct
            // for "is this object's origin inside the box", the same
            // simplification most editors' marquee-select uses for
            // anything that isn't a dedicated occlusion/coverage query.
            ImVec2 rectMin(std::min(dragSelectStart_.x, io.MousePos.x), std::min(dragSelectStart_.y, io.MousePos.y));
            ImVec2 rectMax(std::max(dragSelectStart_.x, io.MousePos.x), std::max(dragSelectStart_.y, io.MousePos.y));

            glm::mat4 proj = renderCamera_.projectionMatrix(imageSize.x / imageSize.y);
            glm::mat4 view = renderCamera_.viewMatrix();
            glm::mat4 viewProj = proj * view;

            std::vector<core::EntityId> picked;
            auto view2 = ecs.view<core::Transform>();
            for (auto entity : view2) {
                // Real world position -- see parentWorldMatrix()'s own
                // comment; transform.position alone is local-space once an
                // entity is parented, so a parented prop's marquee-select
                // hit point would silently drift away from its own real
                // rendered position by its parent's world offset.
                glm::vec3 worldPosition = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, entity)[3]);
                glm::vec4 clip = viewProj * glm::vec4(worldPosition, 1.0f);
                if (clip.w <= 0.0f) continue; // behind the camera
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                float screenX = imageOrigin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x;
                // See worldToScreen()'s comment: renderCamera_'s Vulkan Y
                // flip already puts ndc.y == -1 at the viewport top, so no
                // extra "1.0f - t" flip belongs here.
                float screenY = imageOrigin.y + (ndc.y * 0.5f + 0.5f) * imageSize.y;
                if (screenX >= rectMin.x && screenX <= rectMax.x && screenY >= rectMin.y && screenY <= rectMax.y) {
                    picked.push_back(entity);
                }
            }
            explorer.setSelectedMultiple(std::move(picked));
        } else {
            // Not a drag -- a plain click.
            pickAtCursor();
        }
    }
}

bool ViewportPanel::worldToScreen(const glm::mat4& viewProj, glm::vec3 worldPos, ImVec2 imageOrigin,
                                   ImVec2 imageSize, ImVec2& outScreen) const {
    glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.001f) return false; // behind (or at) the camera -- see handleSelection()'s identical guard
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outScreen.x = imageOrigin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x;
    // renderCamera_.projectionMatrix() already applies Vulkan's Y flip (see
    // Camera.hpp), so ndc.y == -1 is the viewport TOP here -- stacking an
    // OpenGL-style "1.0f - t" flip on top of that mirrored every caller of
    // this function (grid overlay, camera rail, physics debug draws, sculpt
    // brush ring, ...) about the horizontal centerline.
    outScreen.y = imageOrigin.y + (ndc.y * 0.5f + 0.5f) * imageSize.y;
    return true;
}

namespace {
// Box corners in the collider's own local space (matches how
// Physics::attachBodyToEntity() builds a JPH::BoxShapeSettings straight
// from ColliderShape::params as half-extents -- no Transform::scale
// applied, see that function's comment; the debug wireframe must use the
// exact same position+rotation-only placement or it would silently
// disagree with where the live Jolt body actually is).
std::array<glm::vec3, 8> boxCorners(glm::vec3 halfExtent) {
    return {glm::vec3{-halfExtent.x, -halfExtent.y, -halfExtent.z}, glm::vec3{halfExtent.x, -halfExtent.y, -halfExtent.z},
            glm::vec3{halfExtent.x, -halfExtent.y, halfExtent.z}, glm::vec3{-halfExtent.x, -halfExtent.y, halfExtent.z},
            glm::vec3{-halfExtent.x, halfExtent.y, -halfExtent.z}, glm::vec3{halfExtent.x, halfExtent.y, -halfExtent.z},
            glm::vec3{halfExtent.x, halfExtent.y, halfExtent.z}, glm::vec3{-halfExtent.x, halfExtent.y, halfExtent.z}};
}
constexpr int kBoxEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                   {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

// A circle of `segments` points around `center`, in the plane spanned by
// `axisA`/`axisB` (both expected orthonormal) -- the one shared primitive
// Sphere and Capsule wireframes are both built from (three orthogonal
// circles for a sphere; two end-cap circles plus four connecting side
// lines for a capsule).
std::vector<glm::vec3> circlePoints(glm::vec3 center, glm::vec3 axisA, glm::vec3 axisB, float radius, int segments) {
    std::vector<glm::vec3> points;
    points.reserve(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        float t = (2.0f * 3.14159265f * static_cast<float>(i)) / static_cast<float>(segments);
        points.push_back(center + axisA * (radius * std::cos(t)) + axisB * (radius * std::sin(t)));
    }
    return points;
}
} // namespace

// Kronos ("Studio Movie Mode"): the camera-rail gizmo -- the spline
// itself, its control-point handles, and the look-at vectors that show
// what the camera is actually aiming at along the move. Drawn here rather
// than in MovieModePlugin because this panel owns the camera matrices and
// the viewport image rectangle; the plugin owns the rail. Same split, and
// the same projection helpers, as drawPhysicsDebugOverlay() below.
void ViewportPanel::drawCameraRailOverlay(plugins::MovieModePlugin& movieMode, ImVec2 imageOrigin, ImVec2 imageSize) {
    if (!movieMode.isOpen() || !movieMode.showRailGizmo()) return;
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) return;

    cinematic::CameraRail& rail = movieMode.rail();
    if (rail.pointCount() < 2) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    glm::mat4 viewProj = renderCamera_.projectionMatrix(imageSize.x / imageSize.y) * renderCamera_.viewMatrix();

    auto projectLine = [&](glm::vec3 a, glm::vec3 b, ImU32 color, float thickness) {
        ImVec2 screenA, screenB;
        if (!worldToScreen(viewProj, a, imageOrigin, imageSize, screenA)) return;
        if (!worldToScreen(viewProj, b, imageOrigin, imageSize, screenB)) return;
        drawList->AddLine(screenA, screenB, color, thickness);
    };

    // --- the spline ------------------------------------------------------
    // Sampled through CameraRail::samplePosition() rather than
    // re-evaluating the spline basis here, so the drawn path is by
    // construction the path the camera travels -- including the difference
    // between Catmull-Rom (through the points) and Bezier (shaped by them),
    // which is exactly what the author needs to see.
    constexpr ImU32 kRailColor = IM_COL32(80, 170, 225, 235);
    constexpr int kSamplesPerSegment = 24;
    const int totalSamples = static_cast<int>(rail.pointCount() - 1) * kSamplesPerSegment;
    glm::vec3 previous = rail.samplePosition(0.0f);
    for (int i = 1; i <= totalSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(totalSamples);
        const glm::vec3 current = rail.samplePosition(t);
        projectLine(previous, current, kRailColor, 2.0f);
        previous = current;
    }

    // --- look-at vectors --------------------------------------------------
    if (movieMode.showLookAtLines()) {
        constexpr ImU32 kAimColor = IM_COL32(235, 190, 90, 130);
        constexpr int kAimSamples = 8;
        for (int i = 0; i <= kAimSamples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kAimSamples);
            // deltaSeconds of 0: sampling for a gizmo must not advance the
            // rail's damped aim, which is stateful -- see CameraRail::sample().
            const cinematic::RailSample sample = rail.sample(t, 0.0f);
            projectLine(sample.position, sample.position + sample.forward * 2.5f, kAimColor, 1.4f);
        }
    }

    // --- control-point handles -------------------------------------------
    const std::vector<cinematic::RailPoint>& points = rail.points();
    const int selectedPoint = movieMode.selectedRailPoint();
    int hoveredPoint = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    for (size_t i = 0; i < points.size(); ++i) {
        ImVec2 screen;
        if (!worldToScreen(viewProj, points[i].position, imageOrigin, imageSize, screen)) continue;

        const bool isSelected = static_cast<int>(i) == selectedPoint;
        const float radius = isSelected ? 7.0f : 5.0f;
        const bool isHovered = std::abs(mouse.x - screen.x) <= radius + 3.0f &&
                                std::abs(mouse.y - screen.y) <= radius + 3.0f;
        if (isHovered) hoveredPoint = static_cast<int>(i);

        const ImU32 fill = isSelected ? IM_COL32(255, 220, 120, 255)
                                       : (isHovered ? IM_COL32(190, 225, 250, 255) : IM_COL32(80, 170, 225, 255));
        drawList->AddCircleFilled(screen, radius, fill);
        drawList->AddCircle(screen, radius, IM_COL32(15, 18, 20, 220), 0, 1.6f);

        char label[16];
        std::snprintf(label, sizeof(label), "%zu", i + 1);
        drawList->AddText(ImVec2(screen.x + radius + 3.0f, screen.y - 7.0f), IM_COL32(220, 235, 245, 220), label);
    }

    // --- where the camera actually is at the current playhead -------------
    {
        ImVec2 screen;
        if (worldToScreen(viewProj, rail.samplePosition(movieMode.railParameterAtPlayhead()), imageOrigin, imageSize,
                           screen)) {
            drawList->AddCircleFilled(screen, 4.5f, IM_COL32(242, 90, 76, 255));
            drawList->AddCircle(screen, 7.5f, IM_COL32(242, 90, 76, 170), 0, 1.5f);
        }
    }

    // --- handle dragging --------------------------------------------------
    // Dragging moves the point in the plane facing the camera through its
    // current position: with only a 2D mouse there is no depth information
    // to recover, and projecting onto the view plane is the one choice that
    // makes the handle track the cursor exactly.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredPoint >= 0) {
        movieMode.setSelectedRailPoint(hoveredPoint);
        draggingRailPoint_ = hoveredPoint;
    }
    if (draggingRailPoint_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && draggingRailPoint_ < static_cast<int>(points.size())) {
            const glm::vec3 pointPos = points[static_cast<size_t>(draggingRailPoint_)].position;
            glm::vec3 rayOrigin, rayDirection;
            computeMouseRay(mouse, imageOrigin, imageSize, rayOrigin, rayDirection);
            const glm::vec3 planeNormal = glm::normalize(camera_.position - pointPos);
            const float denominator = glm::dot(rayDirection, planeNormal);
            // A ray parallel to the plane has no intersection; skipping is
            // an honest no-op rather than dividing by ~0 and flinging the
            // point to infinity.
            if (std::abs(denominator) > 1e-5f) {
                const float distance = glm::dot(pointPos - rayOrigin, planeNormal) / denominator;
                if (distance > 0.0f) movieMode.moveRailPoint(draggingRailPoint_, rayOrigin + rayDirection * distance);
            }
        } else {
            draggingRailPoint_ = -1;
        }
    }
}

void ViewportPanel::drawPhysicsDebugOverlay(core::ECS& ecs, plugins::PhysicsPreviewPlugin& physicsPreview,
                                             ImVec2 imageOrigin, ImVec2 imageSize) {
    if (!physicsPreview.showColliders && !physicsPreview.showContacts && !physicsPreview.showRaycasts) return;
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    glm::mat4 viewProj = renderCamera_.projectionMatrix(imageSize.x / imageSize.y) * renderCamera_.viewMatrix();

    auto projectLine = [&](glm::vec3 a, glm::vec3 b, ImU32 color, float thickness) {
        ImVec2 screenA, screenB;
        if (!worldToScreen(viewProj, a, imageOrigin, imageSize, screenA)) return;
        if (!worldToScreen(viewProj, b, imageOrigin, imageSize, screenB)) return;
        drawList->AddLine(screenA, screenB, color, thickness);
    };
    auto projectPolyline = [&](const std::vector<glm::vec3>& points, ImU32 color, float thickness, bool closed) {
        size_t count = points.size();
        for (size_t i = 0; i + 1 < count; ++i) projectLine(points[i], points[i + 1], color, thickness);
        if (closed && count > 1) projectLine(points[count - 1], points[0], color, thickness);
    };

    if (physicsPreview.showColliders) {
        constexpr ImU32 kColliderColor = IM_COL32(90, 230, 120, 220);
        constexpr int kCircleSegments = 24;
        auto view = ecs.view<core::Transform, core::ColliderShape>();
        for (auto entity : view) {
            auto& transform = view.get<core::Transform>(entity);
            auto& shape = view.get<core::ColliderShape>(entity);
            glm::mat3 rot = glm::mat3_cast(transform.rotation);
            glm::vec3 right = rot[0], up = rot[1], fwd = rot[2];

            switch (shape.kind) {
                case core::ColliderShapeKind::Box: {
                    auto corners = boxCorners(shape.params);
                    for (auto& c : corners) c = transform.position + rot * c;
                    for (auto& edge : kBoxEdges) projectLine(corners[edge[0]], corners[edge[1]], kColliderColor, 1.5f);
                    break;
                }
                case core::ColliderShapeKind::Sphere: {
                    float radius = shape.params.x;
                    projectPolyline(circlePoints(transform.position, right, up, radius, kCircleSegments), kColliderColor,
                                     1.5f, true);
                    projectPolyline(circlePoints(transform.position, right, fwd, radius, kCircleSegments), kColliderColor,
                                     1.5f, true);
                    projectPolyline(circlePoints(transform.position, up, fwd, radius, kCircleSegments), kColliderColor,
                                     1.5f, true);
                    break;
                }
                case core::ColliderShapeKind::Capsule: {
                    // params: x=radius, y=halfHeight -- same convention
                    // Physics::attachBodyToEntity()'s Capsule case reads.
                    float radius = shape.params.x;
                    float halfHeight = shape.params.y;
                    glm::vec3 top = transform.position + up * halfHeight;
                    glm::vec3 bottom = transform.position - up * halfHeight;
                    projectPolyline(circlePoints(top, right, fwd, radius, kCircleSegments), kColliderColor, 1.5f, true);
                    projectPolyline(circlePoints(bottom, right, fwd, radius, kCircleSegments), kColliderColor, 1.5f, true);
                    for (int i = 0; i < 4; ++i) {
                        float t = (2.0f * 3.14159265f * static_cast<float>(i)) / 4.0f;
                        glm::vec3 offset = right * (radius * std::cos(t)) + fwd * (radius * std::sin(t));
                        projectLine(bottom + offset, top + offset, kColliderColor, 1.5f);
                    }
                    break;
                }
                case core::ColliderShapeKind::Mesh:
                    // Real, stated limitation -- see this method's own doc
                    // comment (drawPhysicsDebugOverlay's declaration).
                    break;
            }
        }
    }

    if (physicsPreview.showContacts) {
        constexpr ImU32 kContactColor = IM_COL32(255, 200, 60, 230);
        constexpr float kNormalLength = 0.4f;
        for (const auto& contact : physicsPreview.recentContacts()) {
            ImVec2 screenPoint;
            if (worldToScreen(viewProj, contact.point, imageOrigin, imageSize, screenPoint)) {
                drawList->AddCircleFilled(screenPoint, 4.0f, kContactColor);
            }
            projectLine(contact.point, contact.point + contact.normal * kNormalLength, kContactColor, 2.0f);
        }
    }

    if (physicsPreview.showRaycasts && physicsPreview.hasTestRay()) {
        constexpr ImU32 kRayColor = IM_COL32(120, 180, 255, 230);
        constexpr ImU32 kHitColor = IM_COL32(255, 90, 90, 230);
        const core::Physics::RaycastHit& hit = physicsPreview.testRayHit();
        glm::vec3 endPoint = hit.hit ? hit.point : physicsPreview.testRayOrigin();
        projectLine(physicsPreview.testRayOrigin(), endPoint, kRayColor, 2.0f);
        if (hit.hit) {
            ImVec2 screenHit;
            if (worldToScreen(viewProj, hit.point, imageOrigin, imageSize, screenHit)) {
                drawList->AddCircleFilled(screenHit, 5.0f, kHitColor);
            }
            projectLine(hit.point, hit.point + hit.normal * 0.4f, kHitColor, 2.0f);
        }
    }
}

void ViewportPanel::drawSprint8DebugOverlays(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                              const ViewportDebugContext& debugContext, ImVec2 imageOrigin,
                                              ImVec2 imageSize) {
    if (!showBoundingBoxes_ && !showTerrainStreaming_ && !showCascades_) return;
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float aspect = imageSize.x / imageSize.y;
    glm::mat4 viewProj = renderCamera_.projectionMatrix(aspect) * renderCamera_.viewMatrix();

    auto projectLine = [&](glm::vec3 a, glm::vec3 b, ImU32 color, float thickness) {
        ImVec2 screenA, screenB;
        if (!worldToScreen(viewProj, a, imageOrigin, imageSize, screenA)) return;
        if (!worldToScreen(viewProj, b, imageOrigin, imageSize, screenB)) return;
        drawList->AddLine(screenA, screenB, color, thickness);
    };

    // Bounding boxes -- every entity with a real Renderable + Transform +
    // a resolvable mesh gets its real local AABB (Mesh::localBoundsMin()/
    // Max(), the same data core::pickEntity() already trusts for click-
    // to-select) drawn as a world-space wireframe box, corners transformed
    // by that entity's own Transform (position/rotation/scale) rather
    // than assuming axis-aligned-in-world (a rotated/scaled entity's box
    // wireframe rotates/scales with it, even though the *box itself* is
    // no longer strictly axis-aligned once rotated -- a real, honest
    // "oriented bounding box drawn from an AABB source" simplification,
    // not a true world-space AABB recompute).
    if (showBoundingBoxes_) {
        constexpr ImU32 kBoundsColor = IM_COL32(255, 170, 60, 200);
        auto view = ecs.view<core::Transform, core::Renderable>();
        for (auto entity : view) {
            auto& transform = view.get<core::Transform>(entity);
            auto& renderable = view.get<core::Renderable>(entity);
            const core::Mesh* mesh = meshLibrary.get(renderable.meshHandle);
            if (mesh == nullptr) continue;
            glm::vec3 boundsMin = mesh->localBoundsMin();
            glm::vec3 boundsMax = mesh->localBoundsMax();
            glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
            glm::vec3 halfExtent = (boundsMax - boundsMin) * 0.5f;
            auto corners = boxCorners(halfExtent);
            // hierarchy::computeWorldMatrix(), not transform.matrix() alone --
            // otherwise a parented entity's bounds wireframe is drawn at its
            // local-space position instead of where it's actually rendered
            // (same class of offset-drift bug already fixed for picking/
            // gizmo/render -- see ScenePicking.cpp's own comment).
            glm::mat4 model = core::hierarchy::computeWorldMatrix(ecs, entity);
            for (auto& c : corners) c = glm::vec3(model * glm::vec4(center + c, 1.0f));
            for (auto& edge : kBoxEdges) projectLine(corners[edge[0]], corners[edge[1]], kBoundsColor, 1.0f);
        }
    }

    // Terrain streaming -- a wireframe box per currently-*loaded* chunk
    // (unloaded chunks intentionally not drawn, so the overlay itself
    // visually confirms streaming is really happening as the camera
    // moves) plus two real horizontal rings at loadRadius/unloadRadius
    // centered on the camera -- the exact two radii
    // Terrain::shouldChunkBeLoaded() actually decides against.
    if (showTerrainStreaming_ && debugContext.terrain != nullptr) {
        constexpr ImU32 kLoadedChunkColor = IM_COL32(90, 200, 255, 180);
        constexpr ImU32 kLoadRadiusColor = IM_COL32(90, 230, 120, 200);
        constexpr ImU32 kUnloadRadiusColor = IM_COL32(230, 90, 90, 200);
        constexpr int kRingSegments = 48;
        for (const core::Terrain::ChunkDebugInfo& chunkInfo : debugContext.terrain->chunkDebugInfo()) {
            if (!chunkInfo.loaded) continue;
            glm::vec3 halfExtent(chunkInfo.halfExtentX, 0.5f, chunkInfo.halfExtentZ);
            auto corners = boxCorners(halfExtent);
            for (auto& c : corners) c += chunkInfo.center;
            for (auto& edge : kBoxEdges) projectLine(corners[edge[0]], corners[edge[1]], kLoadedChunkColor, 1.0f);
        }
        glm::vec3 camPos = camera_.position;
        auto ring = [&](float radius, ImU32 color) {
            auto points = circlePoints(glm::vec3(camPos.x, camPos.y, camPos.z), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1),
                                        radius, kRingSegments);
            for (size_t i = 0; i + 1 < points.size(); ++i) projectLine(points[i], points[i + 1], color, 2.0f);
            if (points.size() > 1) projectLine(points.back(), points.front(), color, 2.0f);
        };
        if (debugContext.terrainLoadRadius > 0.0f) ring(debugContext.terrainLoadRadius, kLoadRadiusColor);
        if (debugContext.terrainUnloadRadius > 0.0f) ring(debugContext.terrainUnloadRadius, kUnloadRadiusColor);
    }

    // CSM cascades -- a real boundary rectangle at each cascade's actual
    // split depth (Renderer::computeCascades(), the exact same function
    // scene.frag's shadow sampling is fit against), perpendicular to the
    // camera's forward axis, sized generously in the camera's right/up
    // plane just to be visually legible (the rectangle's *size* is not
    // itself meaningful shadow data -- only its *depth*, i.e. which plane
    // along the view axis it sits at, is real).
    if (showCascades_ && debugContext.renderer != nullptr) {
        constexpr ImU32 kCascadeColors[core::Renderer::kCascadeCount] = {
            IM_COL32(255, 210, 90, 220), IM_COL32(255, 140, 90, 220), IM_COL32(255, 90, 90, 220),
            IM_COL32(200, 90, 255, 220)};
        std::array<float, core::Renderer::kCascadeCount> splitDepths =
            debugContext.renderer->debugCascadeSplitDepths(camera_, aspect);
        glm::vec3 forward = camera_.forward();
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));
        constexpr float kRectHalfSize = 8.0f;
        for (uint32_t i = 0; i < core::Renderer::kCascadeCount; ++i) {
            glm::vec3 planeCenter = camera_.position + forward * splitDepths[i];
            glm::vec3 corners[4] = {
                planeCenter - right * kRectHalfSize - up * kRectHalfSize,
                planeCenter + right * kRectHalfSize - up * kRectHalfSize,
                planeCenter + right * kRectHalfSize + up * kRectHalfSize,
                planeCenter - right * kRectHalfSize + up * kRectHalfSize,
            };
            for (int c = 0; c < 4; ++c) projectLine(corners[c], corners[(c + 1) % 4], kCascadeColors[i], 2.0f);
        }
    }
}

void ViewportPanel::drawSubObjectEditing(plugins::ModelingModePlugin& modelingMode, core::ECS& ecs,
                                          core::EntityId selected, ImVec2 imageOrigin, ImVec2 imageSize) {
    auto* transform = ecs.tryGetComponent<core::Transform>(selected);
    auto* editable = ecs.tryGetComponent<core::EditableMeshComponent>(selected);
    auto* renderable = ecs.tryGetComponent<core::Renderable>(selected);
    if (transform == nullptr || editable == nullptr || renderable == nullptr) return;
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) return;

    // hierarchy::computeWorldMatrix(), not transform->matrix() alone --
    // otherwise vertex/edge/face highlights land at a parented mesh
    // entity's local-space position instead of its real rendered position
    // (same class of offset-drift bug already fixed for picking/gizmo/
    // render -- see ScenePicking.cpp's own comment).
    const glm::mat4 model = core::hierarchy::computeWorldMatrix(ecs, selected);
    const glm::mat4 view = renderCamera_.viewMatrix();
    const glm::mat4 proj = renderCamera_.projectionMatrix(imageSize.x / imageSize.y);
    const glm::mat4 viewProj = proj * view;
    const core::EditableMesh& mesh = editable->mesh;

    // --- highlight the current real selection -----------------------------
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    constexpr ImU32 kHighlightColor = IM_COL32(255, 200, 60, 255);
    switch (modelingMode.subObjectMode()) {
        case core::EditableMesh::SelectionMode::Vertex: {
            if (editable->selectedVertex < mesh.vertexCount()) {
                glm::vec3 worldPos = glm::vec3(model * glm::vec4(mesh.vertices()[editable->selectedVertex].position, 1.0f));
                ImVec2 screen;
                if (worldToScreen(viewProj, worldPos, imageOrigin, imageSize, screen)) {
                    drawList->AddCircleFilled(screen, 6.0f, kHighlightColor);
                    drawList->AddCircle(screen, 6.0f, IM_COL32(20, 20, 20, 220), 0, 1.5f);
                }
            }
            break;
        }
        case core::EditableMesh::SelectionMode::Edge: {
            uint32_t v0 = editable->selectedEdge.first;
            uint32_t v1 = editable->selectedEdge.second;
            if (v0 < mesh.vertexCount() && v1 < mesh.vertexCount()) {
                glm::vec3 wp0 = glm::vec3(model * glm::vec4(mesh.vertices()[v0].position, 1.0f));
                glm::vec3 wp1 = glm::vec3(model * glm::vec4(mesh.vertices()[v1].position, 1.0f));
                ImVec2 s0, s1;
                if (worldToScreen(viewProj, wp0, imageOrigin, imageSize, s0) &&
                    worldToScreen(viewProj, wp1, imageOrigin, imageSize, s1)) {
                    drawList->AddLine(s0, s1, kHighlightColor, 4.0f);
                }
            }
            break;
        }
        case core::EditableMesh::SelectionMode::Face:
        default: {
            if (editable->selectedFace < mesh.faceCount()) {
                std::array<uint32_t, 3> verts = mesh.faceVertexIndices(editable->selectedFace);
                ImVec2 screenPts[3];
                bool allVisible = true;
                for (int i = 0; i < 3; ++i) {
                    glm::vec3 wp =
                        glm::vec3(model * glm::vec4(mesh.vertices()[verts[static_cast<size_t>(i)]].position, 1.0f));
                    if (!worldToScreen(viewProj, wp, imageOrigin, imageSize, screenPts[i])) {
                        allVisible = false;
                        break;
                    }
                }
                if (allVisible) {
                    drawList->AddTriangleFilled(screenPts[0], screenPts[1], screenPts[2], IM_COL32(255, 200, 60, 70));
                    drawList->AddTriangle(screenPts[0], screenPts[1], screenPts[2], kHighlightColor, 2.5f);
                }
            }
            break;
        }
    }

    // --- real Ctrl+Click ray-vs-mesh picking -------------------------------
    // Ctrl, not a plain click, so this never steals handleSelection()'s own
    // plain-click whole-entity picking/drag-select-box for this same
    // window -- the same "Ctrl overloads left-click for a second, more
    // specific action" convention studio::PreviewScene's own
    // ctrlDragPaintActive_ already established.
    ImGuiIO& io = ImGui::GetIO();
    bool hovered = ImGui::IsWindowHovered();
    bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    bool releasedWithoutDrag = ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                                io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] <
                                    io.MouseDragThreshold * io.MouseDragThreshold;
    if (hovered && !overGizmo && io.KeyCtrl && releasedWithoutDrag) {
        constexpr float kMaxPickDistance = 1000.0f;
        glm::vec3 worldOrigin, worldDirection;
        computeMouseRay(io.MousePos, imageOrigin, imageSize, worldOrigin, worldDirection);
        glm::mat4 invModel = glm::inverse(model);
        glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(worldOrigin, 1.0f));
        glm::vec3 localDirection = glm::normalize(glm::vec3(invModel * glm::vec4(worldDirection, 0.0f)));
        modelingMode.pickSubObject(*editable, localOrigin, localDirection, kMaxPickDistance);
    }

    // --- real Translate-only gizmo on the current selection ----------------
    glm::vec3 localAnchor = modelingMode.subObjectAnchorLocal(*editable);
    glm::vec3 worldAnchor = glm::vec3(model * glm::vec4(localAnchor, 1.0f));

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y);

    // A pure-translation matrix, not the entity's own model -- the
    // sub-object gizmo always manipulates in world axes (ImGuizmo::WORLD
    // below), unlike drawGizmo()'s object-mode gizmo which can follow the
    // entity's own local orientation.
    glm::mat4 gizmoModel(1.0f);
    gizmoModel[3] = glm::vec4(worldAnchor, 1.0f);

    // See drawGizmo()'s identical fix/comment: ImGuizmo needs an unflipped
    // (OpenGL-convention) projection, not renderCamera_'s Vulkan-flipped one.
    glm::mat4 gizmoProj = proj;
    gizmoProj[1][1] *= -1.0f;
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(gizmoProj), ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                          glm::value_ptr(gizmoModel));

    if (ImGuizmo::IsUsing()) {
        glm::vec3 newWorldAnchor = glm::vec3(gizmoModel[3]);
        glm::vec3 worldDelta = newWorldAnchor - worldAnchor;
        if (worldDelta != glm::vec3(0.0f)) {
            // Maps the world-space drag delta back into the mesh's own
            // local space, accounting for the entity's rotation and
            // (possibly non-uniform) scale -- the exact inverse of how
            // `model` above turns a local vertex position into `worldPos`.
            glm::mat3 linear(model);
            glm::vec3 localDelta = glm::inverse(linear) * worldDelta;
            modelingMode.translateSubObjectSelection(*editable, *renderable, localDelta);
        }
    }

    // --- PBR paint brush ring: a real preview of where/how large the
    // next "Apply Sculpt" stroke will land -- centered on the exact same
    // subObjectAnchorLocal() applySculptStroke() itself brushes around,
    // sized to the exact current sculptRadius() slider value. Gated on a
    // real (in-range) selection, same condition the highlight switch
    // above already checks, since subObjectAnchorLocal() falls back to
    // the entity origin otherwise (see its own doc comment) and drawing
    // a ring there would be misleading, not a real preview. This is a
    // static preview of the *next* stroke, not a live mouse-drag brush --
    // see applySculptStroke()'s own header comment for why no continuous
    // drag gesture exists here to follow.
    bool hasValidSubObjectSelection = false;
    switch (modelingMode.subObjectMode()) {
        case core::EditableMesh::SelectionMode::Vertex:
            hasValidSubObjectSelection = editable->selectedVertex < mesh.vertexCount();
            break;
        case core::EditableMesh::SelectionMode::Edge:
            hasValidSubObjectSelection = editable->selectedEdge.first < mesh.vertexCount() &&
                                          editable->selectedEdge.second < mesh.vertexCount();
            break;
        case core::EditableMesh::SelectionMode::Face:
        default:
            hasValidSubObjectSelection = editable->selectedFace < mesh.faceCount();
            break;
    }
    if (hasValidSubObjectSelection) {
        glm::vec3 camRight = glm::normalize(glm::cross(renderCamera_.forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
        ImVec2 screenCenter, screenRim;
        if (worldToScreen(viewProj, worldAnchor, imageOrigin, imageSize, screenCenter) &&
            worldToScreen(viewProj, worldAnchor + camRight * modelingMode.sculptRadius(), imageOrigin, imageSize,
                          screenRim)) {
            float screenRadius = std::sqrt((screenRim.x - screenCenter.x) * (screenRim.x - screenCenter.x) +
                                            (screenRim.y - screenCenter.y) * (screenRim.y - screenCenter.y));
            constexpr ImU32 kBrushRingColor = IM_COL32(80, 220, 255, 220);
            drawList->AddCircle(screenCenter, screenRadius, kBrushRingColor, 48, 2.0f);
            drawList->AddCircleFilled(screenCenter, 3.0f, kBrushRingColor);
        }
    }
}

void ViewportPanel::drawTerrainSculpt(plugins::TerrainEditorPlugin& terrainEditor, core::ECS& ecs,
                                       ImVec2 imageOrigin, ImVec2 imageSize, float deltaTime) {
    // endLiveStroke() must run before any early-return below: if a stroke
    // is active and this frame happens to fail hasTerrain()/imageSize (a
    // terrain deleted mid-drag, a viewport briefly collapsed to zero
    // size), skipping the release check would leave strokeActive_ stuck
    // true forever -- beginLiveStroke() no-ops against that, so every
    // future stroke would silently stop recording undo, and the stale
    // strokeBeforeSnapshot_ could even be the wrong size for a
    // recreated terrain.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        terrainEditor.endLiveStroke();
    }

    if (!terrainEditor.hasTerrain() || imageSize.x <= 0.0f || imageSize.y <= 0.0f) return;

    ImGuiIO& io = ImGui::GetIO();
    bool hovered = ImGui::IsWindowHovered();
    bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

    glm::vec3 rayOrigin, rayDirection;
    computeMouseRay(io.MousePos, imageOrigin, imageSize, rayOrigin, rayDirection);

    glm::vec3 hitPoint(0.0f);
    bool hasHit = hovered && !overGizmo && terrainEditor.raycastFromMouse(rayOrigin, rayDirection, hitPoint);

    // Click/hold drives one stroke -- see beginLiveStroke()'s own comment
    // on why this lands one UndoStack::Command per drag instead of per
    // frame. A stroke that starts on the terrain and drags off it still
    // closes out correctly: applyLiveBrush() simply isn't called on
    // off-terrain frames, and the release check above already fired
    // before this function could return early for any reason.
    if (hasHit && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        terrainEditor.beginLiveStroke();
    }
    if (hasHit && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        terrainEditor.applyLiveBrush(hitPoint.x, hitPoint.z, ecs, deltaTime);
    }

    if (!hasHit) return;

    // Brush-ring preview -- same worldToScreen()+camRight-offset radius
    // computation drawSubObjectEditing()'s own sculpt ring uses, just
    // centered on a live raycast hit instead of a static sub-object
    // anchor. forward() can be near-parallel to world-up (camera looking
    // straight down, the single most common pose for a terrain tool),
    // which sends the un-guarded cross product to a near-zero vector and
    // normalize() to NaN -- fall back to world +X, which is never
    // parallel to a forward vector that's near vertical.
    const glm::mat4 view = renderCamera_.viewMatrix();
    const glm::mat4 proj = renderCamera_.projectionMatrix(imageSize.x / imageSize.y);
    const glm::mat4 viewProj = proj * view;
    glm::vec3 camRight = glm::cross(renderCamera_.forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    camRight = glm::length(camRight) > 1e-4f ? glm::normalize(camRight) : glm::vec3(1.0f, 0.0f, 0.0f);
    ImVec2 screenCenter, screenRim;
    if (worldToScreen(viewProj, hitPoint, imageOrigin, imageSize, screenCenter) &&
        worldToScreen(viewProj, hitPoint + camRight * terrainEditor.brushRadius(), imageOrigin, imageSize,
                      screenRim)) {
        float screenRadius = std::sqrt((screenRim.x - screenCenter.x) * (screenRim.x - screenCenter.x) +
                                        (screenRim.y - screenCenter.y) * (screenRim.y - screenCenter.y));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        constexpr ImU32 kBrushRingColor = IM_COL32(120, 255, 140, 220);
        drawList->AddCircle(screenCenter, screenRadius, kBrushRingColor, 48, 2.0f);
        drawList->AddCircleFilled(screenCenter, 3.0f, kBrushRingColor);
    }
}

void ViewportPanel::draw(float deltaTime, VkDescriptorSet sceneTexture, VkExtent2D sceneTextureExtent,
                          core::ECS* ecs, core::MeshLibrary* meshLibrary, ExplorerPanel& explorer,
                          plugins::PhysicsPreviewPlugin* physicsPreview, const ViewportDebugContext& debugContext,
                          plugins::MovieModePlugin* movieMode, bool showEngineDebugOverlays,
                          plugins::ModelingModePlugin* modelingMode, plugins::TerrainEditorPlugin* terrainEditor) {
    ImGuizmo::BeginFrame(); // once per ImGui frame -- see header comment

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    updateFreeFly(deltaTime);
    updateFocus(deltaTime);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    desiredExtent_ = {static_cast<uint32_t>(std::max(1.0f, avail.x)), static_cast<uint32_t>(std::max(1.0f, avail.y))};

    ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
    if (sceneTexture != VK_NULL_HANDLE && sceneTextureExtent.width > 0 && sceneTextureExtent.height > 0) {
        // Real rendered scene from the previous frame's offscreen pass --
        // see OffscreenTarget.hpp's doc comment on the one-frame latency.
        ImGui::Image(sceneTexture, ImVec2(static_cast<float>(sceneTextureExtent.width),
                                           static_cast<float>(sceneTextureExtent.height)));
    } else {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(imageOrigin, ImVec2(imageOrigin.x + avail.x, imageOrigin.y + avail.y), IM_COL32(18, 18, 24, 255));
        ImGui::Dummy(avail);
    }
    ImVec2 imageSize = ImGui::GetItemRectSize();

    // Kronos ("Studio Asset Drag-and-Drop"): real drop target -- the
    // viewport image/dummy just submitted above is "the last item," so
    // this attaches here rather than needing a separate invisible
    // widget. Accepts the exact same "ASSET_WORLD_PROP" payload
    // CreatorAssetBrowserPlugin::drawPropEntries() now offers as a real
    // drag source (a core::WorldPropKind, raw-copied the same way
    // "ASSET_MATERIAL_PRESET" already is). The real drop *position* is
    // a real raycast under the actual drop cursor (ScenePicking::
    // pickEntity(), the same real ray-vs-mesh-AABB test click-to-select
    // already uses below) -- lands ON existing scene geometry the
    // cursor is actually over, falling back to the real y=0 ground
    // plane only when nothing was hit (dropped over open sky).
    if (ecs != nullptr && meshLibrary != nullptr && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_WORLD_PROP")) {
            core::WorldPropKind kind;
            std::memcpy(&kind, payload->Data, sizeof(core::WorldPropKind));

            glm::vec3 rayOrigin, rayDirection;
            computeMouseRay(ImGui::GetMousePos(), imageOrigin, imageSize, rayOrigin, rayDirection);

            glm::vec3 dropPosition = rayOrigin + rayDirection * 20.0f; // real, honest fallback if even the ground plane isn't hit
            core::ScenePickResult hit = core::pickEntity(*ecs, *meshLibrary, rayOrigin, rayDirection, 500.0f);
            if (hit.hit) {
                dropPosition = hit.point;
            } else if (std::fabs(rayDirection.y) > 1e-4f) {
                float t = -rayOrigin.y / rayDirection.y;
                if (t > 0.0f) dropPosition = rayOrigin + rayDirection * t;
            }

            ++propSpawnCount_;
            core::EntityId spawned = spawnPropAuthoring(*ecs, kind, dropPosition, propSpawnCount_,
                                                          propSpawnMeshHandles_.boxMesh, propSpawnMeshHandles_.capsuleMesh);
            explorer.setSelected(spawned);
        }
        ImGui::EndDragDropTarget();
    }

    // Live Sculpt claims plain left-click for terrain sculpting, so it
    // must be the only thing responding to it -- the whole-entity gizmo,
    // click-to-select, and drag-select-box all defer to it while it's on
    // (same "one interaction owns the mouse at a time" precedent
    // drawSubObjectEditing()'s Ctrl+Click already sets for a different
    // plugin, just full deferral instead of modifier-disambiguation since
    // Live Sculpt doesn't use a modifier).
    bool liveSculptActive = terrainEditor != nullptr && terrainEditor->liveSculptEnabled();

    core::EntityId selected = explorer.selectedEntity();
    if (ecs != nullptr && selected != core::kNullEntity && !liveSculptActive) {
        // Kronos ("3D DCC Modeling Suite" -- real sub-object raycast
        // picking): a real core::EditableMeshComponent on the selection
        // switches to Modeling Mode's own vertex/edge/face gizmo instead
        // of the whole-entity Translate/Rotate/Scale one -- see
        // drawSubObjectEditing()'s own comment on why never both at once.
        auto* editable = ecs->tryGetComponent<core::EditableMeshComponent>(selected);
        if (modelingMode != nullptr && editable != nullptr) {
            drawSubObjectEditing(*modelingMode, *ecs, selected, imageOrigin, imageSize);
        } else if (!selectTool_) {
            drawGizmo(*ecs, selected, explorer.selectedEntities(), imageOrigin, imageSize);
        }
    }
    if (ecs != nullptr && meshLibrary != nullptr) {
        drawSelectionHighlight(*ecs, *meshLibrary, explorer.selectedEntities(), imageOrigin, imageSize);
    }

    // Click-to-select / drag-select-box -- only while free-flying isn't
    // consuming the mouse (right-drag), there's an ECS+MeshLibrary to pick
    // against, and Live Sculpt isn't claiming left-click instead.
    bool overViewCube = false;
    if (ribbonMode_ && showViewCube_) {
        const ImVec2 cube = viewCubeOrigin(imageOrigin, imageSize);
        const ImVec2 mouse = ImGui::GetMousePos();
        overViewCube = mouse.x >= cube.x && mouse.y >= cube.y && mouse.x <= cube.x + kViewCubeSize &&
                       mouse.y <= cube.y + kViewCubeSize;
    }
    if (ecs != nullptr && meshLibrary != nullptr && !dragging_ && !liveSculptActive && !overViewCube) {
        handleSelection(*ecs, *meshLibrary, explorer, imageOrigin, imageSize);
    }
    if (ecs != nullptr && !dragging_ && liveSculptActive) {
        drawTerrainSculpt(*terrainEditor, *ecs, imageOrigin, imageSize, deltaTime);
    }

    if (ecs != nullptr && physicsPreview != nullptr) {
        drawPhysicsDebugOverlay(*ecs, *physicsPreview, imageOrigin, imageSize);
    }
    if (movieMode != nullptr) {
        drawCameraRailOverlay(*movieMode, imageOrigin, imageSize);
    }
    if (ecs != nullptr && meshLibrary != nullptr) {
        drawSprint8DebugOverlays(*ecs, *meshLibrary, debugContext, imageOrigin, imageSize);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawStatusBar(drawList, imageOrigin, imageSize, explorer.selectedEntities().size());

    if (movieMode != nullptr) {
        // Kronos (viewport error audit -- Movie Maker active camera
        // switcher overlay): the only real "which camera is currently
        // live" state that exists today is previewThroughRailCamera() --
        // MovieModePlugin owns a single CameraRail, not a multi-camera
        // list, so a real switcher can only ever be a readout of this one
        // boolean, not a fabricated multi-entry picker. Drawn whenever
        // movieMode is active at all, NOT gated on showRailGizmo() /
        // showLookAtLines(), since this reports which camera renders the
        // scene -- independent of whether the rail's own gizmo/lines are
        // currently toggled visible.
        const char* activeCameraLabel =
            movieMode->previewThroughRailCamera() ? "Active Camera: Rail" : "Active Camera: Free-fly";
        ImVec2 textSize = ImGui::CalcTextSize(activeCameraLabel);
        ImVec2 badgeOrigin(imageOrigin.x + imageSize.x - textSize.x - 22.0f, imageOrigin.y + imageSize.y - 26.0f);
        drawList->AddRectFilled(ImVec2(badgeOrigin.x - 6.0f, badgeOrigin.y - 4.0f),
                                 ImVec2(badgeOrigin.x + textSize.x + 6.0f, badgeOrigin.y + textSize.y + 4.0f),
                                 IM_COL32(20, 20, 26, 200), 4.0f);
        drawList->AddText(badgeOrigin,
                           movieMode->previewThroughRailCamera() ? IM_COL32(120, 220, 255, 255)
                                                                  : IM_COL32(210, 212, 218, 255),
                           activeCameraLabel);
    }

    // Gizmo mode toolbar -- W/E/R matches the near-universal DCC/game-editor
    // convention (Roblox Studio included) for translate/rotate/scale.
    // Gated on !WantCaptureKeyboard (a real stability fix: without this,
    // typing "w" into e.g. Inspector's Name field also silently switched
    // the gizmo mode underneath whatever panel actually had focus) and on
    // the viewport itself being hovered, not just "gizmo not in use".
    bool viewportHovered = ImGui::IsWindowHovered();
    if (!ImGuizmo::IsUsing() && !ImGui::GetIO().WantCaptureKeyboard && viewportHovered) {
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (!dragging_ && !ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) setGizmoOperation(GizmoOperation::Translate);
            if (ImGui::IsKeyPressed(ImGuiKey_E)) setGizmoOperation(GizmoOperation::Rotate);
            if (ImGui::IsKeyPressed(ImGuiKey_R)) setGizmoOperation(GizmoOperation::Scale);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_1, false)) setSelectTool(true);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_2, false)) setGizmoOperation(GizmoOperation::Translate);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_3, false)) setGizmoOperation(GizmoOperation::Scale);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_4, false)) setGizmoOperation(GizmoOperation::Rotate);
        if (!dragging_ && !ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false) && ecs != nullptr && meshLibrary != nullptr) {
            focusOn(*ecs, *meshLibrary, explorer.selectedEntities());
        }

        // Kronos ("Developer Velocity Sprint" -- "Drop-to-Ground Shortcut
        // (End Key)"): same gating as W/E/R above (hovered, not fighting
        // an active gizmo drag or a focused text field).
        if (ImGui::IsKeyPressed(ImGuiKey_End) && ecs != nullptr && meshLibrary != nullptr &&
            explorer.selectedEntity() != core::kNullEntity) {
            dropSelectedToGround(*ecs, *meshLibrary, explorer.selectedEntity());
        }
    }

    if (ribbonMode_) {
        drawViewCube(imageOrigin, imageSize);
    } else {
        constexpr float kIconButtonSize = 28.0f;
        constexpr float kToolbarPadding = 5.0f;
        const ImVec2 iconSize(kIconButtonSize, kIconButtonSize);
        const float rounding = ImGui::GetStyle().FrameRounding + 3.0f;

        auto glassPanel = [&](ImVec2 min, ImVec2 max) {
            ui::softShadow(drawList, min, max, rounding, 10.0f, ImVec4(0.0f, 0.0f, 0.0f, 0.35f));
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.82f), rounding);
            drawList->AddRectFilledMultiColor(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, min.y + (max.y - min.y) * 0.5f),
                                              IM_COL32(255, 255, 255, 10), IM_COL32(255, 255, 255, 10),
                                              IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
            drawList->AddRect(min, max, IM_COL32(255, 255, 255, 22), rounding);
        };
        auto separator = [&] {
            ImGui::SameLine(0.0f, 6.0f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            drawList->AddLine(ImVec2(p.x, p.y + 5.0f), ImVec2(p.x, p.y + kIconButtonSize - 5.0f),
                              ImGui::GetColorU32(ImGuiCol_Border), 1.0f);
            ImGui::Dummy(ImVec2(1.0f, kIconButtonSize));
            ImGui::SameLine(0.0f, 6.0f);
        };
        auto label = [&](const char* text) {
            if (!advancedMode_) {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", text);
            }
            ImGui::SameLine(0.0f, 3.0f);
        };
        auto presetCombo = [&](const char* id, const char* preview, const char* tooltip, auto&& body) {
            ImGui::SetNextItemWidth(advancedMode_ ? 62.0f : 72.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, (kIconButtonSize - ImGui::GetFontSize()) * 0.5f));
            if (ImGui::BeginCombo(id, preview)) {
                body();
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
        };

        ImDrawListSplitter splitter;
        splitter.Split(drawList, 2);
        splitter.SetCurrentChannel(drawList, 1);

        ImGui::SetCursorScreenPos(ImVec2(imageOrigin.x + 10.0f + kToolbarPadding, imageOrigin.y + 10.0f + kToolbarPadding));
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 3.0f));

        if (iconButton("gizmo_translate", Icon::Translate, iconSize, gizmoOperation_ == GizmoOperation::Translate,
                        "Move (W)")) {
            gizmoOperation_ = GizmoOperation::Translate;
        }
        ImGui::SameLine();
        if (iconButton("gizmo_rotate", Icon::Rotate, iconSize, gizmoOperation_ == GizmoOperation::Rotate, "Rotate (E)")) {
            gizmoOperation_ = GizmoOperation::Rotate;
        }
        ImGui::SameLine();
        if (iconButton("gizmo_scale", Icon::Scale, iconSize, gizmoOperation_ == GizmoOperation::Scale, "Scale (R)")) {
            gizmoOperation_ = GizmoOperation::Scale;
        }

        separator();
        const bool worldSpace = gizmoSpace_ == GizmoSpace::World;
        if (iconButton("gizmo_space", worldSpace ? Icon::WorldSpace : Icon::LocalSpace, iconSize, false,
                        worldSpace ? "World space (click for local)" : "Local space (click for world)")) {
            gizmoSpace_ = worldSpace ? GizmoSpace::Local : GizmoSpace::World;
        }
        if (!advancedMode_) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(worldSpace ? "World" : "Local");
        }

        separator();
        if (iconButton("grid_snap", Icon::Snap, iconSize, gridSnapEnabled_, "Grid snap for Move")) {
            gridSnapEnabled_ = !gridSnapEnabled_;
        }
        label("Grid");
        static constexpr float kGridSnapPresets[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f};
        char gridPresetLabel[16];
        std::snprintf(gridPresetLabel, sizeof(gridPresetLabel), "%gm", translateSnap_);
        presetCombo("##grid_snap_preset", gridPresetLabel, "Grid snap increment (meters)", [&] {
            for (float preset : kGridSnapPresets) {
                char text[16];
                std::snprintf(text, sizeof(text), "%gm", preset);
                const bool selected = std::fabs(translateSnap_ - preset) < 0.001f;
                if (ImGui::Selectable(text, selected)) translateSnap_ = preset;
                if (selected) ImGui::SetItemDefaultFocus();
            }
        });

        ImGui::SameLine(0.0f, 8.0f);
        if (iconButton("angle_snap", Icon::Snap, iconSize, angleSnapEnabled_, "Angle snap for Rotate")) {
            angleSnapEnabled_ = !angleSnapEnabled_;
        }
        label("Angle");
        static constexpr float kAngleSnapPresets[] = {5.0f, 15.0f, 30.0f, 45.0f, 90.0f};
        char anglePresetLabel[16];
        std::snprintf(anglePresetLabel, sizeof(anglePresetLabel), "%.0f\xc2\xb0", rotateSnapDegrees_);
        presetCombo("##angle_snap_preset", anglePresetLabel, "Angle snap increment (degrees)", [&] {
            for (float preset : kAngleSnapPresets) {
                char text[16];
                std::snprintf(text, sizeof(text), "%.0f\xc2\xb0", preset);
                const bool selected = std::fabs(rotateSnapDegrees_ - preset) < 0.001f;
                if (ImGui::Selectable(text, selected)) rotateSnapDegrees_ = preset;
                if (selected) ImGui::SetItemDefaultFocus();
            }
        });

        ImGui::SameLine(0.0f, 8.0f);
        if (iconButton("scale_snap", Icon::Snap, iconSize, scaleSnapEnabled_, "Scale snap")) {
            scaleSnapEnabled_ = !scaleSnapEnabled_;
        }
        label("Scale");
        ImGui::SetNextItemWidth(advancedMode_ ? 52.0f : 60.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, (kIconButtonSize - ImGui::GetFontSize()) * 0.5f));
        ImGui::DragFloat("##scale_snap_val", &scaleSnap_, 0.01f, 0.01f, 10.0f, "%.2f");
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Scale snap increment (drag to adjust)");

        if (showAssetTools_) {
            separator();
            if (modelImporterPlugin_ != nullptr) {
                if (iconButton("import_asset", Icon::Folder, iconSize, false, "Import 3D asset (glTF / OBJ / FBX)")) {
                    modelImporterPlugin_->browseForFile();
                }
                ImGui::SameLine();
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f, (kIconButtonSize - ImGui::GetFontSize()) * 0.5f));
            if (ui::button(advancedMode_ ? "+##add_primitive" : "+  Add##add_primitive", ui::ButtonKind::Ghost)) {
                ImGui::OpenPopup("##add_primitive_menu");
            }
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Add primitive");
            if (ImGui::BeginPopup("##add_primitive_menu")) {
                ui::sectionHeader("Add primitive");
                if (ecs != nullptr) {
                    if (ImGui::Selectable("Sphere")) spawnPrimitive(*ecs, meshLibrary, explorer, Primitive::Sphere);
                    if (ImGui::Selectable("Cube")) spawnPrimitive(*ecs, meshLibrary, explorer, Primitive::Block);
                    if (ImGui::Selectable("Cylinder")) spawnPrimitive(*ecs, meshLibrary, explorer, Primitive::Cylinder);
                    if (ImGui::Selectable("Plane")) spawnPrimitive(*ecs, meshLibrary, explorer, Primitive::Plane);
                    if (ImGui::Selectable("Torus")) spawnPrimitive(*ecs, meshLibrary, explorer, Primitive::Torus);
                }
                ImGui::EndPopup();
            }
        }

        ImGui::PopStyleVar();
        ImGui::EndGroup();

        const ImVec2 groupMin = ImGui::GetItemRectMin();
        const ImVec2 groupMax = ImGui::GetItemRectMax();
        splitter.SetCurrentChannel(drawList, 0);
        glassPanel(ImVec2(groupMin.x - kToolbarPadding, groupMin.y - kToolbarPadding),
                   ImVec2(groupMax.x + kToolbarPadding, groupMax.y + kToolbarPadding));
        splitter.Merge(drawList);

        // Top-right: view options popover and the beginner/pro density switch.
        const bool hasOverlayOptions = physicsPreview != nullptr || showEngineDebugOverlays;
        const char* modeLabel = advancedMode_ ? "Compact" : "Labels";
        const ImVec2 modeSize(ImGui::CalcTextSize(modeLabel).x + 20.0f, kIconButtonSize);
        const ImVec2 overlaysSize(ImGui::CalcTextSize("Overlays").x + 20.0f, kIconButtonSize);
        float rightWidth = modeSize.x + (hasOverlayOptions ? overlaysSize.x + 3.0f : 0.0f);
        ImVec2 rightMin(imageOrigin.x + imageSize.x - 10.0f - kToolbarPadding * 2.0f - rightWidth, imageOrigin.y + 10.0f);
        if (rightMin.x > groupMax.x + kToolbarPadding + 8.0f) {
            ImDrawListSplitter rightSplitter;
            rightSplitter.Split(drawList, 2);
            rightSplitter.SetCurrentChannel(drawList, 1);
            ImGui::SetCursorScreenPos(ImVec2(rightMin.x + kToolbarPadding, rightMin.y + kToolbarPadding));
            ImGui::BeginGroup();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 3.0f));
            if (hasOverlayOptions) {
                if (ui::button("Overlays", ui::ButtonKind::Ghost, overlaysSize)) ImGui::OpenPopup("##viewport_overlays");
                ImGui::SameLine();
            }
            if (ui::button(modeLabel, ui::ButtonKind::Ghost, modeSize)) advancedMode_ = !advancedMode_;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip(advancedMode_ ? "Compact toolbar. Click to show text labels."
                                                : "Labelled toolbar. Click for compact icons.");
            }
            ImGui::PopStyleVar();
            ImGui::EndGroup();
            const ImVec2 rMin = ImGui::GetItemRectMin();
            const ImVec2 rMax = ImGui::GetItemRectMax();
            rightSplitter.SetCurrentChannel(drawList, 0);
            glassPanel(ImVec2(rMin.x - kToolbarPadding, rMin.y - kToolbarPadding),
                       ImVec2(rMax.x + kToolbarPadding, rMax.y + kToolbarPadding));
            rightSplitter.Merge(drawList);
        }
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f, 0.0f), ImVec2(360.0f, FLT_MAX));
    if (ImGui::BeginPopup("##viewport_overlays")) {
        if (physicsPreview != nullptr) {
            ui::sectionHeader("Physics");
            ui::toggle("Colliders", &physicsPreview->showColliders);
            ui::toggle("Contacts", &physicsPreview->showContacts);
            ui::toggle("Raycasts", &physicsPreview->showRaycasts);
            const bool canCastRay = physicsPreview->isPlaying();
            ImGui::BeginDisabled(!canCastRay);
            if (ui::button("Cast test ray", ui::ButtonKind::Secondary, ImVec2(-FLT_MIN, 0.0f))) {
                physicsPreview->castTestRay(camera_.position, camera_.forward(), 1000.0f);
            }
            ImGui::EndDisabled();
            if (!canCastRay && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Start play mode to cast a ray from the camera.");
            }
        }
        if (showEngineDebugOverlays) {
            ui::sectionHeader("Engine");
            ui::toggle("Bounding boxes", &showBoundingBoxes_);
            ImGui::BeginDisabled(debugContext.terrain == nullptr);
            ui::toggle("Terrain streaming", &showTerrainStreaming_);
            ImGui::EndDisabled();
            ImGui::BeginDisabled(debugContext.renderer == nullptr);
            ui::toggle("Shadow cascades", &showCascades_);
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

glm::vec3 ViewportPanel::spawnPointInFront(core::ECS& ecs, core::MeshLibrary* meshLibrary, glm::vec3 boundsMin,
                                           glm::vec3 boundsMax, core::EntityId exclude) const {
    constexpr float kMaxSpawnDistance = 40.0f;
    const glm::vec3 forward = camera_.forward();
    glm::vec3 target = camera_.position + forward * 6.0f;
    float targetDistance = kMaxSpawnDistance;
    if (forward.y < -0.05f) {
        const float t = -camera_.position.y / forward.y;
        if (t > 0.0f && t <= kMaxSpawnDistance) {
            target = camera_.position + forward * t;
            targetDistance = t;
        }
    }
    if (meshLibrary != nullptr) {
        const core::ScenePickResult hit =
            core::pickEntity(ecs, *meshLibrary, camera_.position, forward, targetDistance, exclude);
        if (hit.hit) target = hit.point - forward * 0.01f;
    }
    if (gridSnapEnabled_ && translateSnap_ > 0.0f) {
        target.x = std::round(target.x / translateSnap_) * translateSnap_;
        target.z = std::round(target.z / translateSnap_) * translateSnap_;
    }
    if (meshLibrary == nullptr) return {target.x, -boundsMin.y, target.z};

    constexpr float kTouchTolerance = 0.001f;
    const glm::vec2 footMin = glm::vec2(target.x + boundsMin.x, target.z + boundsMin.z) + kTouchTolerance;
    const glm::vec2 footMax = glm::vec2(target.x + boundsMax.x, target.z + boundsMax.z) - kTouchTolerance;
    bool foundSurface = false;
    float surfaceY = 0.0f;
    auto view = ecs.view<core::Transform, core::Renderable>();
    for (auto entity : view) {
        if (entity == exclude) continue;
        const auto& renderable = view.get<core::Renderable>(entity);
        if (!renderable.visible) continue;
        const core::Mesh* mesh = meshLibrary->get(renderable.meshHandle);
        if (mesh == nullptr) continue;
        const glm::mat4 world = core::hierarchy::computeWorldMatrix(ecs, entity);
        glm::vec3 worldMin(FLT_MAX);
        glm::vec3 worldMax(-FLT_MAX);
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) ? mesh->localBoundsMax().x : mesh->localBoundsMin().x,
                                  (corner & 2) ? mesh->localBoundsMax().y : mesh->localBoundsMin().y,
                                  (corner & 4) ? mesh->localBoundsMax().z : mesh->localBoundsMin().z);
            const glm::vec3 p = glm::vec3(world * glm::vec4(local, 1.0f));
            worldMin = glm::min(worldMin, p);
            worldMax = glm::max(worldMax, p);
        }
        if (worldMin.y > camera_.position.y) continue;
        if (worldMax.x < footMin.x || worldMin.x > footMax.x || worldMax.z < footMin.y || worldMin.z > footMax.y) continue;
        surfaceY = foundSurface ? std::max(surfaceY, worldMax.y) : worldMax.y;
        foundSurface = true;
    }
    return {target.x, surfaceY - boundsMin.y, target.z};
}

core::EntityId ViewportPanel::spawnPrimitive(core::ECS& ecs, core::MeshLibrary* meshLibrary, ExplorerPanel& explorer,
                                             Primitive kind) {
    struct Spec {
        const char* name;
        uint32_t mesh;
        core::MeshSourceKind sourceKind;
        glm::vec3 params;
        bool hasMeshSource;
        float lift;
    };
    // Cylinder has no MeshSourceKind of its own, so it isn't saved with a source.
    Spec spec{};
    switch (kind) {
        case Primitive::Block:
            spec = {"Part", propSpawnMeshHandles_.boxMesh, core::MeshSourceKind::Box, {0.5f, 0.5f, 0.5f}, true, 0.5f};
            break;
        case Primitive::Sphere:
            spec = {"Sphere", propSpawnMeshHandles_.sphereMesh, core::MeshSourceKind::Capsule, {0.5f, 0.0f, 0.0f}, true, 0.5f};
            break;
        case Primitive::Cylinder:
            spec = {"Cylinder", propSpawnMeshHandles_.cylinderMesh, core::MeshSourceKind::Box, {0.5f, 0.5f, 0.0f}, false, 0.5f};
            break;
        case Primitive::Plane:
            spec = {"Plane", propSpawnMeshHandles_.planeMesh, core::MeshSourceKind::Plane, {2.0f, 0.0f, 2.0f}, true, 0.01f};
            break;
        case Primitive::Torus:
            spec = {"Torus", propSpawnMeshHandles_.torusMesh, core::MeshSourceKind::Torus, {1.0f, 0.35f, 0.0f}, true, 0.35f};
            break;
    }

    glm::vec3 boundsMin(-spec.lift);
    glm::vec3 boundsMax(spec.lift);
    if (const core::Mesh* mesh = meshLibrary != nullptr ? meshLibrary->get(spec.mesh) : nullptr) {
        boundsMin = mesh->localBoundsMin();
        boundsMax = mesh->localBoundsMax();
    }
    const glm::vec3 spawnPos = spawnPointInFront(ecs, meshLibrary, boundsMin, boundsMax);
    core::EntityId entity = ecs.createEntity(spec.name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = spawnPos;
    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = spec.mesh;
    renderable.baseColor = {0.64f, 0.64f, 0.66f, 1.0f};
    renderable.metallic = 0.0f;
    renderable.roughness = 0.7f;
    if (spec.hasMeshSource) {
        auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
        meshSource.kind = spec.sourceKind;
        meshSource.params = spec.params;
    }
    explorer.setSelected(entity);
    return entity;
}

void ViewportPanel::focusOn(core::ECS& ecs, core::MeshLibrary& meshLibrary, const std::vector<core::EntityId>& entities) {
    glm::vec3 boundsMin(FLT_MAX), boundsMax(-FLT_MAX);
    bool any = false;
    for (core::EntityId entity : entities) {
        if (!ecs.raw().valid(entity) || ecs.tryGetComponent<core::Transform>(entity) == nullptr) continue;
        const glm::mat4 world = core::hierarchy::computeWorldMatrix(ecs, entity);
        glm::vec3 localMin(-0.5f), localMax(0.5f);
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) {
            if (const core::Mesh* mesh = meshLibrary.get(renderable->meshHandle)) {
                localMin = mesh->localBoundsMin();
                localMax = mesh->localBoundsMax();
            }
        }
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner((i & 1) ? localMax.x : localMin.x, (i & 2) ? localMax.y : localMin.y,
                                   (i & 4) ? localMax.z : localMin.z);
            const glm::vec3 p = glm::vec3(world * glm::vec4(corner, 1.0f));
            boundsMin = glm::min(boundsMin, p);
            boundsMax = glm::max(boundsMax, p);
        }
        any = true;
    }
    if (!any) return;
    const float radius = std::max(0.5f, glm::length(boundsMax - boundsMin) * 0.5f);
    focusTarget_ = (boundsMin + boundsMax) * 0.5f;
    focusDistance_ = std::clamp(radius / std::tan(glm::radians(camera_.verticalFovDegrees) * 0.5f) * 1.1f, 2.0f, 400.0f);
    focusActive_ = true;
    orbitActive_ = false;
}

void ViewportPanel::updateFocus(float deltaTime) {
    if (!focusActive_ || dragging_) {
        focusActive_ = false;
        orbitActive_ = false;
        return;
    }
    const float blend = 1.0f - std::exp(-deltaTime * 14.0f);
    if (orbitActive_) {
        const float yawDelta = std::remainder(orbitYawGoal_ - camera_.yawDegrees, 360.0f);
        const float pitchDelta = orbitPitchGoal_ - camera_.pitchDegrees;
        camera_.yawDegrees += yawDelta * blend;
        camera_.pitchDegrees += pitchDelta * blend;
        if (std::fabs(yawDelta) < 0.05f && std::fabs(pitchDelta) < 0.05f) {
            camera_.yawDegrees = orbitYawGoal_;
            camera_.pitchDegrees = orbitPitchGoal_;
            orbitActive_ = false;
        }
        camera_.position = focusTarget_ - camera_.forward() * focusDistance_;
        if (!orbitActive_) focusActive_ = false;
        return;
    }
    const glm::vec3 goal = focusTarget_ - camera_.forward() * focusDistance_;
    camera_.position += (goal - camera_.position) * blend;
    if (glm::length(goal - camera_.position) < 0.01f) {
        camera_.position = goal;
        focusActive_ = false;
    }
}

void ViewportPanel::resetCamera() {
    focusActive_ = false;
    camera_.position = {0.0f, 8.0f, -10.0f};
    camera_.yawDegrees = 90.0f;
    camera_.pitchDegrees = -22.0f;
}

void ViewportPanel::drawViewCube(ImVec2 imageOrigin, ImVec2 imageSize) {
    if (!showViewCube_ || imageSize.x < kViewCubeSize * 2.5f || imageSize.y < kViewCubeSize * 2.0f) return;

    struct Face {
        glm::vec3 normal;
        glm::vec3 u;
        glm::vec3 v;
        const char* label;
    };
    static const std::array<Face, 6> kFaces = {{
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, "TOP"},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, "BOTTOM"},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, "FRONT"},
        {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}, "BACK"},
        {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}, "RIGHT"},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, "LEFT"},
    }};

    const ImVec2 origin = viewCubeOrigin(imageOrigin, imageSize);
    const ImVec2 center(origin.x + kViewCubeSize * 0.5f, origin.y + kViewCubeSize * 0.5f);
    const float half = kViewCubeSize * 0.24f;
    const glm::mat3 rotation(camera_.viewMatrix());
    auto project = [&](const glm::vec3& p) {
        const glm::vec3 view = rotation * p;
        return ImVec2(center.x + view.x * half, center.y - view.y * half);
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool mouseInside = ImGui::IsWindowHovered() && mouse.x >= origin.x && mouse.y >= origin.y &&
                             mouse.x <= origin.x + kViewCubeSize && mouse.y <= origin.y + kViewCubeSize;
    if (mouseInside) drawList->AddCircleFilled(center, kViewCubeSize * 0.5f, IM_COL32(16, 18, 24, 70), 48);

    const std::array<std::pair<glm::vec3, ImU32>, 3> axes = {{
        {{1, 0, 0}, IM_COL32(232, 72, 72, 255)},
        {{0, 1, 0}, IM_COL32(88, 200, 96, 255)},
        {{0, 0, 1}, IM_COL32(72, 140, 240, 255)},
    }};
    const char* axisNames[] = {"X", "Y", "Z"};
    for (size_t i = 0; i < axes.size(); ++i) {
        const ImVec2 tip = project(axes[i].first * 1.8f);
        drawList->AddLine(center, tip, axes[i].second, 2.0f);
        drawList->AddCircleFilled(tip, 6.5f, axes[i].second, 16);
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, axisNames[i]);
        drawList->AddText(ImGui::GetFont(), 10.0f, ImVec2(tip.x - textSize.x * 0.5f, tip.y - textSize.y * 0.5f),
                          IM_COL32(255, 255, 255, 255), axisNames[i]);
    }

    int hovered = -1;
    std::array<std::array<ImVec2, 4>, 6> quads{};
    std::array<float, 6> facing{};
    for (size_t i = 0; i < kFaces.size(); ++i) {
        const Face& face = kFaces[i];
        facing[i] = (rotation * face.normal).z;
        quads[i] = {project(face.normal - face.u - face.v), project(face.normal + face.u - face.v),
                    project(face.normal + face.u + face.v), project(face.normal - face.u + face.v)};
        if (facing[i] <= 0.02f || !mouseInside) continue;
        bool inside = true;
        float sign = 0.0f;
        for (int k = 0; k < 4 && inside; ++k) {
            const ImVec2 a = quads[i][k];
            const ImVec2 b = quads[i][(k + 1) % 4];
            const float cross = (b.x - a.x) * (mouse.y - a.y) - (b.y - a.y) * (mouse.x - a.x);
            if (sign == 0.0f) sign = cross;
            else if (cross * sign < 0.0f) inside = false;
        }
        if (inside) hovered = static_cast<int>(i);
    }

    for (size_t i = 0; i < kFaces.size(); ++i) {
        if (facing[i] <= 0.02f) continue;
        const float shade = 0.68f + 0.32f * facing[i];
        const ImU32 fill = static_cast<int>(i) == hovered
                               ? IM_COL32(86, 170, 255, 255)
                               : IM_COL32(static_cast<int>(212 * shade), static_cast<int>(219 * shade),
                                          static_cast<int>(232 * shade), 255);
        drawList->AddQuadFilled(quads[i][0], quads[i][1], quads[i][2], quads[i][3], fill);
        drawList->AddQuad(quads[i][0], quads[i][1], quads[i][2], quads[i][3], IM_COL32(38, 42, 52, 255), 1.3f);
        if (facing[i] > 0.3f) {
            const ImVec2 mid = project(kFaces[i].normal);
            const float fontSize = 10.0f;
            const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, kFaces[i].label);
            const int alpha = static_cast<int>(255.0f * std::clamp((facing[i] - 0.3f) / 0.2f, 0.0f, 1.0f));
            const ImU32 text = static_cast<int>(i) == hovered ? IM_COL32(255, 255, 255, alpha) : IM_COL32(30, 34, 44, alpha);
            drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(mid.x - textSize.x * 0.5f, mid.y - textSize.y * 0.5f),
                              text, kFaces[i].label);
        }
    }

    if (hovered >= 0) {
        ImGui::SetTooltip("View from %s", kFaces[static_cast<size_t>(hovered)].label);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const glm::vec3 forward = camera_.forward();
            float distance = 12.0f;
            if (forward.y < -0.05f) distance = std::clamp(-camera_.position.y / forward.y, 2.0f, 200.0f);
            focusTarget_ = camera_.position + forward * distance;
            focusDistance_ = distance;
            const glm::vec3 goalForward = -kFaces[static_cast<size_t>(hovered)].normal;
            if (std::fabs(goalForward.y) > 0.5f) {
                orbitYawGoal_ = camera_.yawDegrees;
                orbitPitchGoal_ = goalForward.y < 0.0f ? -89.0f : 89.0f;
            } else {
                orbitYawGoal_ = glm::degrees(std::atan2(goalForward.z, goalForward.x));
                orbitPitchGoal_ = 0.0f;
            }
            focusActive_ = true;
            orbitActive_ = true;
        }
    }
}

void ViewportPanel::drawStatusBar(ImDrawList* drawList, ImVec2 imageOrigin, ImVec2 imageSize, size_t selectionCount) {
    const char* tool = selectTool_ ? "Select"
                       : gizmoOperation_ == GizmoOperation::Translate ? "Move"
                       : gizmoOperation_ == GizmoOperation::Rotate    ? "Rotate"
                                                                      : "Scale";
    char text[160];
    if (selectionCount > 0) {
        std::snprintf(text, sizeof(text), "%s  \xc2\xb7  %zu selected  \xc2\xb7  F to focus", tool, selectionCount);
    } else {
        std::snprintf(text, sizeof(text), "%s  \xc2\xb7  Hold right mouse + WASD to fly  \xc2\xb7  Scroll to zoom", tool);
    }
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 min(imageOrigin.x + 10.0f, imageOrigin.y + imageSize.y - size.y - 18.0f);
    const ImVec2 max(min.x + size.x + 20.0f, min.y + size.y + 10.0f);
    if (max.x > imageOrigin.x + imageSize.x - 10.0f) return;
    drawList->AddRectFilled(min, max, IM_COL32(18, 18, 22, 170), (max.y - min.y) * 0.5f);
    drawList->AddText(ImVec2(min.x + 10.0f, min.y + 5.0f), IM_COL32(236, 238, 242, 235), text);
}

} // namespace engine::studio::panels
