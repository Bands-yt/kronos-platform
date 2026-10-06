#include "core/Bvh.hpp"

#include <cmath>

namespace engine::core {

Aabb Aabb::transformed(const Aabb& local, const glm::mat4& transform) {
    const glm::vec3 translation(transform[3]);
    Aabb result{translation, translation};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            const float a = transform[column][row] * local.min[column];
            const float b = transform[column][row] * local.max[column];
            result.min[row] += std::min(a, b);
            result.max[row] += std::max(a, b);
        }
    }
    return result;
}

bool rayAabb(const glm::vec3& origin, const glm::vec3& inverseDirection, const Aabb& box, float maxT,
             float& outEnter) {
    float tmin = 0.0f;
    float tmax = maxT;
    for (int axis = 0; axis < 3; ++axis) {
        const float inv = inverseDirection[axis];
        if (std::isinf(inv)) {
            if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) return false;
            continue;
        }
        float t1 = (box.min[axis] - origin[axis]) * inv;
        float t2 = (box.max[axis] - origin[axis]) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    outEnter = tmin;
    return true;
}

Frustum Frustum::fromViewProjection(const glm::mat4& m) {
    const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
    Frustum frustum;
    frustum.planes = {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2};
    for (glm::vec4& plane : frustum.planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 0.0f) plane /= length;
    }
    return frustum;
}

FrustumTest Frustum::test(const Aabb& box) const {
    const glm::vec3 center = box.center();
    const glm::vec3 extent = 0.5f * (box.max - box.min);
    bool inside = true;
    for (const glm::vec4& plane : planes) {
        const glm::vec3 normal(plane);
        const float distance = glm::dot(normal, center) + plane.w;
        const float radius = glm::dot(extent, glm::abs(normal));
        if (distance < -radius) return FrustumTest::Outside;
        if (distance < radius) inside = false;
    }
    return inside ? FrustumTest::Inside : FrustumTest::Intersects;
}

int32_t DynamicAabbTree::allocateNode() {
    if (freeList_ == kNull) {
        nodes_.emplace_back();
        nodes_.back().height = 0;
        return static_cast<int32_t>(nodes_.size() - 1);
    }
    const int32_t index = freeList_;
    Node& node = nodes_[static_cast<size_t>(index)];
    freeList_ = node.parent;
    node = Node{};
    node.height = 0;
    return index;
}

void DynamicAabbTree::freeNode(int32_t index) {
    Node& node = nodes_[static_cast<size_t>(index)];
    node.parent = freeList_;
    node.height = -1;
    node.left = node.right = kNull;
    freeList_ = index;
}

int32_t DynamicAabbTree::insert(const Aabb& box, uint32_t userData) {
    const int32_t leaf = allocateNode();
    Node& node = nodes_[static_cast<size_t>(leaf)];
    node.box = {box.min - glm::vec3(margin_), box.max + glm::vec3(margin_)};
    node.userData = userData;
    insertLeaf(leaf);
    ++proxyCount_;
    return leaf;
}

void DynamicAabbTree::remove(int32_t proxy) {
    if (proxy < 0 || static_cast<size_t>(proxy) >= nodes_.size()) return;
    if (nodes_[static_cast<size_t>(proxy)].height != 0) return;
    removeLeaf(proxy);
    freeNode(proxy);
    --proxyCount_;
}

bool DynamicAabbTree::update(int32_t proxy, const Aabb& box) {
    Node& node = nodes_[static_cast<size_t>(proxy)];
    if (node.box.contains(box)) {
        // Shrink when the fat box has grown far too loose (an object that was
        // large or fast and is now small), otherwise leave the tree alone.
        const Aabb loose{box.min - glm::vec3(margin_ * 4.0f), box.max + glm::vec3(margin_ * 4.0f)};
        if (loose.contains(node.box)) return false;
    }
    removeLeaf(proxy);
    nodes_[static_cast<size_t>(proxy)].box = {box.min - glm::vec3(margin_), box.max + glm::vec3(margin_)};
    insertLeaf(proxy);
    return true;
}

void DynamicAabbTree::clear() {
    nodes_.clear();
    root_ = kNull;
    freeList_ = kNull;
    proxyCount_ = 0;
}

void DynamicAabbTree::insertLeaf(int32_t leaf) {
    if (root_ == kNull) {
        root_ = leaf;
        nodes_[static_cast<size_t>(leaf)].parent = kNull;
        return;
    }

    // Descend towards the sibling whose merge costs the least surface area.
    const Aabb leafBox = nodes_[static_cast<size_t>(leaf)].box;
    int32_t index = root_;
    while (!nodes_[static_cast<size_t>(index)].leaf()) {
        const Node& node = nodes_[static_cast<size_t>(index)];
        const float area = node.box.surfaceArea();
        const float combinedArea = Aabb::merged(node.box, leafBox).surfaceArea();
        const float cost = 2.0f * combinedArea;
        const float inheritance = 2.0f * (combinedArea - area);

        auto childCost = [&](int32_t child) {
            const Node& c = nodes_[static_cast<size_t>(child)];
            const float merged = Aabb::merged(leafBox, c.box).surfaceArea();
            return c.leaf() ? merged + inheritance : merged - c.box.surfaceArea() + inheritance;
        };
        const float leftCost = childCost(node.left);
        const float rightCost = childCost(node.right);
        if (cost < leftCost && cost < rightCost) break;
        index = leftCost < rightCost ? node.left : node.right;
    }

    const int32_t sibling = index;
    const int32_t oldParent = nodes_[static_cast<size_t>(sibling)].parent;
    const int32_t newParent = allocateNode();
    {
        Node& parent = nodes_[static_cast<size_t>(newParent)];
        parent.parent = oldParent;
        parent.box = Aabb::merged(leafBox, nodes_[static_cast<size_t>(sibling)].box);
        parent.height = nodes_[static_cast<size_t>(sibling)].height + 1;
        parent.left = sibling;
        parent.right = leaf;
    }
    if (oldParent != kNull) {
        Node& grand = nodes_[static_cast<size_t>(oldParent)];
        if (grand.left == sibling) grand.left = newParent;
        else grand.right = newParent;
    } else {
        root_ = newParent;
    }
    nodes_[static_cast<size_t>(sibling)].parent = newParent;
    nodes_[static_cast<size_t>(leaf)].parent = newParent;

    refitUpwards(nodes_[static_cast<size_t>(leaf)].parent);
}

void DynamicAabbTree::removeLeaf(int32_t leaf) {
    if (leaf == root_) {
        root_ = kNull;
        return;
    }
    const int32_t parent = nodes_[static_cast<size_t>(leaf)].parent;
    const int32_t grand = nodes_[static_cast<size_t>(parent)].parent;
    const Node& parentNode = nodes_[static_cast<size_t>(parent)];
    const int32_t sibling = parentNode.left == leaf ? parentNode.right : parentNode.left;

    if (grand != kNull) {
        Node& grandNode = nodes_[static_cast<size_t>(grand)];
        if (grandNode.left == parent) grandNode.left = sibling;
        else grandNode.right = sibling;
        nodes_[static_cast<size_t>(sibling)].parent = grand;
        freeNode(parent);
        refitUpwards(grand);
    } else {
        root_ = sibling;
        nodes_[static_cast<size_t>(sibling)].parent = kNull;
        freeNode(parent);
    }
}

void DynamicAabbTree::refitUpwards(int32_t index) {
    while (index != kNull) {
        index = balance(index);
        Node& node = nodes_[static_cast<size_t>(index)];
        const Node& left = nodes_[static_cast<size_t>(node.left)];
        const Node& right = nodes_[static_cast<size_t>(node.right)];
        node.height = 1 + std::max(left.height, right.height);
        node.box = Aabb::merged(left.box, right.box);
        index = node.parent;
    }
}

// AVL-style rotation: promotes the taller grandchild when the two subtrees
// of `a` differ in height by more than one. Returns the subtree's new root.
int32_t DynamicAabbTree::balance(int32_t a) {
    Node& nodeA = nodes_[static_cast<size_t>(a)];
    if (nodeA.leaf() || nodeA.height < 2) return a;

    const int32_t b = nodeA.left;
    const int32_t c = nodeA.right;
    const int32_t heightDiff = nodes_[static_cast<size_t>(c)].height - nodes_[static_cast<size_t>(b)].height;

    auto rotate = [&](int32_t up, int32_t stay) {
        // `up` (a child of a) becomes the parent of a; `stay` is a's other child.
        Node& upNode = nodes_[static_cast<size_t>(up)];
        const int32_t f = upNode.left;
        const int32_t g = upNode.right;

        upNode.left = a;
        upNode.parent = nodeA.parent;
        nodeA.parent = up;
        if (upNode.parent != kNull) {
            Node& above = nodes_[static_cast<size_t>(upNode.parent)];
            if (above.left == a) above.left = up;
            else above.right = up;
        } else {
            root_ = up;
        }

        const Node& fNode = nodes_[static_cast<size_t>(f)];
        const Node& gNode = nodes_[static_cast<size_t>(g)];
        const int32_t keep = fNode.height > gNode.height ? f : g;
        const int32_t give = keep == f ? g : f;
        upNode.right = keep;
        if (nodeA.left == up) nodeA.left = give;
        else nodeA.right = give;
        nodes_[static_cast<size_t>(give)].parent = a;

        const Node& stayNode = nodes_[static_cast<size_t>(stay)];
        const Node& giveNode = nodes_[static_cast<size_t>(give)];
        nodeA.box = Aabb::merged(stayNode.box, giveNode.box);
        nodeA.height = 1 + std::max(stayNode.height, giveNode.height);
        const Node& keepNode = nodes_[static_cast<size_t>(keep)];
        upNode.box = Aabb::merged(nodeA.box, keepNode.box);
        upNode.height = 1 + std::max(nodeA.height, keepNode.height);
        return up;
    };

    if (heightDiff > 1) return rotate(c, b);
    if (heightDiff < -1) return rotate(b, c);
    return a;
}

bool DynamicAabbTree::validate() const {
    if (root_ == kNull) return proxyCount_ == 0;
    if (nodes_[static_cast<size_t>(root_)].parent != kNull) return false;
    size_t leaves = 0;
    std::vector<int32_t> stack{root_};
    while (!stack.empty()) {
        const int32_t index = stack.back();
        stack.pop_back();
        const Node& node = nodes_[static_cast<size_t>(index)];
        if (node.height < 0) return false;
        if (node.leaf()) {
            if (node.right != kNull || node.height != 0) return false;
            ++leaves;
            continue;
        }
        const Node& left = nodes_[static_cast<size_t>(node.left)];
        const Node& right = nodes_[static_cast<size_t>(node.right)];
        if (left.parent != index || right.parent != index) return false;
        if (node.height != 1 + std::max(left.height, right.height)) return false;
        if (!node.box.contains(left.box) || !node.box.contains(right.box)) return false;
        stack.push_back(node.left);
        stack.push_back(node.right);
    }
    return leaves == proxyCount_;
}

} // namespace engine::core
