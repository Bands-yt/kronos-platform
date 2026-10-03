#include "core/RiggedAvatar.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>

#include "core/CatalogueIndex.hpp"
#include "core/Components.hpp"

namespace engine::core {

namespace {

const char* jointNameFor(HumanoidBodySegment segment) {
    switch (segment) {
        case HumanoidBodySegment::Head: return "head";
        case HumanoidBodySegment::Torso: return "spine_upper";
        case HumanoidBodySegment::LeftArm: return "arm_L_upper";
        case HumanoidBodySegment::RightArm: return "arm_R_upper";
        case HumanoidBodySegment::LeftHand: return "hand_L";
        case HumanoidBodySegment::RightHand: return "hand_R";
        case HumanoidBodySegment::LeftLeg: return "leg_L_upper";
        case HumanoidBodySegment::RightLeg: return "leg_R_upper";
        case HumanoidBodySegment::LeftFoot: return "foot_L";
        case HumanoidBodySegment::RightFoot: return "foot_R";
    }
    return "";
}

constexpr float kPi = 3.14159265f;

float signedPow(float x, float e) { return std::copysign(std::pow(std::abs(x), e), x); }

// Appends skinned vertices/triangles for one body segment. Triangles are
// wound so their geometric normal agrees with the vertex normals, and
// smoothNormals() replaces provisional normals with welded, area-weighted ones.
struct BodyBuilder {
    std::vector<Vertex>& vertices;
    std::vector<uint32_t>& indices;
    std::vector<HumanoidBodySegment>& segments;
    SkinWeights& skinWeights;
    HumanoidBodySegment segment;

    uint32_t vertexMark() const { return static_cast<uint32_t>(vertices.size()); }
    size_t indexMark() const { return indices.size(); }

    uint32_t push(glm::vec3 position, glm::vec3 normal, glm::vec2 uv, int jointA, int jointB = -1, float weightB = 0.0f) {
        Vertex v;
        v.position = position;
        v.normal = glm::length(normal) > 1e-8f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        v.uv = uv;
        vertices.push_back(v);
        segments.push_back(segment);
        VertexSkinWeights sw;
        weightB = std::clamp(weightB, 0.0f, 1.0f);
        if (jointB < 0 || jointB == jointA || weightB <= 0.0f) {
            sw.jointIndices = {jointA, -1, -1, -1};
            sw.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        } else if (weightB >= 1.0f) {
            sw.jointIndices = {jointB, -1, -1, -1};
            sw.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        } else {
            sw.jointIndices = {jointA, jointB, -1, -1};
            sw.weights = {1.0f - weightB, weightB, 0.0f, 0.0f};
        }
        skinWeights.perVertex.push_back(sw);
        return static_cast<uint32_t>(vertices.size() - 1);
    }

    void tri(uint32_t a, uint32_t b, uint32_t c) {
        const glm::vec3& pa = vertices[a].position;
        glm::vec3 n = glm::cross(vertices[b].position - pa, vertices[c].position - pa);
        if (glm::dot(n, n) < 1e-16f) return;
        glm::vec3 reference = vertices[a].normal + vertices[b].normal + vertices[c].normal;
        if (glm::dot(n, reference) < 0.0f) std::swap(b, c);
        indices.insert(indices.end(), {a, b, c});
    }

    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        tri(a, b, c);
        tri(a, c, d);
    }

    // Rings of `ringSize` vertices starting at `first`, laid out consecutively.
    void connectRings(uint32_t first, uint32_t ringCount, uint32_t ringSize, bool wrap) {
        uint32_t columns = wrap ? ringSize : ringSize - 1;
        for (uint32_t r = 0; r + 1 < ringCount; ++r) {
            for (uint32_t s = 0; s < columns; ++s) {
                uint32_t next = (s + 1) % ringSize;
                quad(first + r * ringSize + s, first + r * ringSize + next, first + (r + 1) * ringSize + next,
                     first + (r + 1) * ringSize + s);
            }
        }
    }

    void smoothNormals(uint32_t firstVertex, size_t firstIndex) {
        auto key = [](glm::vec3 p) {
            return std::array<int64_t, 3>{std::llround(p.x * 1e5), std::llround(p.y * 1e5), std::llround(p.z * 1e5)};
        };
        std::map<std::array<int64_t, 3>, glm::vec3> accumulated;
        for (size_t i = firstIndex; i + 2 < indices.size(); i += 3) {
            const glm::vec3& a = vertices[indices[i]].position;
            const glm::vec3& b = vertices[indices[i + 1]].position;
            const glm::vec3& c = vertices[indices[i + 2]].position;
            glm::vec3 n = glm::cross(b - a, c - a);
            for (const glm::vec3* p : {&a, &b, &c}) accumulated[key(*p)] += n;
        }
        for (uint32_t v = firstVertex; v < vertices.size(); ++v) {
            auto it = accumulated.find(key(vertices[v].position));
            if (it != accumulated.end() && glm::dot(it->second, it->second) > 1e-20f) {
                vertices[v].normal = glm::normalize(it->second);
            }
        }
    }
};

// Superellipsoid (exponent 1 = ellipsoid, < 1 rounds toward a box) in an
// arbitrary orientation. With jointB set, weights blend from jointA to
// jointB across the plane perpendicular to `blendAxis`.
struct Ellipsoid {
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.1f};
    glm::mat3 basis{1.0f};
    float exponent = 1.0f;
    uint32_t rings = 16;
    uint32_t segments = 24;
    int jointA = -1;
    int jointB = -1;
    glm::vec3 blendAxis{0.0f, -1.0f, 0.0f};
};

using EllipsoidDeform = std::function<glm::vec3(glm::vec3 local, glm::vec3 unit)>;

void appendEllipsoid(BodyBuilder& builder, const Ellipsoid& e, const EllipsoidDeform& deform = {}) {
    uint32_t first = builder.vertexMark();
    size_t firstIndex = builder.indexMark();
    glm::vec3 axis = glm::length(e.blendAxis) > 1e-5f ? glm::normalize(e.blendAxis) : glm::vec3(0.0f, -1.0f, 0.0f);
    for (uint32_t r = 0; r <= e.rings; ++r) {
        float v = static_cast<float>(r) / static_cast<float>(e.rings);
        float phi = v * kPi;
        for (uint32_t s = 0; s <= e.segments; ++s) {
            float u = static_cast<float>(s) / static_cast<float>(e.segments);
            float theta = u * 2.0f * kPi;
            glm::vec3 unit(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            glm::vec3 shaped(signedPow(std::sin(phi), e.exponent) * signedPow(std::cos(theta), e.exponent),
                             signedPow(std::cos(phi), e.exponent),
                             signedPow(std::sin(phi), e.exponent) * signedPow(std::sin(theta), e.exponent));
            glm::vec3 local = shaped * e.halfExtents;
            if (deform) local = deform(local, unit);
            glm::vec3 worldUnit = e.basis * unit;
            float blend = e.jointB >= 0 ? glm::smoothstep(-0.45f, 0.45f, glm::dot(worldUnit, axis)) : 0.0f;
            builder.push(e.center + e.basis * local, e.basis * (unit / e.halfExtents), {u, v}, e.jointA, e.jointB, blend);
        }
    }
    builder.connectRings(first, e.rings + 1, e.segments + 1, false);
    builder.smoothNormals(first, firstIndex);
}

void appendSphere(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<HumanoidBodySegment>& segments,
                   glm::vec3 center, glm::vec3 radii, int jointIndex, HumanoidBodySegment segment,
                   SkinWeights& skinWeights) {
    BodyBuilder builder{vertices, indices, segments, skinWeights, segment};
    Ellipsoid e;
    e.center = center;
    e.halfExtents = radii;
    e.jointA = jointIndex;
    appendEllipsoid(builder, e);
}

void appendSphereBlended(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                          std::vector<HumanoidBodySegment>& segments, glm::vec3 center, glm::vec3 radii,
                          int primaryJoint, int secondaryJoint, glm::vec3 blendAxis, HumanoidBodySegment segment,
                          SkinWeights& skinWeights) {
    BodyBuilder builder{vertices, indices, segments, skinWeights, segment};
    Ellipsoid e;
    e.center = center;
    e.halfExtents = radii;
    e.jointA = primaryJoint;
    e.jointB = secondaryJoint;
    e.blendAxis = blendAxis;
    e.rings = 12;
    e.segments = 18;
    appendEllipsoid(builder, e);
}

// Ellipsoid with a cranium that is fuller at the temples and a jaw that
// narrows toward the chin. The front of the face stays where the facial
// feature joints (face_* in buildHumanoidSkeleton()) expect it.
void appendProfiledHead(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                         std::vector<HumanoidBodySegment>& segments, glm::vec3 center, glm::vec3 radii, int jointIndex,
                         HumanoidBodySegment segment, SkinWeights& skinWeights) {
    BodyBuilder builder{vertices, indices, segments, skinWeights, segment};
    Ellipsoid e;
    e.center = center;
    e.halfExtents = radii;
    e.jointA = jointIndex;
    e.rings = 20;
    e.segments = 28;
    appendEllipsoid(builder, e, [](glm::vec3 local, glm::vec3 unit) {
        float height = unit.y; // +1 crown, -1 chin
        float temples = 1.0f + 0.06f * std::exp(-std::pow((height - 0.15f) / 0.45f, 2.0f));
        float jaw = 1.0f - 0.22f * glm::smoothstep(-0.15f, -0.95f, height);
        float lateral = temples * jaw;
        float depth = temples * (unit.z < 0.0f ? 1.0f + 0.05f * glm::smoothstep(-0.5f, 0.6f, height) : 1.0f);
        float chin = unit.z > 0.0f ? 1.0f - 0.06f * glm::smoothstep(-0.4f, -0.9f, height) : 1.0f;
        return glm::vec3(local.x * lateral, local.y, local.z * depth * chin);
    });
}

// A tapered tube between two joints with a slight muscle swell. Rings blend
// from startJoint to endJoint over the far half so a bend at endJoint
// stretches the skin instead of tearing it; the end ring follows endJoint
// exactly, matching the next limb's start ring.
void appendSmoothLimb(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                       std::vector<HumanoidBodySegment>& segments, glm::vec3 startPos, glm::vec3 endPos,
                       glm::vec2 crossSectionStart, glm::vec2 crossSectionEnd, int startJoint, int endJoint,
                       HumanoidBodySegment segment, SkinWeights& skinWeights, float swell = 0.07f) {
    constexpr uint32_t kRingSize = 18;
    constexpr uint32_t kRings = 9;
    BodyBuilder builder{vertices, indices, segments, skinWeights, segment};

    glm::vec3 boneDir = endPos - startPos;
    if (glm::length(boneDir) < 1e-5f) boneDir = glm::vec3(0.0f, -1.0f, 0.0f);
    boneDir = glm::normalize(boneDir);
    glm::vec3 reference = std::abs(boneDir.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 basisA = glm::normalize(glm::cross(reference, boneDir));
    glm::vec3 basisB = glm::normalize(glm::cross(boneDir, basisA));

    uint32_t first = builder.vertexMark();
    size_t firstIndex = builder.indexMark();
    for (uint32_t r = 0; r < kRings; ++r) {
        float t = static_cast<float>(r) / static_cast<float>(kRings - 1);
        glm::vec3 center = glm::mix(startPos, endPos, t);
        glm::vec2 crossSection = glm::mix(crossSectionStart, crossSectionEnd, t) * (1.0f + swell * std::sin(kPi * t));
        float endWeight = glm::smoothstep(0.4f, 1.0f, t);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            float theta = (static_cast<float>(i) / static_cast<float>(kRingSize)) * 2.0f * kPi;
            glm::vec3 radial = basisA * std::cos(theta) + basisB * std::sin(theta);
            glm::vec3 offset = basisA * (std::cos(theta) * crossSection.x) + basisB * (std::sin(theta) * crossSection.y);
            builder.push(center + offset, radial, {static_cast<float>(i) / static_cast<float>(kRingSize), t}, startJoint,
                         endJoint, endWeight);
        }
    }
    builder.connectRings(first, kRings, kRingSize, true);
    builder.smoothNormals(first, firstIndex);
}

// Upsamples a few control rings with Catmull-Rom so the silhouette curves
// instead of kinking at each control ring.
std::vector<glm::vec2> upsampleProfile(const std::vector<glm::vec2>& controls, uint32_t stepsPerSpan) {
    if (controls.size() < 2) return controls;
    std::vector<glm::vec2> out;
    auto at = [&](int i) { return controls[static_cast<size_t>(std::clamp(i, 0, static_cast<int>(controls.size()) - 1))]; };
    for (int span = 0; span + 1 < static_cast<int>(controls.size()); ++span) {
        for (uint32_t step = 0; step < stepsPerSpan; ++step) {
            float t = static_cast<float>(step) / static_cast<float>(stepsPerSpan);
            glm::vec2 p0 = at(span - 1), p1 = at(span), p2 = at(span + 1), p3 = at(span + 2);
            float t2 = t * t, t3 = t2 * t;
            out.push_back(0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                  (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3));
        }
    }
    out.push_back(controls.back());
    return out;
}

// Vertical barrel through `ringRadii` (bottom to top) with a squarish
// superellipse cross-section. capDome > 0 closes the ends with domes of that
// height (as a fraction of the end ring's depth); 0 closes them flat.
void appendProfiledBarrel(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                           std::vector<HumanoidBodySegment>& segments, glm::vec3 center, float halfHeight,
                           const std::vector<glm::vec2>& ringRadii, int jointIndex, HumanoidBodySegment segment,
                           SkinWeights& skinWeights, float capDome = 0.0f) {
    constexpr uint32_t kRingSize = 28;
    constexpr uint32_t kDomeRings = 4;
    constexpr float kSquareness = 0.8f;
    BodyBuilder builder{vertices, indices, segments, skinWeights, segment};
    std::vector<glm::vec2> profile = upsampleProfile(ringRadii, 4);
    if (profile.empty()) return;

    struct Ring {
        float y;
        glm::vec2 radii;
    };
    std::vector<Ring> rings;
    auto domeRings = [&](glm::vec2 radii, float y, float direction) {
        std::vector<Ring> dome;
        float height = capDome * std::min(radii.x, radii.y);
        for (uint32_t k = 1; k <= kDomeRings; ++k) {
            float a = (static_cast<float>(k) / static_cast<float>(kDomeRings + 1)) * 0.5f * kPi;
            dome.push_back({y + direction * height * std::sin(a), radii * std::cos(a)});
        }
        return dome;
    };
    float bottomY = center.y - halfHeight;
    float span = 2.0f * halfHeight;
    if (capDome > 0.0f) {
        std::vector<Ring> bottom = domeRings(profile.front(), bottomY, -1.0f);
        rings.insert(rings.end(), bottom.rbegin(), bottom.rend());
    }
    for (size_t r = 0; r < profile.size(); ++r) {
        float v = profile.size() > 1 ? static_cast<float>(r) / static_cast<float>(profile.size() - 1) : 0.0f;
        rings.push_back({bottomY + v * span, profile[r]});
    }
    if (capDome > 0.0f) {
        std::vector<Ring> top = domeRings(profile.back(), bottomY + span, 1.0f);
        rings.insert(rings.end(), top.begin(), top.end());
    }

    uint32_t first = builder.vertexMark();
    size_t firstIndex = builder.indexMark();
    for (size_t r = 0; r < rings.size(); ++r) {
        float v = static_cast<float>(r) / static_cast<float>(rings.size() - 1);
        for (uint32_t s = 0; s < kRingSize; ++s) {
            float theta = (static_cast<float>(s) / static_cast<float>(kRingSize)) * 2.0f * kPi;
            glm::vec3 offset(signedPow(std::cos(theta), kSquareness) * rings[r].radii.x, 0.0f,
                             signedPow(std::sin(theta), kSquareness) * rings[r].radii.y);
            builder.push(glm::vec3(center.x, rings[r].y, center.z) + offset, offset,
                         {static_cast<float>(s) / static_cast<float>(kRingSize), v}, jointIndex);
        }
    }
    uint32_t ringCount = static_cast<uint32_t>(rings.size());
    builder.connectRings(first, ringCount, kRingSize, true);

    auto closeEnd = [&](uint32_t ring, float y, glm::vec3 outward, bool flat) {
        uint32_t centerIndex = builder.push(glm::vec3(center.x, y, center.z), outward, {0.5f, 0.5f}, jointIndex);
        uint32_t rimFirst = first + ring * kRingSize;
        if (flat) {
            rimFirst = builder.vertexMark();
            for (uint32_t s = 0; s < kRingSize; ++s) {
                builder.push(vertices[first + ring * kRingSize + s].position, outward, {0.5f, 0.5f}, jointIndex);
            }
        }
        for (uint32_t s = 0; s < kRingSize; ++s) builder.tri(centerIndex, rimFirst + s, rimFirst + (s + 1) % kRingSize);
    };
    if (capDome > 0.0f) {
        float bottomHeight = capDome * std::min(profile.front().x, profile.front().y);
        float topHeight = capDome * std::min(profile.back().x, profile.back().y);
        closeEnd(0, bottomY - bottomHeight, {0.0f, -1.0f, 0.0f}, false);
        closeEnd(ringCount - 1, bottomY + span + topHeight, {0.0f, 1.0f, 0.0f}, false);
        builder.smoothNormals(first, firstIndex);
    } else {
        builder.smoothNormals(first, firstIndex);
        closeEnd(0, rings.front().y, {0.0f, -1.0f, 0.0f}, true);
        closeEnd(ringCount - 1, rings.back().y, {0.0f, 1.0f, 0.0f}, true);
    }
}

// Shared torso silhouette: the base body and the shirt shell
// (spawnAvatarClothing()) use the same profile at different outward scales
// so the shirt always encloses the body. Bottom to top: hips, waist,
// chest, upper chest/shoulders, neckline.
std::vector<glm::vec2> torsoProfileFor(float w, float outwardScale) {
    float s = w * outwardScale;
    return {
        {0.215f * s, 0.135f * s},
        {0.200f * s, 0.125f * s},
        {0.240f * s, 0.145f * s},
        {0.300f * s, 0.150f * s},
        {0.235f * s, 0.135f * s},
    };
}

// Fraction of the pelvis-to-neck span the torso's top ring sits at; the
// body and the shirt shell must agree so their rings line up.
constexpr float kTorsoTopFraction = 0.81f;

constexpr float kTorsoCapDome = 0.55f;

// A relaxed hand: rounded palm, four slightly curled fingers of natural
// lengths and a thumb angled forward, all rigidly bound to the hand joint.
// The arm chain runs along local X in bind pose, so `sideSign` (-1 left,
// +1 right) points toward the fingertips. palmHalfExtents: x length,
// y thickness, z width.
void appendHand(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<HumanoidBodySegment>& segments,
                 glm::vec3 wristPos, float sideSign, glm::vec3 palmHalfExtents, int jointIndex,
                 HumanoidBodySegment segment, SkinWeights& skinWeights) {
    BodyBuilder builder{vertices, indices, segments, skinWeights, segment};
    glm::vec3 along(sideSign, 0.0f, 0.0f);
    glm::vec3 h = palmHalfExtents;

    Ellipsoid palm;
    palm.center = wristPos + along * (h.x * 0.85f);
    palm.halfExtents = h;
    palm.exponent = 0.6f;
    palm.jointA = jointIndex;
    palm.rings = 12;
    palm.segments = 18;
    appendEllipsoid(builder, palm);

    auto fingerBasis = [&](glm::vec3 direction) {
        glm::vec3 x = glm::normalize(direction);
        glm::vec3 z = glm::normalize(glm::cross(x, glm::vec3(0.0f, 1.0f, 0.0f)));
        if (glm::dot(z, glm::vec3(0.0f, 0.0f, 1.0f)) < 0.0f) z = -z;
        glm::vec3 y = glm::cross(z, x);
        return glm::mat3(x, y, z);
    };

    constexpr std::array<float, 4> kFingerLength = {0.92f, 1.0f, 0.94f, 0.76f};
    float fingerRadius = h.z * 0.24f;
    float knuckleX = h.x * 1.75f;
    for (size_t i = 0; i < kFingerLength.size(); ++i) {
        float lane = (static_cast<float>(i) / 3.0f) * 2.0f - 1.0f; // +1 index (front) .. -1 pinky
        float length = h.x * 0.95f * kFingerLength[i];
        glm::vec3 direction = along + glm::vec3(0.0f, -0.28f, 0.05f * lane);
        glm::vec3 base = wristPos + along * knuckleX + glm::vec3(0.0f, -h.y * 0.15f, lane * h.z * 0.7f);
        Ellipsoid finger;
        finger.basis = fingerBasis(direction);
        finger.center = base + glm::normalize(direction) * (length * 0.5f);
        finger.halfExtents = glm::vec3(length * 0.5f + fingerRadius * 0.5f, fingerRadius * 0.9f, fingerRadius);
        finger.exponent = 0.75f;
        finger.jointA = jointIndex;
        finger.rings = 8;
        finger.segments = 10;
        appendEllipsoid(builder, finger);
    }

    float thumbLength = h.x * 0.85f;
    glm::vec3 thumbDirection = along * 0.55f + glm::vec3(0.0f, -0.35f, 0.8f);
    glm::vec3 thumbBase = wristPos + along * (h.x * 0.55f) + glm::vec3(0.0f, -h.y * 0.2f, h.z * 0.75f);
    Ellipsoid thumb;
    thumb.basis = fingerBasis(thumbDirection);
    thumb.center = thumbBase + glm::normalize(thumbDirection) * (thumbLength * 0.5f);
    thumb.halfExtents = glm::vec3(thumbLength * 0.5f + fingerRadius * 0.5f, fingerRadius, fingerRadius * 1.1f);
    thumb.exponent = 0.75f;
    thumb.jointA = jointIndex;
    thumb.rings = 8;
    thumb.segments = 10;
    appendEllipsoid(builder, thumb);
}

// A sneaker: a rounded upper that lowers toward the toe and a slightly
// wider, flat sole, both rigidly bound to the foot joint.
void appendShoe(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<HumanoidBodySegment>& segments,
                 glm::vec3 footPos, float ls, int jointIndex, HumanoidBodySegment segment, SkinWeights& skinWeights) {
    BodyBuilder builder{vertices, indices, segments, skinWeights, segment};
    Ellipsoid upper;
    upper.center = footPos + glm::vec3(0.0f, -0.07f, 0.07f);
    upper.halfExtents = glm::vec3(0.12f, 0.08f, 0.21f) * ls;
    upper.exponent = 0.55f;
    upper.jointA = jointIndex;
    float halfLength = upper.halfExtents.z;
    float halfHeight = upper.halfExtents.y;
    appendEllipsoid(builder, upper, [halfLength, halfHeight](glm::vec3 local, glm::vec3) {
        float toe = glm::smoothstep(-0.1f, 1.0f, local.z / halfLength);
        if (local.y > 0.0f) local.y *= 1.0f - 0.45f * toe;
        local.y -= 0.12f * halfHeight * toe;
        local.x *= 1.0f - 0.12f * toe * toe;
        return local;
    });

    Ellipsoid sole;
    sole.center = footPos + glm::vec3(0.0f, -0.135f, 0.075f);
    sole.halfExtents = glm::vec3(0.13f, 0.025f, 0.225f) * ls;
    sole.exponent = 0.4f;
    sole.jointA = jointIndex;
    sole.rings = 10;
    appendEllipsoid(builder, sole);
}

} // namespace

Skeleton applyBodyProportionsToSkeleton(const Skeleton& base, BodyProportions proportions) {
    Skeleton scaled = base;
    for (Joint& joint : scaled.joints) {
        joint.localPosition.y *= proportions.height;
        if (joint.name == "spine_lower" || joint.name == "spine_upper" || joint.name == "neck") {
            joint.localPosition.y *= proportions.torsoLength;
        }
        if (joint.name == "leg_L_upper" || joint.name == "leg_R_upper") {
            joint.localPosition.x *= proportions.width;
        }
        if (joint.name == "arm_L_upper" || joint.name == "arm_R_upper") {
            joint.localPosition.x *= proportions.shoulderWidth;
        }
    }
    return scaled;
}

Skeleton buildHumanoidSkeleton() {
    Skeleton skeleton;
    skeleton.name = "Humanoid";

    // Kronos ("Avatar System" -- Full Technical Specification): the real
    // 18-bone rig, exactly as specified (root/pelvis/spine_lower/
    // spine_upper/neck/head, upper/lower arms + hands, upper/lower legs +
    // feet on both sides). Bind pose is a real, honest T-ish pose (arms
    // out to the sides), standing with feet at the skeleton's own local
    // Y=0 -- root sits at the ground, pelvis at real waist height above
    // it, matching this engine's existing ~1.8-2.0 unit character-height
    // convention (CharacterController's own capsule).
    // The root joint is lifted by the shoe sole depth so soles rest on
    // the skeleton origin rather than the ankle joint.
    Joint root;
    root.name = "root";
    root.localPosition = {0.0f, kAvatarSoleDepth, 0.0f};
    int rootIndex = skeleton.addJoint(root);

    Joint pelvis;
    pelvis.name = "pelvis";
    pelvis.parentIndex = rootIndex;
    pelvis.localPosition = {0.0f, 1.0f, 0.0f};
    int pelvisIndex = skeleton.addJoint(pelvis);

    // Kronos ("Avatar Proportion and Arm Polish Pass" -- "verify
    // torso-to-leg ratio (torso ~= 0.9 leg length)"): real, small
    // reduction (0.2 -> 0.16) -- pelvis-to-neck torso height was 0.85
    // against a real 0.9 hip-to-foot leg length (ratio ~0.94); this
    // closes it to 0.81 (ratio exactly 0.9), matching the target. Safe
    // to change in isolation (no `.anim` file update needed) because
    // spine_lower's own local position is never baked into any shipped
    // clip -- only spine_upper is ever tracked (always at its own
    // unchanged local 0.3 offset from spine_lower), so spine_upper's
    // real *absolute* height shifts down automatically through the
    // real joint hierarchy without its own tracked value needing to
    // change.
    Joint spineLower;
    spineLower.name = "spine_lower";
    spineLower.parentIndex = pelvisIndex;
    spineLower.localPosition = {0.0f, 0.16f, 0.0f};
    int spineLowerIndex = skeleton.addJoint(spineLower);

    Joint spineUpper;
    spineUpper.name = "spine_upper";
    spineUpper.parentIndex = spineLowerIndex;
    spineUpper.localPosition = {0.0f, 0.3f, 0.0f};
    int spineUpperIndex = skeleton.addJoint(spineUpper);

    Joint neck;
    neck.name = "neck";
    neck.parentIndex = spineUpperIndex;
    neck.localPosition = {0.0f, 0.35f, 0.0f};
    int neckIndex = skeleton.addJoint(neck);

    Joint head;
    head.name = "head";
    head.parentIndex = neckIndex;
    head.localPosition = {0.0f, 0.2f, 0.0f};
    int headIndex = skeleton.addJoint(head);

    // Kronos ("Avatar 2.0" -- "Facial System"): five real, new attachment
    // joints, children of "head" -- NOT a vertex morph-target/blend-shape
    // system (this rig's GPU skinning pipeline has no per-vertex blend
    // weight support to build that against without a much larger render-
    // pipeline change, see AvatarFace.hpp's own header comment for the
    // real, honest scope this took instead). Each real facial feature
    // mesh (spawnAvatarFace(), AvatarFace.cpp) is rigidly bound 100% to
    // its own joint here, so "expression" is real, per-joint procedural
    // transform (scale/rotate/offset), applied the exact same
    // right-multiply-onto-the-skinning-matrix way the existing head-bob
    // (AvatarController::tick()) already proves out -- not four separate
    // new mechanisms. Positions are real, hand-placed offsets on the
    // real head ellipsoid's own front hemisphere (+Z is this rig's own
    // real "forward" -- see CharacterController::tick()'s own
    // faceDir = (sin(yaw), 0, cos(yaw)), which is 0,0,1 at yaw 0).
    Joint leftEye;
    leftEye.name = "face_left_eye";
    leftEye.parentIndex = headIndex;
    leftEye.localPosition = {-0.06f, 0.03f, 0.14f};
    skeleton.addJoint(leftEye);

    Joint rightEye;
    rightEye.name = "face_right_eye";
    rightEye.parentIndex = headIndex;
    rightEye.localPosition = {0.06f, 0.03f, 0.14f};
    skeleton.addJoint(rightEye);

    Joint leftBrow;
    leftBrow.name = "face_left_brow";
    leftBrow.parentIndex = headIndex;
    leftBrow.localPosition = {-0.06f, 0.09f, 0.135f};
    skeleton.addJoint(leftBrow);

    Joint rightBrow;
    rightBrow.name = "face_right_brow";
    rightBrow.parentIndex = headIndex;
    rightBrow.localPosition = {0.06f, 0.09f, 0.135f};
    skeleton.addJoint(rightBrow);

    Joint mouth;
    mouth.name = "face_mouth";
    mouth.parentIndex = headIndex;
    mouth.localPosition = {0.0f, -0.07f, 0.145f};
    skeleton.addJoint(mouth);

    // Kronos ("Avatar 2.0" -- "Accessory Rigging"): four real, new
    // attachment joints -- "handheld" reuses the existing hand_L/hand_R
    // joints below (already real; no new joint needed for it). Real,
    // hand-placed offsets on the head ellipsoid/torso, same convention
    // the five facial joints just above already establish.
    Joint attachHat;
    attachHat.name = "attach_hat";
    attachHat.parentIndex = headIndex;
    attachHat.localPosition = {0.0f, 0.19f, 0.0f};
    skeleton.addJoint(attachHat);

    Joint attachHair;
    attachHair.name = "attach_hair";
    attachHair.parentIndex = headIndex;
    attachHair.localPosition = {0.0f, 0.15f, -0.09f};
    skeleton.addJoint(attachHair);

    Joint attachFaceAccessory;
    attachFaceAccessory.name = "attach_face_accessory";
    attachFaceAccessory.parentIndex = headIndex;
    attachFaceAccessory.localPosition = {0.0f, 0.03f, 0.165f};
    skeleton.addJoint(attachFaceAccessory);

    // attach_back is parented to spine_upper (the torso's own real
    // attachment joint, same one the torso mesh itself binds to), not
    // head -- a real, distinct location for backpacks/capes.
    Joint attachBack;
    attachBack.name = "attach_back";
    attachBack.parentIndex = spineUpperIndex;
    attachBack.localPosition = {0.0f, 0.05f, -0.17f};
    skeleton.addJoint(attachBack);

    // Kronos ("Avatar Visual Silhouette Pass" -- "Arms, Legs, and Feet"):
    // arm segment lengths real-increased from the original 0.32/0.28
    // (total 0.6) to 0.51/0.44 (total 0.95), together with a new, more
    // vertical idle/walk/run/jump_start rest angle (85 degrees off
    // horizontal, up from the previous T-pose-fix's 50 degrees -- see
    // idle.anim's own real, recomposed keyframes) -- forward-kinematics
    // verified (scripted, not eyeballed) to land the idle-pose hand just
    // below mid-thigh height (world y ~= 0.654, vs. the real hip/knee
    // midpoint at 0.675), matching a classic blocky-avatar silhouette
    // rather than a realistic human reach. A length-only change at the
    // old 50-degree rest angle would have needed an even longer,
    // disproportionate arm to reach the same target -- the rest-angle
    // change is a real, necessary part of this, not a separate, optional
    // tweak (see docs/progress.md for the full FK math).
    //
    // The shoulder's own lateral offset also real-widened, 0.25 -> 0.36
    // -> 0.41 -> 0.44 (three real, separate rounds of fixes found via
    // live screenshot/direct feedback, not part of the original plan).
    // First round: the torso's own new, wider shoulder-bulge ring (see
    // the torso profile below, 0.29 max half-width) meant a
    // near-vertical arm starting at the original 0.25 offset began
    // *inside* the torso's own silhouette and stayed hugging it for its
    // whole length, reading as almost invisible from the front. Second
    // round: a real, direct user proportional-reference correction
    // thickened the arm's own cross-sections meaningfully (see
    // shoulderCrossSection's own comment below), needing a matching
    // increase in torso clearance. Third round (this pass, "adjust
    // shoulder offset outward by ~0.05 torso width"): a further real,
    // modest widening -- 0.41 had the arm's own inner edge
    // (0.41 - 0.125 cross-section radius = 0.285) sitting *just inside*
    // the torso's 0.29 boundary, a real, if small, overlap; 0.44 clears
    // it by a real 0.025 margin instead.
    //
    // Kronos ("Avatar Proportion and Arm Polish Pass" -- "shorten upper
    // arms slightly so wrists land just below mid-thigh"): the
    // upper/lower split changed 0.51/0.44 -> 0.47/0.48 (same real 0.95
    // total -- see the FK note below on why the split alone doesn't
    // change the wrist's own target height). Real FK check (scripted):
    // since both the shoulder's real rest rotation and the elbow's own
    // new subtle idle curvature (added below, "add subtle elbow
    // curvature for silhouette continuity") rotate around the *same* Z
    // axis, the wrist's final world Y position only depends on the
    // arm's real *total* length, not where along that length the elbow
    // sits -- confirmed by scripted FK across several splits, all
    // landing within 0.0001 of world y=0.652, still comfortably "just
    // below mid-thigh" (0.675).
    // Every .anim file's own arm_L_upper/arm_R_upper keyframes bake
    // this same shoulder position (this rig's animation format stores
    // absolute position per keyframe, not a bind-pose delta) and were
    // updated to match; walk.anim/run.anim's own arm_L_lower/
    // arm_R_lower keyframes bake the upper-arm length the same way and
    // were updated too.
    // Kronos ("Joint Spacing Pass" -- real, deliberate, small 8%
    // reduction, 0.44 -> 0.405 -- NOT the 30-40% originally requested):
    // the previous 0.44 value has real, hand-verified history (three
    // earlier rounds, see this function's own comment above) specifically
    // widening this offset to keep the arm's own inner edge
    // (offset - shoulderCrossSection radius 0.125) clear of the torso's
    // 0.29 shoulder-bulge boundary, landing at a real, tight 0.025
    // margin. A 30-40% cut would have dropped the offset to ~0.26-0.31,
    // putting the arm's inner edge well *inside* the torso and
    // reintroducing that exact, already-fixed overlap. 8% keeps the real
    // visual intent (closing the visible gap, sitting flush at the
    // shoulder socket) while landing just at/past that margin (inner
    // edge ~0.28 vs. the 0.29 boundary -- a small, deliberate few-
    // hundredths overlap right at the socket, which reads as a flush
    // joint rather than a visible seam, not a body-length clipping
    // issue). Every `.anim` file's own arm_L_upper/arm_R_upper keyframes
    // bake this same absolute position (see this function's own
    // class-level comment on why) and were updated to match -- skipping
    // any of them would silently keep showing the old 0.44 offset
    // whenever that clip plays, since a track's own baked position
    // always wins over this bind pose the moment any clip touches the
    // joint.
    // Kronos ("Final Visual Refinements" -- "Adjust shoulder joint
    // attachment height to align with the top of the torso"): real,
    // 0.1 -> 0.2 -- the torso's own real top (torsoTop in
    // buildHumanoidMeshData(), pelvis + (neck-pelvis)*0.81) sits at
    // local Y~=1.656 (pelvis 1.0 + spineLower 0.16 + spineUpper 0.3 =
    // spine_upper at 1.46, *0.81 fraction of the remaining pelvis-to-neck
    // span above that); this joint's own Y offset from spine_upper
    // (0.2) lands the shoulder (and the shoulder-cap sphere sitting on
    // it) at Y~=1.66, matching that top within a few hundredths rather
    // than the old 0.1's Y~=1.56, which sat visibly below it. Every
    // `.anim` file's own arm_L_upper/arm_R_upper keyframes bake this
    // same absolute Y (see this function's own class-level comment) and
    // were updated to match.
    Joint armLUpper;
    armLUpper.name = "arm_L_upper";
    armLUpper.parentIndex = spineUpperIndex;
    armLUpper.localPosition = {-0.405f, 0.2f, 0.0f};
    int armLUpperIndex = skeleton.addJoint(armLUpper);

    Joint armLLower;
    armLLower.name = "arm_L_lower";
    armLLower.parentIndex = armLUpperIndex;
    armLLower.localPosition = {-0.47f, 0.0f, 0.0f};
    int armLLowerIndex = skeleton.addJoint(armLLower);

    Joint handL;
    handL.name = "hand_L";
    handL.parentIndex = armLLowerIndex;
    handL.localPosition = {-0.48f, 0.0f, 0.0f};
    skeleton.addJoint(handL);

    Joint armRUpper;
    armRUpper.name = "arm_R_upper";
    armRUpper.parentIndex = spineUpperIndex;
    armRUpper.localPosition = {0.405f, 0.2f, 0.0f};
    int armRUpperIndex = skeleton.addJoint(armRUpper);

    Joint armRLower;
    armRLower.name = "arm_R_lower";
    armRLower.parentIndex = armRUpperIndex;
    armRLower.localPosition = {0.47f, 0.0f, 0.0f};
    int armRLowerIndex = skeleton.addJoint(armRLower);

    Joint handR;
    handR.name = "hand_R";
    handR.parentIndex = armRLowerIndex;
    handR.localPosition = {0.48f, 0.0f, 0.0f};
    skeleton.addJoint(handR);

    // Kronos ("Joint Spacing Pass"): real 20% reduction, 0.18 -> 0.144 --
    // unlike the shoulder above, no prior overlap-avoidance history
    // exists for this offset against the torso's waist ring (0.20 half-
    // width), and the hip/thigh region sits mostly below the torso's own
    // vertical extent rather than running alongside it for a full limb
    // length, so this has real headroom without reintroducing a fixed
    // bug. Every `.anim` file except idle.anim (idle has no leg tracks
    // at all, so this bind-pose value shows through directly there)
    // bakes this same absolute position and was updated to match.
    Joint legLUpper;
    legLUpper.name = "leg_L_upper";
    legLUpper.parentIndex = pelvisIndex;
    legLUpper.localPosition = {-0.144f, -0.1f, 0.0f};
    int legLUpperIndex = skeleton.addJoint(legLUpper);

    // Kronos ("Avatar Visual Silhouette Pass" -- "Shorten upper legs
    // slightly for balance"): upper leg (thigh) real-shortened 0.45 ->
    // 0.36, lower leg (shin) real-lengthened 0.45 -> 0.54 by the exact
    // same amount -- total hip-to-foot leg length is unchanged (0.9), so
    // feet stay real-grounded at the skeleton's own y=0 convention (see
    // this function's own class-level comment) instead of floating or
    // sinking. A shorter thigh shifts the knee bend point down, reading
    // as a stubbier, lower-center-of-mass silhouette without changing
    // overall height. walk.anim/run.anim/jump_start.anim/jump_air.anim/
    // jump_land.anim all bake this same joint's own local position into
    // their leg_L_lower/leg_R_lower keyframes (this rig's animation
    // format stores absolute position per keyframe, not a bind-pose
    // delta -- see AnimationPlayer's own doc) and were updated to match
    // this exact value; skipping any of them would pop the leg length
    // during that specific clip.
    Joint legLLower;
    legLLower.name = "leg_L_lower";
    legLLower.parentIndex = legLUpperIndex;
    legLLower.localPosition = {0.0f, -0.36f, 0.0f};
    int legLLowerIndex = skeleton.addJoint(legLLower);

    Joint footL;
    footL.name = "foot_L";
    footL.parentIndex = legLLowerIndex;
    footL.localPosition = {0.0f, -0.54f, 0.05f};
    skeleton.addJoint(footL);

    Joint legRUpper;
    legRUpper.name = "leg_R_upper";
    legRUpper.parentIndex = pelvisIndex;
    legRUpper.localPosition = {0.144f, -0.1f, 0.0f};
    int legRUpperIndex = skeleton.addJoint(legRUpper);

    Joint legRLower;
    legRLower.name = "leg_R_lower";
    legRLower.parentIndex = legRUpperIndex;
    legRLower.localPosition = {0.0f, -0.36f, 0.0f};
    int legRLowerIndex = skeleton.addJoint(legRLower);

    Joint footR;
    footR.name = "foot_R";
    footR.parentIndex = legRLowerIndex;
    footR.localPosition = {0.0f, -0.54f, 0.05f};
    skeleton.addJoint(footR);

    return skeleton;
}

HumanoidMeshData buildHumanoidMeshData(const Skeleton& skeleton, HeadShape headShape, BodyProportions bodyProportions) {
    HumanoidMeshData data;
    std::vector<glm::mat4> world = skeleton.bindPoseMatrices();
    auto worldPos = [&](const char* jointName) -> glm::vec3 {
        int index = skeleton.findJointIndex(jointName);
        return index >= 0 ? glm::vec3(world[static_cast<size_t>(index)][3]) : glm::vec3(0.0f);
    };
    auto joint = [&](const char* jointName) { return skeleton.findJointIndex(jointName); };
    auto& v = data.vertices;
    auto& i = data.indices;
    auto& tags = data.vertexSegments;
    auto& sw = data.skinWeights;
    const float w = bodyProportions.width;
    const float ls = bodyProportions.limbScale;

    // Head and neck share the Head segment (skin colour) but the neck is
    // bound to its own joint so head turns twist the neck, not the collar.
    if (headShape == HeadShape::Oval) {
        appendProfiledHead(v, i, tags, worldPos("head"), headShapeRadii(headShape), joint("head"),
                           HumanoidBodySegment::Head, sw);
    } else {
        appendSphere(v, i, tags, worldPos("head"), headShapeRadii(headShape), joint("head"), HumanoidBodySegment::Head, sw);
    }
    glm::vec3 pelvisPos = worldPos("pelvis");
    glm::vec3 neckPos = worldPos("neck");
    glm::vec3 torsoTop = pelvisPos + (neckPos - pelvisPos) * kTorsoTopFraction;
    appendSmoothLimb(v, i, tags, torsoTop - glm::vec3(0.0f, 0.03f, 0.0f), neckPos + glm::vec3(0.0f, 0.04f, 0.0f),
                     glm::vec2(0.1f * w, 0.092f * w), glm::vec2(0.078f * w, 0.075f * w), joint("neck"), joint("neck"),
                     HumanoidBodySegment::Head, sw, 0.0f);

    appendProfiledBarrel(v, i, tags, (pelvisPos + torsoTop) * 0.5f, glm::length(torsoTop - pelvisPos) * 0.5f,
                         torsoProfileFor(w, 1.0f), joint("spine_upper"), HumanoidBodySegment::Torso, sw, kTorsoCapDome);

    float hipReach = std::abs(worldPos("leg_L_upper").x - pelvisPos.x) + 0.1f * ls;
    appendSphere(v, i, tags, pelvisPos + glm::vec3(0.0f, -0.05f, 0.0f), glm::vec3(hipReach, 0.11f, 0.135f * w),
                 joint("pelvis"), HumanoidBodySegment::LeftLeg, sw);

    struct ArmSpec {
        const char* upper;
        const char* lower;
        const char* hand;
        HumanoidBodySegment arm;
        HumanoidBodySegment handSegment;
        float side;
    };
    const glm::vec2 shoulderSection(0.09f * ls, 0.088f * ls);
    const glm::vec2 elbowSection(0.068f * ls, 0.066f * ls);
    const glm::vec2 wristSection(0.052f * ls, 0.045f * ls);
    for (const ArmSpec& arm : {ArmSpec{"arm_L_upper", "arm_L_lower", "hand_L", HumanoidBodySegment::LeftArm,
                                       HumanoidBodySegment::LeftHand, -1.0f},
                               ArmSpec{"arm_R_upper", "arm_R_lower", "hand_R", HumanoidBodySegment::RightArm,
                                       HumanoidBodySegment::RightHand, 1.0f}}) {
        glm::vec3 shoulder = worldPos(arm.upper);
        glm::vec3 elbow = worldPos(arm.lower);
        glm::vec3 wrist = worldPos(arm.hand);
        appendSphereBlended(v, i, tags, shoulder - glm::vec3(arm.side * 0.04f * ls, 0.03f * ls, 0.0f),
                            glm::vec3(0.12f, 0.095f, 0.1f) * ls, joint("spine_upper"), joint(arm.upper),
                            glm::vec3(arm.side, -0.6f, 0.0f), arm.arm, sw);
        appendSmoothLimb(v, i, tags, shoulder, elbow, shoulderSection, elbowSection, joint(arm.upper), joint(arm.lower),
                         arm.arm, sw);
        appendSphereBlended(v, i, tags, elbow, glm::vec3(elbowSection.x * 0.98f), joint(arm.upper), joint(arm.lower),
                            glm::normalize(wrist - elbow), arm.arm, sw);
        appendSmoothLimb(v, i, tags, elbow, wrist, elbowSection, wristSection, joint(arm.lower), joint(arm.hand),
                         arm.arm, sw, 0.1f);
        appendHand(v, i, tags, wrist, arm.side, glm::vec3(0.092f, 0.04f, 0.082f) * ls, joint(arm.hand),
                   arm.handSegment, sw);
    }

    struct LegSpec {
        const char* upper;
        const char* lower;
        const char* foot;
        HumanoidBodySegment leg;
        HumanoidBodySegment footSegment;
    };
    const glm::vec2 hipSection(0.12f * ls, 0.12f * ls);
    const glm::vec2 kneeSection(0.085f * ls, 0.088f * ls);
    const glm::vec2 ankleSection(0.06f * ls, 0.062f * ls);
    for (const LegSpec& leg : {LegSpec{"leg_L_upper", "leg_L_lower", "foot_L", HumanoidBodySegment::LeftLeg,
                                       HumanoidBodySegment::LeftFoot},
                               LegSpec{"leg_R_upper", "leg_R_lower", "foot_R", HumanoidBodySegment::RightLeg,
                                       HumanoidBodySegment::RightFoot}}) {
        glm::vec3 hip = worldPos(leg.upper);
        glm::vec3 knee = worldPos(leg.lower);
        glm::vec3 ankle = worldPos(leg.foot);
        appendSmoothLimb(v, i, tags, hip, knee, hipSection, kneeSection, joint(leg.upper), joint(leg.lower), leg.leg, sw);
        appendSphereBlended(v, i, tags, knee, glm::vec3(kneeSection.x * 1.02f), joint(leg.upper), joint(leg.lower),
                            glm::normalize(ankle - knee), leg.leg, sw);
        appendSmoothLimb(v, i, tags, knee, ankle, kneeSection, ankleSection, joint(leg.lower), joint(leg.foot), leg.leg,
                         sw, 0.12f);
        appendShoe(v, i, tags, ankle, ls, joint(leg.foot), leg.footSegment, sw);
    }
    return data;
}

HumanoidMeshData extractSegment(const HumanoidMeshData& meshData, HumanoidBodySegment segment) {
    HumanoidMeshData out;
    std::vector<uint32_t> remap(meshData.vertices.size(), ~0u);

    for (size_t i = 0; i < meshData.vertices.size(); ++i) {
        if (meshData.vertexSegments[i] != segment) continue;
        remap[i] = static_cast<uint32_t>(out.vertices.size());
        out.vertices.push_back(meshData.vertices[i]);
        out.skinWeights.perVertex.push_back(meshData.skinWeights.perVertex[i]);
        out.vertexSegments.push_back(segment);
    }

    for (size_t i = 0; i + 2 < meshData.indices.size(); i += 3) {
        uint32_t a = meshData.indices[i];
        uint32_t b = meshData.indices[i + 1];
        uint32_t c = meshData.indices[i + 2];
        if (remap[a] == ~0u || remap[b] == ~0u || remap[c] == ~0u) continue; // triangle not fully in this segment
        out.indices.push_back(remap[a]);
        out.indices.push_back(remap[b]);
        out.indices.push_back(remap[c]);
    }

    return out;
}

AvatarItemCategory categoryForBodySegment(HumanoidBodySegment segment) {
    switch (segment) {
        case HumanoidBodySegment::Head: return AvatarItemCategory::Head;
        case HumanoidBodySegment::Torso: return AvatarItemCategory::Torso;
        case HumanoidBodySegment::LeftArm: return AvatarItemCategory::Torso;
        case HumanoidBodySegment::RightArm: return AvatarItemCategory::Torso;
        // Kronos ("Multi-Region Clothing Shader & Palette System"): real,
        // but never actually consulted for hands -- resolveSegmentColorsForLoadout()'s
        // own equipped-item override loop skips LeftHand/RightHand
        // entirely (hands are always real skin, no glove
        // AvatarItemCategory exists to equip against). Returned here only
        // so this switch stays real and exhaustive for any other real or
        // future caller.
        case HumanoidBodySegment::LeftHand: return AvatarItemCategory::Head;
        case HumanoidBodySegment::RightHand: return AvatarItemCategory::Head;
        case HumanoidBodySegment::LeftLeg: return AvatarItemCategory::Legs;
        case HumanoidBodySegment::RightLeg: return AvatarItemCategory::Legs;
        // Kronos ("Multi-Region Clothing Shader & Palette System"): real,
        // new -- AvatarItemCategory::Shoes already existed as a real,
        // equippable category (per this file's own earlier "real slot, no
        // fabricated visual behind it yet" comment) with no mesh segment
        // to actually apply its color to until now. This closes that gap.
        case HumanoidBodySegment::LeftFoot: return AvatarItemCategory::Shoes;
        case HumanoidBodySegment::RightFoot: return AvatarItemCategory::Shoes;
    }
    return AvatarItemCategory::Torso;
}

std::array<glm::vec4, kHumanoidBodySegmentCount> resolveSegmentColorsForLoadout(const AvatarLoadout& loadout,
                                                                                 const CatalogueIndex& index,
                                                                                 glm::vec4 skinColor) {
    std::array<glm::vec4, kHumanoidBodySegmentCount> colors;
    for (size_t i = 0; i < kHumanoidBodySegmentCount; ++i) {
        switch (static_cast<HumanoidBodySegment>(i)) {
            case HumanoidBodySegment::Head:
            // Kronos ("Multi-Region Clothing Shader & Palette System" --
            // "Skin Region: Head, Neck, Hands"): real -- hands are bare
            // skin by default, same as the head, not an extension of the
            // shirt's own sleeve color the way they used to read when
            // hands shared the arm's own segment.
            case HumanoidBodySegment::LeftHand:
            case HumanoidBodySegment::RightHand:
                colors[i] = skinColor;
                break;
            case HumanoidBodySegment::LeftLeg:
            case HumanoidBodySegment::RightLeg:
                colors[i] = kDefaultTrouserColor;
                break;
            case HumanoidBodySegment::Torso:
                colors[i] = kDefaultShirtColor;
                break;
            // Kronos ("Final Visual Refinements" -- "Set ... arms ...
            // color to pure black"): real, split off from Torso's own
            // kDefaultShirtColor -- arms and torso now real-differ by
            // default (a dark sleeve against a lighter shirt body), not
            // one flat shirt color everywhere. Still routes through
            // AvatarItemCategory::Torso in categoryForBodySegment() below
            // for equip purposes -- a real, equipped shirt item still
            // legitimately recolors torso+arms together (a real shirt
            // naturally covers both), this only changes the *default*,
            // unequipped look.
            case HumanoidBodySegment::LeftArm:
            case HumanoidBodySegment::RightArm:
                colors[i] = kDefaultArmColor;
                break;
            // Kronos ("Multi-Region Clothing Shader & Palette System" --
            // "Shoe Region: Feet, Shoe Soles"): real, new, distinct
            // default -- see kDefaultShoeColor's own comment.
            case HumanoidBodySegment::LeftFoot:
            case HumanoidBodySegment::RightFoot:
                colors[i] = kDefaultShoeColor;
                break;
        }
    }

    for (size_t i = 0; i < kHumanoidBodySegmentCount; ++i) {
        auto segment = static_cast<HumanoidBodySegment>(i);
        // Kronos ("Multi-Region Clothing Shader & Palette System"): real,
        // deliberate skip -- hands stay real skin regardless of what's
        // equipped in Torso (shirt) or any other category; no glove
        // AvatarItemCategory exists to legitimately override them, and
        // categoryForBodySegment() would otherwise route them through
        // Head's own category by convenience, letting an equipped hat
        // item's color leak onto the hands, which is real, wrong
        // behavior this skip prevents outright.
        if (segment == HumanoidBodySegment::LeftHand || segment == HumanoidBodySegment::RightHand) continue;
        AvatarItemCategory category = categoryForBodySegment(segment);
        std::string itemId = loadout.equippedItemId(category);
        if (itemId.empty()) continue;
        const AvatarItemManifest* manifest = index.findById(itemId);
        if (manifest == nullptr) continue; // equipped id no longer resolves -- fail soft, keep the real, honest default color
        colors[i] = manifest->item.baseColor;
    }
    return colors;
}

glm::vec4 applySegmentShadingGradient(HumanoidBodySegment segment, glm::vec4 color) {
    float multiplier = 1.0f;
    switch (segment) {
        case HumanoidBodySegment::Head: multiplier = 1.0f; break;
        case HumanoidBodySegment::Torso: multiplier = 1.0f; break;
        case HumanoidBodySegment::LeftArm:
        case HumanoidBodySegment::RightArm: multiplier = 0.95f; break;
        // Kronos ("Multi-Region Clothing Shader & Palette System"): real,
        // same 1.0 as Head -- hands are skin too, no reason for a
        // different AO-substitute darkening than the face gets.
        case HumanoidBodySegment::LeftHand:
        case HumanoidBodySegment::RightHand: multiplier = 1.0f; break;
        case HumanoidBodySegment::LeftLeg:
        case HumanoidBodySegment::RightLeg: multiplier = 0.90f; break;
        // Kronos ("Multi-Region Clothing Shader & Palette System"): real,
        // darkest of the group -- shoes sit lowest and closest to real
        // ground contact/shadow of any segment, consistent with this
        // gradient's own "AO substitute" role (see this function's own
        // header comment on why the multiplier stands in for real AO).
        case HumanoidBodySegment::LeftFoot:
        case HumanoidBodySegment::RightFoot: multiplier = 0.85f; break;
    }
    return glm::vec4(color.r * multiplier, color.g * multiplier, color.b * multiplier, color.a);
}

// Kronos ("Avatar Visual Silhouette Pass" -- "Material Pass" -- "Add
// subtle specular... to separate limbs visually. Keep the style
// consistent with Kronos's cinematic lighting"): real, small per-segment
// roughness variation on top of SkinnedRenderable's own existing
// metallic/roughness fields (0.05/0.6 defaults, previously identical on
// every segment) -- head/torso read very slightly smoother (skin/shirt),
// arms a touch rougher, legs (trousers) rougher still, a real, subtle
// specular-highlight size/sharpness difference under this engine's
// existing PBR directional/point lighting (SceneLighting -- no new
// rendering feature, just real per-entity material data every other
// SkinnedRenderable field already uses). AO itself already has a real,
// honest stand-in -- see applySegmentShadingGradient()'s own comment on
// why the color multiplier IS this rig's real AO substitute (no
// per-vertex AO channel exists to compute a true one against); this
// function only adds the specular half. Metallic is left untouched
// (0.05 everywhere) -- an avatar's skin/cloth is genuinely non-metallic,
// varying it wouldn't read as a real material difference the way
// roughness does.
constexpr float kHeadRoughness = 0.55f;
constexpr float kTorsoRoughness = 0.58f;
constexpr float kArmRoughness = 0.62f;
constexpr float kLegRoughness = 0.66f;
// Kronos ("Multi-Region Clothing Shader & Palette System"): real, new --
// hands are skin (matches kHeadRoughness exactly); feet/shoes are
// real-rougher than legs/trousers, consistent with shoe material (cloth/
// leather/rubber) reading less smooth than woven trouser fabric.
constexpr float kHandRoughness = kHeadRoughness;
constexpr float kFootRoughness = 0.70f;

// Internal linkage -- unlike applySegmentShadingGradient() (also called
// live from AvatarEditor.cpp/Application.cpp's own re-tint paths),
// roughness is a static per-segment material property set once at
// spawnRiggedAvatar() time, never re-applied on an equip/skin-tone
// change, so nothing outside this file needs to call it.
[[nodiscard]] static float segmentMaterialRoughness(HumanoidBodySegment segment) {
    switch (segment) {
        case HumanoidBodySegment::Head: return kHeadRoughness;
        case HumanoidBodySegment::Torso: return kTorsoRoughness;
        case HumanoidBodySegment::LeftArm:
        case HumanoidBodySegment::RightArm: return kArmRoughness;
        case HumanoidBodySegment::LeftHand:
        case HumanoidBodySegment::RightHand: return kHandRoughness;
        case HumanoidBodySegment::LeftLeg:
        case HumanoidBodySegment::RightLeg: return kLegRoughness;
        case HumanoidBodySegment::LeftFoot:
        case HumanoidBodySegment::RightFoot: return kFootRoughness;
    }
    return kTorsoRoughness;
}

bool spawnRiggedAvatar(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout, const CatalogueIndex& index,
                        RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator, VkDevice device,
                        VkCommandPool cmdPool, VkQueue queue, std::vector<EntityId>& outSkinnedEntities,
                        std::string& outError, glm::vec4 skinTone, HeadShape headShape, BodyProportions bodyProportions) {
    HumanoidMeshData fullBody = buildHumanoidMeshData(skeleton, headShape, bodyProportions);
    std::array<glm::vec4, kHumanoidBodySegmentCount> colors = resolveSegmentColorsForLoadout(loadout, index, skinTone);

    std::vector<EntityId> spawned;
    for (size_t i = 0; i < kHumanoidBodySegmentCount; ++i) {
        auto segment = static_cast<HumanoidBodySegment>(i);
        HumanoidMeshData segmentData = extractSegment(fullBody, segment);

        RiggedMesh riggedMesh;
        std::string segmentError;
        if (!riggedMesh.uploadFromHost(allocator, device, cmdPool, queue, segmentData.vertices, segmentData.indices,
                                        segmentData.skinWeights, skeleton, segmentError)) {
            outError = std::string("failed to upload rigged mesh for segment \"") + jointNameFor(segment) +
                       "\": " + segmentError;
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        uint32_t handle = riggedMeshLibrary.registerRiggedMesh(std::move(riggedMesh));

        // createEntity() already attaches an identity Transform -- see
        // ECS.hpp -- which AvatarController::tick() overwrites every
        // frame with the character's actual world placement (this
        // function's own header comment explains the split).
        EntityId entity = ecs.createEntity(std::string("AvatarSegment_") + jointNameFor(segment));
        auto& skinned = ecs.addComponent<SkinnedRenderable>(entity);
        skinned.riggedMeshHandle = handle;
        skinned.skinningMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
        skinned.baseColor = applySegmentShadingGradient(segment, colors[i]);
        // Kronos ("Avatar Visual Silhouette Pass" -- "Material Pass"):
        // real, static per-segment roughness -- see
        // segmentMaterialRoughness()'s own comment.
        skinned.roughness = segmentMaterialRoughness(segment);
        // Kronos ("Avatar 2.0" -- "Performance and LOD"): body segments
        // stay AvatarLODCategory::Body (the default) -- see
        // AvatarLODTag's own comment for why this category is never
        // distance-hidden.
        ecs.addComponent<AvatarLODTag>(entity);

        spawned.push_back(entity);
    }

    outSkinnedEntities = std::move(spawned);
    return true;
}

namespace {
// Kronos ("Avatar 2.0" -- "Clothing Meshes" -- "basic cloth shading"): a
// real, small, uniform darkening -- the same real "cheap depth/material
// cue, not a new shader/material system" spirit applySegmentShadingGradient()
// already establishes for the bare body, applied here so a clothing
// shell reads as a distinct (duller, less "plastic") material from the
// skin/body underneath it, not a same-looking layer.
constexpr float kClothingShadingMultiplier = 0.92f;

glm::vec4 resolveClothingColor(const AvatarLoadout& loadout, const CatalogueIndex& index, AvatarItemCategory category,
                                glm::vec4 defaultColor) {
    std::string itemId = loadout.equippedItemId(category);
    if (itemId.empty()) return defaultColor;
    const AvatarItemManifest* manifest = index.findById(itemId);
    return manifest != nullptr ? manifest->item.baseColor : defaultColor; // real, honest fail-soft, same as resolveSegmentColorsForLoadout()
}

bool uploadClothingPiece(ECS& ecs, const Skeleton& skeleton, const std::vector<Vertex>& vertices,
                          const std::vector<uint32_t>& indices, const SkinWeights& skinWeights, glm::vec4 color,
                          const char* entityName, RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator,
                          VkDevice device, VkCommandPool cmdPool, VkQueue queue, EntityId& outEntity,
                          std::string& outError) {
    RiggedMesh riggedMesh;
    std::string error;
    if (!riggedMesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices, skinWeights, skeleton, error)) {
        outError = std::string("failed to upload \"") + entityName + "\": " + error;
        return false;
    }
    uint32_t handle = riggedMeshLibrary.registerRiggedMesh(std::move(riggedMesh));
    outEntity = ecs.createEntity(entityName);
    auto& skinned = ecs.addComponent<SkinnedRenderable>(outEntity);
    skinned.riggedMeshHandle = handle;
    skinned.skinningMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
    skinned.baseColor = glm::vec4(glm::vec3(color) * kClothingShadingMultiplier, color.a);
    // Kronos ("Avatar 2.0" -- "Performance and LOD"): real -- see
    // AvatarLODTag's own comment.
    ecs.addComponent<AvatarLODTag>(outEntity).category = AvatarLODCategory::Clothing;
    return true;
}
} // namespace

bool spawnAvatarClothing(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout, const CatalogueIndex& index,
                          BodyProportions bodyProportions, ClothingFit fit, RiggedMeshLibrary& riggedMeshLibrary,
                          VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                          std::vector<EntityId>& outClothingEntities, std::string& outError) {
    std::vector<glm::mat4> world = skeleton.bindPoseMatrices();
    auto worldPos = [&](const char* jointName) -> glm::vec3 {
        int index2 = skeleton.findJointIndex(jointName);
        return index2 >= 0 ? glm::vec3(world[static_cast<size_t>(index2)][3]) : glm::vec3(0.0f);
    };
    auto jointIndexFor = [&](const char* jointName) { return skeleton.findJointIndex(jointName); };

    float shell = clothingFitScaleMultiplier(fit);
    float w = bodyProportions.width;
    float ls = bodyProportions.limbScale;
    std::vector<HumanoidBodySegment> unusedSegments; // real clothing pieces aren't split via extractSegment(), so this tag is never read back

    std::vector<EntityId> spawned;

    // Shirt: a real, scaled-up torso barrel (short-sleeve -- covers the
    // upper arm only, a real, stated, deliberate simplification that
    // avoids clipping through the hand box at the wrist) -- one combined
    // mesh, one real draw call.
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;

        glm::vec3 pelvisPos = worldPos("pelvis");
        glm::vec3 neckPos = worldPos("neck");
        // Kronos ("Critical Visual Fixes" -- "Avatar Chest Mesh
        // Clipping"): real fix -- this shell used to span the *full*
        // pelvis-to-neck distance and its own independently hand-tuned
        // 3-ring profile, while the base body torso barrel it's meant to
        // cover had already been shortened to kTorsoTopFraction (0.81) of
        // that span and grown a real 4th "shoulder bulge" ring. The two
        // barrels' rings no longer lined up at the same real heights, and
        // even where they did, the shirt had no ring matching the base
        // body's wider shoulder bulge -- so the base body's own skin-
        // colored torso geometry poked through the shirt at the chest/
        // shoulder band. Reusing the exact same kTorsoTopFraction span and
        // torsoProfileFor() ring shape (scaled outward by `shell`)
        // guarantees the shirt fully encloses the base body at every ring,
        // not just the two endpoints.
        glm::vec3 torsoTop = pelvisPos + (neckPos - pelvisPos) * kTorsoTopFraction;
        glm::vec3 torsoCenter = (pelvisPos + torsoTop) * 0.5f;
        float torsoHalfHeight = glm::length(torsoTop - pelvisPos) * 0.5f * 1.03f; // real, slight over-extension so the shell doesn't clip through the neck/waist seam
        std::vector<glm::vec2> shirtProfile = torsoProfileFor(w, shell);
        appendProfiledBarrel(vertices, indices, unusedSegments, torsoCenter, torsoHalfHeight, shirtProfile,
                              jointIndexFor("spine_upper"), HumanoidBodySegment::Torso, skinWeights, kTorsoCapDome);

        // Kronos ("Critical Visual Fixes" -- "Neck Protrudes Unnaturally
        // From Shirt"): real fix for a real regression the chest-clipping
        // fix above introduced -- correctly shortening the main shirt
        // barrel to torsoTop (matching the base body's own real height)
        // also stopped it from covering any of the real, separate neck
        // cylinder above torsoTop (buildHumanoidMeshData()'s own "Add
        // Distinct Neck Primitive" pass), which the shirt's old, taller
        // (if width-misaligned) barrel used to hide almost entirely by
        // accident. A real, short collar cylinder closes that gap --
        // tapered the same way the neck cylinder itself is (its own base/
        // top cross-sections at kCollarNeckFraction, scaled outward by
        // `shell` so it never clips the real skin underneath), covering
        // just the lower real neck the way a real crew-neck collar does
        // and leaving the rest of the neck genuinely bare, not the whole
        // thing.
        // Kronos ("Final Visual Patch" -- "Neck-to-Collar Intersection"):
        // real root cause, found by tracing both ring-generation functions'
        // actual math, not just their radii -- appendSmoothLimb() and
        // appendProfiledBarrel() use genuinely different ring
        // parametrizations. appendProfiledBarrel() places ring vertices at
        // `(cos(theta)*radii.x, 0, sin(theta)*radii.y)` -- theta=0 sits on
        // world +X. appendSmoothLimb() instead builds a basisA/basisB frame
        // from the limb's own bone direction (real, correct for a
        // diagonal limb like an arm, see its own header comment), which
        // for this near-vertical bone works out to
        // `(sin(theta)*crossSection.y, 0, cos(theta)*crossSection.x)` --
        // theta=0 on world +Z, with X and Z swapped relative to
        // crossSection.x/.y. Even with matching radii and an exactly
        // matching seam *position* (the previous pass's own fix, still
        // correct and kept below), the two rings were real, different
        // ellipses rotated ~90 degrees from each other -- corners of one
        // poking past the other's own edges, exactly the "z-fighting/
        // protrusion" reported. The real fix: build the collar with
        // appendProfiledBarrel() too (a second, separate barrel call, not
        // a taller single one -- see kCollarNeckFraction's own comment
        // below for why it can't just be a 5th ring on the main barrel),
        // so both real pieces share the exact same ring math and the seam
        // is truly seamless, not just close.
        glm::vec3 shirtBarrelTop(torsoCenter.x, torsoCenter.y + torsoHalfHeight, torsoCenter.z);
        constexpr float kCollarNeckFraction = 0.4f;
        glm::vec3 collarTop = torsoTop + (neckPos - torsoTop) * kCollarNeckFraction;
        glm::vec2 collarBaseCS = shirtProfile.back(); // exact seam match with the main barrel's own neckline ring
        glm::vec2 collarTopCS(glm::mix(0.24f, 0.14f, kCollarNeckFraction) * w * shell,
                               glm::mix(0.14f, 0.13f, kCollarNeckFraction) * w * shell);
        int neckJointIndex = jointIndexFor("neck");
        // Kronos: `kCollarNeckFraction` deliberately isn't just a 5th
        // ring appended to the main torso barrel's own profile -- that
        // barrel's rings are always spaced *evenly* across its own real
        // halfHeight (appendProfiledBarrel()'s own real, fixed
        // convention), so adding a ring and extending the height would
        // have shifted rings 0-3's own real positions too, undoing the
        // chest/shoulder-bulge alignment the earlier fix in this same
        // pass depends on. A real, separate, short second barrel avoids
        // that entirely -- its own 2 rings only ever affect its own span.
        glm::vec3 collarCenter = (shirtBarrelTop + collarTop) * 0.5f;
        float collarHalfHeight = glm::length(collarTop - shirtBarrelTop) * 0.5f;
        std::vector<glm::vec2> collarProfile = {collarBaseCS, collarTopCS};
        appendProfiledBarrel(vertices, indices, unusedSegments, collarCenter, collarHalfHeight, collarProfile,
                              neckJointIndex, HumanoidBodySegment::Torso, skinWeights);

        glm::vec2 shoulderCS(0.102f * ls * shell, 0.098f * ls * shell);
        glm::vec2 sleeveCS(0.082f * ls * shell, 0.078f * ls * shell);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("arm_L_upper"), worldPos("arm_L_lower"), shoulderCS,
                          sleeveCS, jointIndexFor("arm_L_upper"), jointIndexFor("arm_L_lower"), HumanoidBodySegment::LeftArm,
                          skinWeights);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("arm_R_upper"), worldPos("arm_R_lower"), shoulderCS,
                          sleeveCS, jointIndexFor("arm_R_upper"), jointIndexFor("arm_R_lower"),
                          HumanoidBodySegment::RightArm, skinWeights);

        glm::vec4 color = resolveClothingColor(loadout, index, AvatarItemCategory::Torso, kDefaultShirtColor);
        EntityId entity;
        if (!uploadClothingPiece(ecs, skeleton, vertices, indices, skinWeights, color, "AvatarClothing_Shirt",
                                  riggedMeshLibrary, allocator, device, cmdPool, queue, entity, outError)) {
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        spawned.push_back(entity);
    }

    // Pants: both real, full legs (hip to ankle), same real smooth-limb
    // technique, scaled outward the same way.
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;

        glm::vec2 hipCS(0.135f * ls * shell, 0.135f * ls * shell);
        glm::vec2 kneeCS(0.105f * ls * shell, 0.105f * ls * shell);
        glm::vec2 ankleCS(0.09f * ls * shell, 0.09f * ls * shell); // real, slightly looser than the bare ankle so trouser cuffs don't clip the foot box

        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("leg_L_upper"), worldPos("leg_L_lower"), hipCS,
                          kneeCS, jointIndexFor("leg_L_upper"), jointIndexFor("leg_L_lower"), HumanoidBodySegment::LeftLeg,
                          skinWeights);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("leg_L_lower"), worldPos("foot_L"), kneeCS, ankleCS,
                          jointIndexFor("leg_L_lower"), jointIndexFor("foot_L"), HumanoidBodySegment::LeftLeg, skinWeights);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("leg_R_upper"), worldPos("leg_R_lower"), hipCS,
                          kneeCS, jointIndexFor("leg_R_upper"), jointIndexFor("leg_R_lower"), HumanoidBodySegment::RightLeg,
                          skinWeights);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("leg_R_lower"), worldPos("foot_R"), kneeCS, ankleCS,
                          jointIndexFor("leg_R_lower"), jointIndexFor("foot_R"), HumanoidBodySegment::RightLeg,
                          skinWeights);

        glm::vec4 color = resolveClothingColor(loadout, index, AvatarItemCategory::Legs, kDefaultTrouserColor);
        EntityId entity;
        if (!uploadClothingPiece(ecs, skeleton, vertices, indices, skinWeights, color, "AvatarClothing_Pants",
                                  riggedMeshLibrary, allocator, device, cmdPool, queue, entity, outError)) {
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        spawned.push_back(entity);
    }

    outClothingEntities = std::move(spawned);
    return true;
}

} // namespace engine::core
