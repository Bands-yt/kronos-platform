#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine::core {

// Shared by engine_runtime (a real gameplay camera would drive this from
// script/character state -- not built yet, see Scripting.hpp's Instance
// TODO) and Studio's free-fly editor camera (studio/panels/ViewportPanel.hpp
// already tracks position/yaw/pitch itself; this is the matrix-math half
// that turns that state into view/projection matrices for the SceneUBO).
class Camera {
public:
    glm::vec3 position{0.0f, 2.0f, 6.0f};
    float yawDegrees = -90.0f;
    float pitchDegrees = -10.0f;
    // Kronos ("Real-Time Rendering Evolved" trailer): real camera roll --
    // "slight tilt for cinematic framing" (the user's own SUNSET prompt).
    // 0.0f (the default) reproduces the exact old fixed-up-vector behavior
    // for every existing caller (Studio's free-fly camera, every other
    // engine_runtime mode) -- see viewMatrix()'s own comment.
    float rollDegrees = 0.0f;
    float verticalFovDegrees = 60.0f;
    float nearPlane = 0.05f;
    float farPlane = 500.0f;

    [[nodiscard]] glm::vec3 forward() const {
        float yaw = glm::radians(yawDegrees);
        float pitch = glm::radians(pitchDegrees);
        return glm::normalize(glm::vec3{
            std::cos(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::sin(yaw) * std::cos(pitch),
        });
    }

    [[nodiscard]] glm::mat4 viewMatrix() const {
        // Real, rolled up-vector -- rotating the standard world-up
        // (0,1,0) around the camera's own forward axis by rollDegrees;
        // 0.0f leaves it exactly (0,1,0), so glm::lookAt's result is
        // byte-identical to before rollDegrees existed for any caller
        // that never sets it.
        glm::vec3 up = glm::vec3{0.0f, 1.0f, 0.0f};
        if (rollDegrees != 0.0f) {
            up = glm::vec3(glm::rotate(glm::mat4(1.0f), glm::radians(rollDegrees), forward()) * glm::vec4(up, 0.0f));
        }
        return glm::lookAt(position, position + forward(), up);
    }

    // Vulkan's clip space has Y pointing down and Z in [0,1] (not OpenGL's
    // [-1,1]) -- glm::perspective assumes the OpenGL convention, so the Y
    // flip below is required, not stylistic. Getting this wrong is the
    // single most common "my Vulkan scene renders upside down" bug.
    [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const {
        glm::mat4 proj = glm::perspective(glm::radians(verticalFovDegrees), aspectRatio, nearPlane, farPlane);
        proj[1][1] *= -1.0f;
        return proj;
    }

    // Kronos ("Vulkan Compute PBR Painter" -- v0.4.0 Creator Suite, live
    // viewport picking): the same real "un-project the near/far NDC
    // points through the inverse view-projection matrix" ray
    // construction studio::ViewportPanel::computeMouseRay() already
    // established for the main scene's click-to-select, factored onto
    // Camera itself so any camera (studio::PreviewScene's orbit camera
    // included) can build the same real world-space ray without a
    // second, drift-prone reimplementation. `ndcX`/`ndcY` are already in
    // NDC (-1..1, Y up) -- the caller converts its own mouse/image
    // coordinates first, matching computeMouseRay()'s own convention.
    void screenPointToRay(float ndcX, float ndcY, float aspectRatio, glm::vec3& outOrigin,
                           glm::vec3& outDirection) const {
        glm::mat4 invViewProj = glm::inverse(projectionMatrix(aspectRatio) * viewMatrix());

        // NDC z=0 is the near plane, z=1 the far plane -- this project's
        // Vulkan clip-space convention (GLM_FORCE_DEPTH_ZERO_TO_ONE), not
        // GLM's own OpenGL-style default -- see computeMouseRay()'s own
        // comment for why getting this backwards would still compile and
        // still produce *a* ray, just not one through the cursor.
        glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        nearPoint /= nearPoint.w;
        glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        farPoint /= farPoint.w;

        outOrigin = glm::vec3(nearPoint);
        outDirection = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));
    }
};

} // namespace engine::core
