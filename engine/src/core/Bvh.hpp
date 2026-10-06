#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace engine::core {

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    [[nodiscard]] static Aabb merged(const Aabb& a, const Aabb& b) {
        return {glm::min(a.min, b.min), glm::max(a.max, b.max)};
    }
    [[nodiscard]] bool contains(const Aabb& other) const {
        return glm::all(glm::lessThanEqual(min, other.min)) && glm::all(glm::greaterThanEqual(max, other.max));
    }
    [[nodiscard]] bool overlaps(const Aabb& other) const {
        return glm::all(glm::lessThanEqual(min, other.max)) && glm::all(glm::greaterThanEqual(max, other.min));
    }
    [[nodiscard]] float surfaceArea() const {
        const glm::vec3 d = max - min;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }
    [[nodiscard]] glm::vec3 center() const { return 0.5f * (min + max); }

    // Box enclosing `local` after `transform` (Arvo's method).
    [[nodiscard]] static Aabb transformed(const Aabb& local, const glm::mat4& transform);
};

// Slab test against [0, maxT] along origin + t * direction. `inverseDirection`
// may hold infinities for zero components.
[[nodiscard]] bool rayAabb(const glm::vec3& origin, const glm::vec3& inverseDirection, const Aabb& box, float maxT,
                           float& outEnter);

enum class FrustumTest : uint8_t { Outside, Intersects, Inside };

// Six planes (ax + by + cz + d >= 0 inside) from a view-projection matrix.
// Conservative for both [-1,1] and [0,1] clip depth.
struct Frustum {
    std::array<glm::vec4, 6> planes{};

    [[nodiscard]] static Frustum fromViewProjection(const glm::mat4& viewProj);
    [[nodiscard]] FrustumTest test(const Aabb& box) const;
    [[nodiscard]] bool intersects(const Aabb& box) const { return test(box) != FrustumTest::Outside; }
};

// Incrementally updated bounding volume hierarchy (a dynamic AABB tree).
// Leaves store a slightly enlarged ("fat") box so small movements don't
// touch the tree; inserts pick the cheapest sibling by surface area and
// rotations keep the tree balanced.
namespace detail {
// DFS stack that lives on the call stack until it outgrows 128 entries, so
// queries can nest and run from several threads without sharing scratch.
class NodeStack {
public:
    void push(int32_t value) {
        if (size_ < inline_.size()) inline_[size_] = value;
        else spill_.push_back(value);
        ++size_;
    }
    int32_t pop() {
        --size_;
        if (size_ < inline_.size()) return inline_[size_];
        const int32_t value = spill_.back();
        spill_.pop_back();
        return value;
    }
    [[nodiscard]] bool empty() const { return size_ == 0; }

private:
    std::array<int32_t, 128> inline_{};
    std::vector<int32_t> spill_;
    size_t size_ = 0;
};
} // namespace detail

class DynamicAabbTree {
public:
    static constexpr int32_t kNull = -1;

    explicit DynamicAabbTree(float margin = 0.1f) : margin_(margin) {}

    int32_t insert(const Aabb& box, uint32_t userData);
    void remove(int32_t proxy);
    // Returns true when the leaf had to move (its fat box no longer holds `box`).
    bool update(int32_t proxy, const Aabb& box);
    void clear();

    [[nodiscard]] const Aabb& fatAabb(int32_t proxy) const { return nodes_[static_cast<size_t>(proxy)].box; }
    [[nodiscard]] uint32_t userData(int32_t proxy) const { return nodes_[static_cast<size_t>(proxy)].userData; }
    [[nodiscard]] size_t proxyCount() const { return proxyCount_; }
    [[nodiscard]] int height() const { return root_ == kNull ? 0 : nodes_[static_cast<size_t>(root_)].height + 1; }
    [[nodiscard]] float margin() const { return margin_; }
    void setMargin(float margin) { margin_ = std::max(0.0f, margin); }
    // Checks parent links, heights and that every parent box holds its children.
    [[nodiscard]] bool validate() const;

    // visit(proxy) -> bool keepGoing
    template <typename Visit>
    void queryAabb(const Aabb& box, Visit&& visit) const {
        if (root_ == kNull) return;
        detail::NodeStack stack;
        stack.push(root_);
        while (!stack.empty()) {
            const int32_t index = stack.pop();
            const Node& node = nodes_[static_cast<size_t>(index)];
            if (!node.box.overlaps(box)) continue;
            if (node.leaf()) {
                if (!visit(index)) return;
            } else {
                stack.push(node.left);
                stack.push(node.right);
            }
        }
    }

    // visit(proxy). Subtrees fully inside the frustum skip their plane tests.
    template <typename Visit>
    void queryFrustum(const Frustum& frustum, Visit&& visit) const {
        if (root_ == kNull) return;
        detail::NodeStack stack;
        stack.push(root_);
        while (!stack.empty()) {
            const int32_t index = stack.pop();
            const Node& node = nodes_[static_cast<size_t>(index)];
            const FrustumTest result = frustum.test(node.box);
            if (result == FrustumTest::Outside) continue;
            if (result == FrustumTest::Inside) {
                visitLeaves(index, visit);
            } else if (node.leaf()) {
                visit(index);
            } else {
                stack.push(node.left);
                stack.push(node.right);
            }
        }
    }

    // visit(proxy, tEnter) -> float: the new maximum t (return the hit t to
    // clip the ray, maxT to keep going, or a negative value to stop).
    // Nearer children are visited first.
    template <typename Visit>
    void raycast(const glm::vec3& origin, const glm::vec3& direction, float maxT, Visit&& visit) const {
        if (root_ == kNull) return;
        const glm::vec3 inverse(1.0f / direction.x, 1.0f / direction.y, 1.0f / direction.z);
        detail::NodeStack stack;
        stack.push(root_);
        while (!stack.empty()) {
            const int32_t index = stack.pop();
            const Node& node = nodes_[static_cast<size_t>(index)];
            float enter = 0.0f;
            if (!rayAabb(origin, inverse, node.box, maxT, enter)) continue;
            if (node.leaf()) {
                const float result = visit(index, enter);
                if (result < 0.0f) return;
                maxT = std::min(maxT, result);
                continue;
            }
            float leftEnter = 0.0f;
            float rightEnter = 0.0f;
            const bool hitLeft = rayAabb(origin, inverse, nodes_[static_cast<size_t>(node.left)].box, maxT, leftEnter);
            const bool hitRight = rayAabb(origin, inverse, nodes_[static_cast<size_t>(node.right)].box, maxT, rightEnter);
            if (hitLeft && hitRight) {
                const bool leftFirst = leftEnter <= rightEnter;
                stack.push(leftFirst ? node.right : node.left);
                stack.push(leftFirst ? node.left : node.right);
            } else if (hitLeft) {
                stack.push(node.left);
            } else if (hitRight) {
                stack.push(node.right);
            }
        }
    }

private:
    struct Node {
        Aabb box;
        int32_t parent = kNull; // doubles as the free-list link
        int32_t left = kNull;
        int32_t right = kNull;
        int32_t height = -1; // -1 marks a free node
        uint32_t userData = 0;
        [[nodiscard]] bool leaf() const { return left == kNull; }
    };

    template <typename Visit>
    void visitLeaves(int32_t start, Visit& visit) const {
        detail::NodeStack stack;
        stack.push(start);
        while (!stack.empty()) {
            const int32_t index = stack.pop();
            const Node& node = nodes_[static_cast<size_t>(index)];
            if (node.leaf()) {
                visit(index);
            } else {
                stack.push(node.left);
                stack.push(node.right);
            }
        }
    }

    int32_t allocateNode();
    void freeNode(int32_t index);
    void insertLeaf(int32_t leaf);
    void removeLeaf(int32_t leaf);
    int32_t balance(int32_t index);
    void refitUpwards(int32_t index);

    std::vector<Node> nodes_;
    int32_t root_ = kNull;
    int32_t freeList_ = kNull;
    size_t proxyCount_ = 0;
    float margin_;
};

} // namespace engine::core
