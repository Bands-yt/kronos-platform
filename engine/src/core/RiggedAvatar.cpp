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

// A box with rounded edges and ends laid along start -> end. `section` is the
// half size across the axis (x along `side`, y along the other direction);
// `squareness` below 1 flattens the sides (1 is round). The rounded ends
// reach `cap` past start and end. Skin weights blend from startJoint to
// endJoint between blendFrom and blendTo (fractions of the length).
struct Block {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f, 1.0f, 0.0f};
    glm::vec2 section{0.1f};
    glm::vec3 side{1.0f, 0.0f, 0.0f};
    float squareness = 0.3f;
    float cap = 0.04f;
    float capSquareness = 0.45f;
    int startJoint = -1;
    int endJoint = -1;
    float blendFrom = 0.5f;
    float blendTo = 0.5f;
};

// The same block grown evenly on every side, for clothing worn over it.
Block inflated(Block block, float scale) {
    float grow = (scale - 1.0f) * std::min(block.section.x, block.section.y);
    block.section += glm::vec2(grow);
    block.cap += grow;
    return block;
}

void appendBlock(BodyBuilder& builder, const Block& b) {
    constexpr uint32_t kRingSize = 32;
    constexpr uint32_t kBodyRings = 10;
    constexpr uint32_t kCapRings = 5;
    glm::vec3 axis = b.end - b.start;
    float length = glm::length(axis);
    axis = length > 1e-5f ? axis / length : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 across = b.side - axis * glm::dot(b.side, axis);
    if (glm::length(across) < 1e-4f) {
        glm::vec3 fallback = std::abs(axis.x) > 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        across = fallback - axis * glm::dot(fallback, axis);
    }
    across = glm::normalize(across);
    glm::vec3 other = glm::cross(axis, across);

    struct Ring {
        float along;
        float scale;
        float t;
        float tilt;
    };
    std::vector<Ring> rings;
    for (uint32_t k = kCapRings; k >= 1; --k) {
        float a = static_cast<float>(k) / static_cast<float>(kCapRings + 1) * 0.5f * kPi;
        rings.push_back({-b.cap * signedPow(std::sin(a), b.capSquareness), signedPow(std::cos(a), b.capSquareness), 0.0f,
                         -std::sin(a)});
    }
    for (uint32_t r = 0; r <= kBodyRings; ++r) {
        float t = static_cast<float>(r) / static_cast<float>(kBodyRings);
        rings.push_back({t * length, 1.0f, t, 0.0f});
    }
    for (uint32_t k = 1; k <= kCapRings; ++k) {
        float a = static_cast<float>(k) / static_cast<float>(kCapRings + 1) * 0.5f * kPi;
        rings.push_back({length + b.cap * signedPow(std::sin(a), b.capSquareness),
                         signedPow(std::cos(a), b.capSquareness), 1.0f, std::sin(a)});
    }

    auto weightAt = [&](float t) {
        if (b.endJoint < 0) return 0.0f;
        if (b.blendTo <= b.blendFrom) return t >= b.blendFrom ? 1.0f : 0.0f;
        return glm::smoothstep(b.blendFrom, b.blendTo, t);
    };

    uint32_t first = builder.vertexMark();
    size_t firstIndex = builder.indexMark();
    for (size_t r = 0; r < rings.size(); ++r) {
        const Ring& ring = rings[r];
        glm::vec3 center = b.start + axis * ring.along;
        float weight = weightAt(ring.t);
        for (uint32_t s = 0; s < kRingSize; ++s) {
            float theta = static_cast<float>(s) / static_cast<float>(kRingSize) * 2.0f * kPi;
            float c = std::cos(theta);
            float sn = std::sin(theta);
            glm::vec3 offset = across * (signedPow(c, b.squareness) * b.section.x * ring.scale) +
                               other * (signedPow(sn, b.squareness) * b.section.y * ring.scale);
            glm::vec3 radial = across * (signedPow(c, 2.0f - b.squareness) / b.section.x) +
                               other * (signedPow(sn, 2.0f - b.squareness) / b.section.y);
            glm::vec3 normal = glm::normalize(radial) * std::sqrt(1.0f - ring.tilt * ring.tilt) + axis * ring.tilt;
            builder.push(center + offset, normal,
                         {static_cast<float>(s) / static_cast<float>(kRingSize),
                          static_cast<float>(r) / static_cast<float>(rings.size() - 1)},
                         b.startJoint, b.endJoint, weight);
        }
    }
    uint32_t ringCount = static_cast<uint32_t>(rings.size());
    builder.connectRings(first, ringCount, kRingSize, true);
    uint32_t last = first + (ringCount - 1) * kRingSize;
    uint32_t startPole = builder.push(b.start - axis * b.cap, -axis, {0.5f, 0.0f}, b.startJoint, b.endJoint, weightAt(0.0f));
    uint32_t endPole = builder.push(b.end + axis * b.cap, axis, {0.5f, 1.0f}, b.startJoint, b.endJoint, weightAt(1.0f));
    for (uint32_t s = 0; s < kRingSize; ++s) {
        builder.tri(startPole, first + s, first + (s + 1) % kRingSize);
        builder.tri(endPole, last + s, last + (s + 1) % kRingSize);
    }
    builder.smoothNormals(first, firstIndex);
}

// Where each block of the Roblox-style body goes for a given skeleton. The
// body and the clothing shells share it so clothes always fit.
struct BlockyBody {
    Block head;
    Block upperTorso;
    Block lowerTorso;
    std::array<Block, 2> arms;
    std::array<Block, 2> sleeves;
    std::array<Block, 2> hands;
    std::array<Block, 2> legs;
    std::array<Block, 2> feet;
};

BlockyBody layoutBlockyBody(const Skeleton& skeleton, HeadShape headShape, BodyProportions proportions) {
    std::vector<glm::mat4> world = skeleton.bindPoseMatrices();
    auto pos = [&](const char* name) {
        int index = skeleton.findJointIndex(name);
        return index >= 0 ? glm::vec3(world[static_cast<size_t>(index)][3]) : glm::vec3(0.0f);
    };
    auto joint = [&](const char* name) { return skeleton.findJointIndex(name); };
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float w = proportions.width;
    const float ls = proportions.limbScale;
    BlockyBody body;

    glm::vec3 radii = headShapeRadii(headShape);
    glm::vec3 headPos = pos("head");
    body.head.cap = 0.1f;
    body.head.start = headPos - up * (radii.y - body.head.cap);
    body.head.end = headPos + up * (radii.y - body.head.cap);
    body.head.section = {radii.x, radii.z};
    body.head.squareness = 0.85f;
    body.head.capSquareness = 0.6f;
    body.head.startJoint = joint("head");

    glm::vec3 pelvis = pos("pelvis");
    float hipY = pos("leg_L_upper").y;
    float waistY = pos("spine_lower").y;
    float neckY = pos("neck").y;

    Block& lower = body.lowerTorso;
    lower.cap = 0.05f;
    lower.start = glm::vec3(pelvis.x, hipY - 0.06f + lower.cap, pelvis.z);
    lower.end = glm::vec3(pelvis.x, waistY + 0.02f - lower.cap, pelvis.z);
    lower.section = glm::vec2(0.265f, 0.14f) * w;
    lower.capSquareness = 0.5f;
    lower.startJoint = joint("pelvis");

    Block& upper = body.upperTorso;
    upper.cap = 0.07f;
    upper.start = glm::vec3(pelvis.x, waistY - 0.06f + upper.cap, pelvis.z);
    upper.end = glm::vec3(pelvis.x, neckY - upper.cap, pelvis.z);
    upper.section = glm::vec2(0.28f, 0.15f) * w;
    upper.startJoint = joint("spine_upper");

    const char* armJoints[2][3] = {{"arm_L_upper", "arm_L_lower", "hand_L"}, {"arm_R_upper", "arm_R_lower", "hand_R"}};
    const char* legJoints[2][3] = {{"leg_L_upper", "leg_L_lower", "foot_L"}, {"leg_R_upper", "leg_R_lower", "foot_R"}};
    for (size_t i = 0; i < 2; ++i) {
        glm::vec3 shoulder = pos(armJoints[i][0]);
        glm::vec3 elbow = pos(armJoints[i][1]);
        glm::vec3 wrist = pos(armJoints[i][2]);
        glm::vec3 along = glm::normalize(wrist - shoulder);

        Block& arm = body.arms[i];
        arm.cap = 0.05f;
        arm.start = shoulder - along * (0.12f - arm.cap);
        arm.end = wrist;
        arm.section = glm::vec2(0.125f * ls);
        arm.side = glm::vec3(0.0f, 0.0f, 1.0f);
        arm.squareness = 0.35f;
        arm.startJoint = joint(armJoints[i][0]);
        arm.endJoint = joint(armJoints[i][1]);
        float elbowT = glm::length(elbow - arm.start) / glm::length(arm.end - arm.start);
        arm.blendFrom = elbowT - 0.1f;
        arm.blendTo = elbowT + 0.1f;

        Block& sleeve = body.sleeves[i];
        sleeve = arm;
        sleeve.end = elbow - along * 0.04f;
        sleeve.endJoint = -1;

        Block& hand = body.hands[i];
        hand.cap = 0.045f;
        hand.start = wrist + along * (hand.cap - 0.01f);
        hand.end = wrist + along * (0.14f * ls - hand.cap);
        hand.section = glm::vec2(0.11f, 0.115f) * ls;
        hand.side = glm::vec3(0.0f, 0.0f, 1.0f);
        hand.squareness = 0.4f;
        hand.capSquareness = 0.55f;
        hand.startJoint = joint(armJoints[i][2]);

        glm::vec3 hip = pos(legJoints[i][0]);
        glm::vec3 knee = pos(legJoints[i][1]);
        glm::vec3 ankle = pos(legJoints[i][2]);

        Block& leg = body.legs[i];
        leg.cap = 0.04f;
        leg.start = hip + up * 0.05f;
        leg.end = ankle;
        leg.section = glm::vec2(0.125f, 0.13f) * ls;
        leg.squareness = 0.35f;
        leg.startJoint = joint(legJoints[i][0]);
        leg.endJoint = joint(legJoints[i][1]);
        float kneeT = glm::length(knee - leg.start) / glm::length(leg.end - leg.start);
        leg.blendFrom = kneeT - 0.08f;
        leg.blendTo = kneeT + 0.08f;

        Block& foot = body.feet[i];
        foot.cap = 0.06f;
        float soleHalf = 0.5f * kAvatarSoleDepth * proportions.height;
        foot.start = ankle + glm::vec3(0.0f, -soleHalf, -0.13f * ls + foot.cap);
        foot.end = ankle + glm::vec3(0.0f, -soleHalf, 0.17f * ls - foot.cap);
        foot.section = glm::vec2(0.13f * ls, soleHalf);
        foot.squareness = 0.35f;
        foot.capSquareness = 0.5f;
        foot.startJoint = joint(legJoints[i][2]);
    }
    return body;
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
    neck.localPosition = {0.0f, 0.26f, 0.0f};
    int neckIndex = skeleton.addJoint(neck);

    Joint head;
    head.name = "head";
    head.parentIndex = neckIndex;
    head.localPosition = {0.0f, 0.24f, 0.0f};
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
    leftEye.localPosition = {-0.075f, 0.035f, 0.226f};
    skeleton.addJoint(leftEye);

    Joint rightEye;
    rightEye.name = "face_right_eye";
    rightEye.parentIndex = headIndex;
    rightEye.localPosition = {0.075f, 0.035f, 0.226f};
    skeleton.addJoint(rightEye);

    Joint leftBrow;
    leftBrow.name = "face_left_brow";
    leftBrow.parentIndex = headIndex;
    leftBrow.localPosition = {-0.075f, 0.105f, 0.228f};
    skeleton.addJoint(leftBrow);

    Joint rightBrow;
    rightBrow.name = "face_right_brow";
    rightBrow.parentIndex = headIndex;
    rightBrow.localPosition = {0.075f, 0.105f, 0.228f};
    skeleton.addJoint(rightBrow);

    Joint mouth;
    mouth.name = "face_mouth";
    mouth.parentIndex = headIndex;
    mouth.localPosition = {0.0f, -0.065f, 0.236f};
    skeleton.addJoint(mouth);

    // Kronos ("Avatar 2.0" -- "Accessory Rigging"): four real, new
    // attachment joints -- "handheld" reuses the existing hand_L/hand_R
    // joints below (already real; no new joint needed for it). Real,
    // hand-placed offsets on the head ellipsoid/torso, same convention
    // the five facial joints just above already establish.
    Joint attachHat;
    attachHat.name = "attach_hat";
    attachHat.parentIndex = headIndex;
    attachHat.localPosition = {0.0f, 0.25f, 0.0f};
    skeleton.addJoint(attachHat);

    Joint attachHair;
    attachHair.name = "attach_hair";
    attachHair.parentIndex = headIndex;
    attachHair.localPosition = {0.0f, 0.2f, -0.08f};
    skeleton.addJoint(attachHair);

    Joint attachFaceAccessory;
    attachFaceAccessory.name = "attach_face_accessory";
    attachFaceAccessory.parentIndex = headIndex;
    attachFaceAccessory.localPosition = {0.0f, 0.035f, 0.25f};
    skeleton.addJoint(attachFaceAccessory);

    // attach_back is parented to spine_upper (the torso's own real
    // attachment joint, same one the torso mesh itself binds to), not
    // head -- a real, distinct location for backpacks/capes.
    Joint attachBack;
    attachBack.name = "attach_back";
    attachBack.parentIndex = spineUpperIndex;
    attachBack.localPosition = {0.0f, 0.05f, -0.19f};
    skeleton.addJoint(attachBack);

    // Blocky proportions: arms the height of the torso, hanging just past the
    // hips in the idle pose. The shipped .anim files bake these positions.
    Joint armLUpper;
    armLUpper.name = "arm_L_upper";
    armLUpper.parentIndex = spineUpperIndex;
    armLUpper.localPosition = {-0.405f, 0.14f, 0.0f};
    int armLUpperIndex = skeleton.addJoint(armLUpper);

    Joint armLLower;
    armLLower.name = "arm_L_lower";
    armLLower.parentIndex = armLUpperIndex;
    armLLower.localPosition = {-0.30f, 0.0f, 0.0f};
    int armLLowerIndex = skeleton.addJoint(armLLower);

    Joint handL;
    handL.name = "hand_L";
    handL.parentIndex = armLLowerIndex;
    handL.localPosition = {-0.30f, 0.0f, 0.0f};
    skeleton.addJoint(handL);

    Joint armRUpper;
    armRUpper.name = "arm_R_upper";
    armRUpper.parentIndex = spineUpperIndex;
    armRUpper.localPosition = {0.405f, 0.14f, 0.0f};
    int armRUpperIndex = skeleton.addJoint(armRUpper);

    Joint armRLower;
    armRLower.name = "arm_R_lower";
    armRLower.parentIndex = armRUpperIndex;
    armRLower.localPosition = {0.30f, 0.0f, 0.0f};
    int armRLowerIndex = skeleton.addJoint(armRLower);

    Joint handR;
    handR.name = "hand_R";
    handR.parentIndex = armRLowerIndex;
    handR.localPosition = {0.30f, 0.0f, 0.0f};
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
    const BlockyBody body = layoutBlockyBody(skeleton, headShape, bodyProportions);
    auto add = [&](const Block& block, HumanoidBodySegment segment) {
        BodyBuilder builder{data.vertices, data.indices, data.vertexSegments, data.skinWeights, segment};
        appendBlock(builder, block);
    };

    if (headShape == HeadShape::Sphere) {
        appendSphere(data.vertices, data.indices, data.vertexSegments, glm::mix(body.head.start, body.head.end, 0.5f),
                     headShapeRadii(headShape), body.head.startJoint, HumanoidBodySegment::Head, data.skinWeights);
    } else {
        add(body.head, HumanoidBodySegment::Head);
    }
    add(body.upperTorso, HumanoidBodySegment::Torso);
    add(body.lowerTorso, HumanoidBodySegment::LeftLeg);
    const HumanoidBodySegment armSegments[2] = {HumanoidBodySegment::LeftArm, HumanoidBodySegment::RightArm};
    const HumanoidBodySegment handSegments[2] = {HumanoidBodySegment::LeftHand, HumanoidBodySegment::RightHand};
    const HumanoidBodySegment legSegments[2] = {HumanoidBodySegment::LeftLeg, HumanoidBodySegment::RightLeg};
    const HumanoidBodySegment footSegments[2] = {HumanoidBodySegment::LeftFoot, HumanoidBodySegment::RightFoot};
    for (size_t i = 0; i < 2; ++i) {
        add(body.arms[i], armSegments[i]);
        add(body.hands[i], handSegments[i]);
        add(body.legs[i], legSegments[i]);
        add(body.feet[i], footSegments[i]);
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
            case HumanoidBodySegment::LeftHand:
            case HumanoidBodySegment::RightHand:
            case HumanoidBodySegment::LeftArm:
            case HumanoidBodySegment::RightArm:
                colors[i] = skinColor;
                break;
            case HumanoidBodySegment::LeftLeg:
            case HumanoidBodySegment::RightLeg:
                colors[i] = kDefaultTrouserColor;
                break;
            case HumanoidBodySegment::Torso:
                colors[i] = kDefaultShirtColor;
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
    const BlockyBody body = layoutBlockyBody(skeleton, HeadShape::Oval, bodyProportions);
    float shell = clothingFitScaleMultiplier(fit);
    std::vector<HumanoidBodySegment> unusedSegments; // real clothing pieces aren't split via extractSegment(), so this tag is never read back

    std::vector<EntityId> spawned;

    // Shirt: the upper torso and short sleeves, grown slightly.
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;

        BodyBuilder builder{vertices, indices, unusedSegments, skinWeights, HumanoidBodySegment::Torso};
        appendBlock(builder, inflated(body.upperTorso, shell));
        for (const Block& sleeve : body.sleeves) appendBlock(builder, inflated(sleeve, shell));

        glm::vec4 color = resolveClothingColor(loadout, index, AvatarItemCategory::Torso, kDefaultShirtColor);
        EntityId entity;
        if (!uploadClothingPiece(ecs, skeleton, vertices, indices, skinWeights, color, "AvatarClothing_Shirt",
                                  riggedMeshLibrary, allocator, device, cmdPool, queue, entity, outError)) {
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        spawned.push_back(entity);
    }

    // Pants: the lower torso and both legs, grown slightly.
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;

        BodyBuilder builder{vertices, indices, unusedSegments, skinWeights, HumanoidBodySegment::LeftLeg};
        appendBlock(builder, inflated(body.lowerTorso, shell));
        for (const Block& leg : body.legs) appendBlock(builder, inflated(leg, shell));

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
