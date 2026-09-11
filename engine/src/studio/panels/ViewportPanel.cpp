#include "studio/panels/ViewportPanel.hpp"

#include "studio/plugins/ModelImporterPlugin.hpp"
#include "studio/plugins/ModelingModePlugin.hpp"
#include "studio/plugins/MovieModePlugin.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
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
#include "core/WorldProp.hpp"
#include "studio/CreatorToolsSpawning.hpp"
#include "studio/StudioIcons.hpp"
#include "studio/plugins/PhysicsPreviewPlugin.hpp"

namespace engine::studio::panels {

namespace {
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
    if (!movieMode.showRailGizmo()) return;
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

void ViewportPanel::drawGroundGridOverlay(ImVec2 imageOrigin, ImVec2 imageSize) {
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

    // Matches buildBringUpScene()'s own 25x25 GroundPlane extent (halfWidth/
    // halfDepth = 12.5) -- a grid line every 1m, so the two read as one
    // consistent floor rather than the grid overshooting or undershooting
    // the real ground mesh underneath it.
    constexpr float kHalfExtent = 12.5f;
    constexpr float kStep = 1.0f;
    constexpr ImU32 kLineColor = IM_COL32(255, 255, 255, 35);
    constexpr ImU32 kAxisColor = IM_COL32(255, 255, 255, 90);
    int lineCount = static_cast<int>(kHalfExtent / kStep);
    for (int i = -lineCount; i <= lineCount; ++i) {
        float offset = static_cast<float>(i) * kStep;
        ImU32 color = (i == 0) ? kAxisColor : kLineColor;
        projectLine({-kHalfExtent, 0.0f, offset}, {kHalfExtent, 0.0f, offset}, color, (i == 0) ? 1.5f : 1.0f);
        projectLine({offset, 0.0f, -kHalfExtent}, {offset, 0.0f, kHalfExtent}, color, (i == 0) ? 1.5f : 1.0f);
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
            IM_COL32(255, 210, 90, 220), IM_COL32(255, 140, 90, 220), IM_COL32(255, 90, 90, 220)};
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

void ViewportPanel::draw(float deltaTime, VkDescriptorSet sceneTexture, VkExtent2D sceneTextureExtent,
                          core::ECS* ecs, core::MeshLibrary* meshLibrary, ExplorerPanel& explorer,
                          plugins::PhysicsPreviewPlugin* physicsPreview, const ViewportDebugContext& debugContext,
                          plugins::MovieModePlugin* movieMode, bool showEngineDebugOverlays,
                          plugins::ModelingModePlugin* modelingMode) {
    ImGuizmo::BeginFrame(); // once per ImGui frame -- see header comment

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    updateFreeFly(deltaTime);

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

    drawGroundGridOverlay(imageOrigin, imageSize);

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

    core::EntityId selected = explorer.selectedEntity();
    if (ecs != nullptr && selected != core::kNullEntity) {
        // Kronos ("3D DCC Modeling Suite" -- real sub-object raycast
        // picking): a real core::EditableMeshComponent on the selection
        // switches to Modeling Mode's own vertex/edge/face gizmo instead
        // of the whole-entity Translate/Rotate/Scale one -- see
        // drawSubObjectEditing()'s own comment on why never both at once.
        auto* editable = ecs->tryGetComponent<core::EditableMeshComponent>(selected);
        if (modelingMode != nullptr && editable != nullptr) {
            drawSubObjectEditing(*modelingMode, *ecs, selected, imageOrigin, imageSize);
        } else {
            drawGizmo(*ecs, selected, explorer.selectedEntities(), imageOrigin, imageSize);
        }
    }
    if (ecs != nullptr && meshLibrary != nullptr) {
        drawSelectionHighlight(*ecs, *meshLibrary, explorer.selectedEntities(), imageOrigin, imageSize);
    }

    // Click-to-select / drag-select-box -- only while free-flying isn't
    // consuming the mouse (right-drag) and there's an ECS+MeshLibrary to
    // pick against.
    if (ecs != nullptr && meshLibrary != nullptr && !dragging_) {
        handleSelection(*ecs, *meshLibrary, explorer, imageOrigin, imageSize);
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

    // Camera status -- bottom-left, out of the toolbar's way (top-left,
    // see below).
    char overlay[192];
    std::snprintf(overlay, sizeof(overlay), "Camera pos (%.1f, %.1f, %.1f)  yaw %.0f  pitch %.0f  |  Right-drag + WASD/QE to fly",
                  camera_.position.x, camera_.position.y, camera_.position.z, camera_.yawDegrees, camera_.pitchDegrees);
    drawList->AddText(ImVec2(imageOrigin.x + 10, imageOrigin.y + imageSize.y - 22), IM_COL32(210, 212, 218, 220), overlay);

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
        if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmoOperation_ = GizmoOperation::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmoOperation_ = GizmoOperation::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoOperation_ = GizmoOperation::Scale;

        // Kronos ("Developer Velocity Sprint" -- "Drop-to-Ground Shortcut
        // (End Key)"): same gating as W/E/R above (hovered, not fighting
        // an active gizmo drag or a focused text field).
        if (ImGui::IsKeyPressed(ImGuiKey_End) && ecs != nullptr && meshLibrary != nullptr &&
            explorer.selectedEntity() != core::kNullEntity) {
            dropSelectedToGround(*ecs, *meshLibrary, explorer.selectedEntity());
        }
    }

    // Drawn on a split channel so the background panel (whose size
    // depends on the buttons' laid-out extent, not known until after
    // EndGroup()) can still be painted *behind* content already recorded
    // into the same window draw list -- the standard ImGui technique for
    // "size a background to fit content I haven't measured yet" without
    // a two-pass layout.
    ImDrawListSplitter splitter;
    splitter.Split(drawList, 2);
    splitter.SetCurrentChannel(drawList, 1);

    constexpr float kIconButtonSize = 28.0f;
    constexpr float kToolbarPadding = 6.0f;
    ImVec2 iconSize(kIconButtonSize, kIconButtonSize);

    // Adobe-style persistent left tool column, not a horizontal bar --
    // every control is always present in both modes (no feature gating);
    // Beginner adds an inline text label after each control (clearer
    // breakdown for newcomers), Advanced stays icon-only/compact to
    // maximize viewport space.
    auto beginnerLabel = [&](const char* text) {
        if (!advancedMode_) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text);
        }
    };

    ImGui::SetCursorScreenPos(ImVec2(imageOrigin.x + 8.0f + kToolbarPadding, imageOrigin.y + 8.0f + kToolbarPadding));
    ImGui::BeginGroup();
    if (iconButton("gizmo_translate", Icon::Translate, iconSize, gizmoOperation_ == GizmoOperation::Translate,
                    "Translate (W) -- move the selected object.")) {
        gizmoOperation_ = GizmoOperation::Translate;
    }
    beginnerLabel("Move (W)");
    if (iconButton("gizmo_rotate", Icon::Rotate, iconSize, gizmoOperation_ == GizmoOperation::Rotate,
                    "Rotate (E) -- turn the selected object.")) {
        gizmoOperation_ = GizmoOperation::Rotate;
    }
    beginnerLabel("Rotate (E)");
    if (iconButton("gizmo_scale", Icon::Scale, iconSize, gizmoOperation_ == GizmoOperation::Scale,
                    "Scale (R) -- resize the selected object.")) {
        gizmoOperation_ = GizmoOperation::Scale;
    }
    beginnerLabel("Scale (R)");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    bool worldSpace = gizmoSpace_ == GizmoSpace::World;
    if (iconButton("gizmo_space", worldSpace ? Icon::WorldSpace : Icon::LocalSpace, iconSize, false,
                    worldSpace ? "World Space -- gizmo axes stay aligned to the world (click for Local)"
                               : "Local Space -- gizmo axes follow the object's own rotation (click for World)")) {
        gizmoSpace_ = worldSpace ? GizmoSpace::Local : GizmoSpace::World;
    }
    beginnerLabel(worldSpace ? "World Space" : "Local Space");
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    // Grid Snap and Angle Snap are independently toggleable and apply
    // to Translate/Rotate respectively regardless of which is
    // currently selected, so switching gizmo modes never silently
    // changes what's snapping.
    if (iconButton("grid_snap", Icon::Snap, iconSize, gridSnapEnabled_,
                    "Grid Snap -- while on, Translate moves in fixed steps instead of freely.")) {
        gridSnapEnabled_ = !gridSnapEnabled_;
    }
    beginnerLabel(gridSnapEnabled_ ? "Grid Snap: On" : "Grid Snap: Off");
    ImGui::SetNextItemWidth(kIconButtonSize);
    static constexpr float kGridSnapPresets[] = {0.25f, 1.0f, 5.0f};
    char gridPresetLabel[16];
    std::snprintf(gridPresetLabel, sizeof(gridPresetLabel), "%.2fm", translateSnap_);
    if (ImGui::BeginCombo("##grid_snap_preset", gridPresetLabel)) {
        for (float preset : kGridSnapPresets) {
            char label[16];
            std::snprintf(label, sizeof(label), "%.2fm", preset);
            bool selected = std::fabs(translateSnap_ - preset) < 0.001f;
            if (ImGui::Selectable(label, selected)) translateSnap_ = preset;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Grid Snap increment (meters) -- how far Translate moves per step while Grid Snap is on.");
    }
    beginnerLabel("Grid Snap step");

    if (iconButton("angle_snap", Icon::Snap, iconSize, angleSnapEnabled_,
                    "Angle Snap -- while on, Rotate turns in fixed steps instead of freely.")) {
        angleSnapEnabled_ = !angleSnapEnabled_;
    }
    beginnerLabel(angleSnapEnabled_ ? "Angle Snap: On" : "Angle Snap: Off");
    ImGui::SetNextItemWidth(kIconButtonSize);
    static constexpr float kAngleSnapPresets[] = {15.0f, 45.0f, 90.0f};
    char anglePresetLabel[16];
    std::snprintf(anglePresetLabel, sizeof(anglePresetLabel), "%.0f%s", rotateSnapDegrees_, "\xc2\xb0"); // UTF-8 degree sign
    if (ImGui::BeginCombo("##angle_snap_preset", anglePresetLabel)) {
        for (float preset : kAngleSnapPresets) {
            char label[16];
            std::snprintf(label, sizeof(label), "%.0f%s", preset, "\xc2\xb0");
            bool selected = std::fabs(rotateSnapDegrees_ - preset) < 0.001f;
            if (ImGui::Selectable(label, selected)) rotateSnapDegrees_ = preset;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Angle Snap increment (degrees) -- how far Rotate turns per step while Angle Snap is on.");
    }
    beginnerLabel("Angle Snap step");

    if (iconButton("scale_snap", Icon::Snap, iconSize, scaleSnapEnabled_,
                    "Scale Snap -- while on, Scale changes in fixed steps instead of freely.")) {
        scaleSnapEnabled_ = !scaleSnapEnabled_;
    }
    beginnerLabel(scaleSnapEnabled_ ? "Scale Snap: On" : "Scale Snap: Off");
    ImGui::SetNextItemWidth(kIconButtonSize);
    ImGui::DragFloat("##scale_snap_val", &scaleSnap_, 0.01f, 0.01f, 10.0f, "%.2f");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Scale Snap increment -- how far Scale changes per step while Scale Snap is on. Drag to adjust.");
    }
    beginnerLabel("Scale Snap step");

    // Kronos ("Clean Viewport & Mesh Import Pipeline"): real "Import 3D
    // Asset..."/"Add Primitive" controls -- see setAssetTools()'s own
    // header comment for exactly which modes enable this row at all.
    if (showAssetTools_) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        if (modelImporterPlugin_ != nullptr) {
            if (iconButton("import_asset", Icon::Folder, iconSize, false,
                            "Import 3D Asset -- opens a real file browser for glTF/.OBJ/.FBX.")) {
                modelImporterPlugin_->setOpen(true);
                modelImporterPlugin_->browseForFile();
            }
            beginnerLabel("Import 3D Asset...");
        }

        ImGui::SetNextItemWidth(kIconButtonSize);
        if (ImGui::BeginCombo("##add_primitive", "+", ImGuiComboFlags_NoArrowButton)) {
            // Real, honest fallback spawn point: 4m in front of the
            // camera, never below the y=0 ground grid -- so a repeated
            // "Add Primitive" click doesn't keep stacking every new shape
            // exactly on top of the last one at the world origin.
            glm::vec3 spawnPos = camera_.position + camera_.forward() * 4.0f;
            spawnPos.y = std::max(spawnPos.y, 0.5f);
            // Prefer landing on the actual y=0 ground plane the camera is
            // looking at -- the fixed 4m-forward point above lands wherever
            // that happens to be relative to the camera's current pitch,
            // which is often well above the visible ground when looking
            // down a long shot (e.g. framing a camera rail), leaving a new
            // primitive's selection box floating with nothing visibly under
            // it. Only used when the camera is actually looking down at the
            // ground steeply enough for the hit point to be sane (not
            // absurdly far away, not behind the camera).
            const glm::vec3 forward = camera_.forward();
            constexpr float kMaxGroundSpawnDistance = 40.0f;
            if (forward.y < -0.05f) {
                const float t = -camera_.position.y / forward.y;
                if (t > 0.0f && t <= kMaxGroundSpawnDistance) {
                    spawnPos = camera_.position + forward * t;
                }
            }

            auto spawnPrimitive = [&](const char* entityName, uint32_t meshHandle, core::MeshSourceKind kind,
                                        glm::vec3 params, bool hasMeshSource) {
                if (ecs == nullptr) return;
                core::EntityId entity = ecs->createEntity(entityName);
                if (auto* transform = ecs->tryGetComponent<core::Transform>(entity)) {
                    transform->position = spawnPos;
                }
                auto& renderable = ecs->addComponent<core::Renderable>(entity);
                renderable.meshHandle = meshHandle;
                renderable.baseColor = {0.82f, 0.82f, 0.85f, 1.0f};
                renderable.metallic = 0.1f;
                renderable.roughness = 0.6f;
                if (hasMeshSource) {
                    auto& meshSource = ecs->addComponent<core::MeshSource>(entity);
                    meshSource.kind = kind;
                    meshSource.params = params;
                }
                // Deliberately render-only on spawn -- no auto-attached
                // EditableMeshComponent. Sub-object edit tools (Ctrl+Click
                // vertex/edge/face pick + gizmo drag, sculpt brushes,
                // Modeling Mode's extrude/inset/bevel/CSG) key off that
                // component, so a plain "Add Primitive" shape stays a
                // normal primitive until the user deliberately opts it into
                // real 3D modeling via ModelingModePlugin's own "Start
                // Editing" button -- see that plugin's header comment for
                // why Box is the one shape it seeds identically today.
                explorer.setSelected(entity);
            };

            if (ImGui::Selectable("Sphere")) {
                // See WorldPropSpawnMeshHandles::sphereMesh's own
                // comment -- a real Capsule with halfHeight=0.
                spawnPrimitive("Sphere", propSpawnMeshHandles_.sphereMesh, core::MeshSourceKind::Capsule,
                                {0.5f, 0.0f, 0.0f}, true);
            }
            if (ImGui::Selectable("Cube")) {
                spawnPrimitive("Cube", propSpawnMeshHandles_.boxMesh, core::MeshSourceKind::Box,
                                {0.5f, 0.5f, 0.5f}, true);
            }
            if (ImGui::Selectable("Cylinder")) {
                // No MeshSourceKind::Cylinder -- see Mesh::createCylinder()'s
                // own comment. radius=0.5, halfHeight=0.5, matching the GPU
                // mesh built in StudioApp.cpp for propSpawnMeshHandles_.cylinderMesh.
                spawnPrimitive("Cylinder", propSpawnMeshHandles_.cylinderMesh, core::MeshSourceKind::Box,
                                {0.5f, 0.5f, 0.0f}, false);
            }
            if (ImGui::Selectable("Plane")) {
                spawnPrimitive("Plane", propSpawnMeshHandles_.planeMesh, core::MeshSourceKind::Plane,
                                {2.0f, 0.0f, 2.0f}, true);
            }
            if (ImGui::Selectable("Torus")) {
                spawnPrimitive("Torus", propSpawnMeshHandles_.torusMesh, core::MeshSourceKind::Torus,
                                {1.0f, 0.35f, 0.0f}, true);
            }
            ImGui::EndCombo();
        }
        beginnerLabel("Add Primitive");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (ImGui::SmallButton(advancedMode_ ? "Pro" : "Beg")) advancedMode_ = !advancedMode_;
    beginnerLabel("Beginner Mode");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip(advancedMode_ ? "Professional mode -- compact icons only. Click for Beginner (adds "
                                           "text labels and richer tooltips; every tool stays available in both)."
                                         : "Beginner mode -- every tool labeled for clarity. Click for "
                                           "Professional for a compact, icon-only layout (same tools, less space).");
    }
    ImGui::EndGroup();

    ImVec2 groupMin = ImGui::GetItemRectMin();
    ImVec2 groupMax = ImGui::GetItemRectMax();
    splitter.SetCurrentChannel(drawList, 0);
    ImVec2 bgMin(groupMin.x - kToolbarPadding, groupMin.y - kToolbarPadding);
    ImVec2 bgMax(groupMax.x + kToolbarPadding, groupMax.y + kToolbarPadding);
    float rounding = ImGui::GetStyle().FrameRounding + 2.0f;
    drawList->AddRectFilled(bgMin, bgMax, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.92f), rounding);
    drawList->AddRect(bgMin, bgMax, ImGui::GetColorU32(ImGuiCol_Border), rounding);
    splitter.Merge(drawList);

    // Advanced-only rows now sit to the RIGHT of the vertical tool
    // column (not stacked beneath it -- the column is tall, so "beneath"
    // would push these rows far down or off-screen). Same top Y as the
    // column's own start.
    float rightColumnX = bgMax.x + 6.0f;
    float lastToolbarRowBottomY = imageOrigin.y + 8.0f;

    if (advancedMode_ && physicsPreview != nullptr) {
        ImDrawListSplitter splitter2;
        splitter2.Split(drawList, 2);
        splitter2.SetCurrentChannel(drawList, 1);

        float row2Y = lastToolbarRowBottomY;
        ImGui::SetCursorScreenPos(ImVec2(rightColumnX + kToolbarPadding, row2Y + kToolbarPadding));
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Physics Debug:");
        ImGui::SameLine();
        ImGui::Checkbox("Colliders##physdbg", &physicsPreview->showColliders);
        ImGui::SameLine();
        ImGui::Checkbox("Contacts##physdbg", &physicsPreview->showContacts);
        ImGui::SameLine();
        ImGui::Checkbox("Raycasts##physdbg", &physicsPreview->showRaycasts);
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(6.0f, 0.0f));
        ImGui::SameLine();
        bool canCastRay = physicsPreview->isPlaying();
        ImGui::BeginDisabled(!canCastRay);
        if (ImGui::Button("Cast Test Ray (Camera Forward)##physdbg")) {
            physicsPreview->castTestRay(camera_.position, camera_.forward(), 1000.0f);
        }
        ImGui::EndDisabled();
        ImGui::EndGroup();

        ImVec2 g2Min = ImGui::GetItemRectMin();
        ImVec2 g2Max = ImGui::GetItemRectMax();
        splitter2.SetCurrentChannel(drawList, 0);
        ImVec2 bg2Min(g2Min.x - kToolbarPadding, g2Min.y - kToolbarPadding);
        ImVec2 bg2Max(g2Max.x + kToolbarPadding, g2Max.y + kToolbarPadding);
        drawList->AddRectFilled(bg2Min, bg2Max, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.92f), rounding);
        drawList->AddRect(bg2Min, bg2Max, ImGui::GetColorU32(ImGuiCol_Border), rounding);
        splitter2.Merge(drawList);
        lastToolbarRowBottomY = bg2Max.y + 6.0f;
    }

    // Sprint 8 debug-overlay toolbar (task category 2) -- same
    // right-of-column technique as the physics row above, stacked
    // beneath it if drawn. Kronos ("Modular Executable Targets"): gated
    // on showEngineDebugOverlays -- see that parameter's own header
    // comment -- and now also on advancedMode_.
    if (advancedMode_ && showEngineDebugOverlays) {
        ImDrawListSplitter splitter3;
        splitter3.Split(drawList, 2);
        splitter3.SetCurrentChannel(drawList, 1);

        float row3Y = lastToolbarRowBottomY;
        ImGui::SetCursorScreenPos(ImVec2(rightColumnX + kToolbarPadding, row3Y + kToolbarPadding));
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Debug Overlays:");
        ImGui::SameLine();
        ImGui::Checkbox("Bounds##sprint8dbg", &showBoundingBoxes_);
        ImGui::SameLine();
        ImGui::BeginDisabled(debugContext.terrain == nullptr);
        ImGui::Checkbox("Terrain Streaming##sprint8dbg", &showTerrainStreaming_);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(debugContext.renderer == nullptr);
        ImGui::Checkbox("CSM Cascades##sprint8dbg", &showCascades_);
        ImGui::EndDisabled();
        ImGui::EndGroup();

        ImVec2 g3Min = ImGui::GetItemRectMin();
        ImVec2 g3Max = ImGui::GetItemRectMax();
        splitter3.SetCurrentChannel(drawList, 0);
        ImVec2 bg3Min(g3Min.x - kToolbarPadding, g3Min.y - kToolbarPadding);
        ImVec2 bg3Max(g3Max.x + kToolbarPadding, g3Max.y + kToolbarPadding);
        drawList->AddRectFilled(bg3Min, bg3Max, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.92f), rounding);
        drawList->AddRect(bg3Min, bg3Max, ImGui::GetColorU32(ImGuiCol_Border), rounding);
        splitter3.Merge(drawList);
    }

    ImGui::End();
}

} // namespace engine::studio::panels
