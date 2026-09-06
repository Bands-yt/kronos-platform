#include "core/UILayout.hpp"

#include <algorithm>

namespace engine::core {

namespace {

// See UINode::fillFromValue's own comment: a Percent-mode size on a
// fillFromValue node is driven by the live resolved value instead of
// its authored placeholder -- applied uniformly to whichever of
// width/height is in Percent mode, not just the parent's current main
// axis, so a fill bar reads correctly regardless of which axis its
// parent happens to lay out along.
float effectiveSizeValue(const UINode& node, const UISize& size) {
    if (node.fillFromValue && size.mode == UISizeMode::Percent) return node.resolvedValue * 100.0f;
    return size.value;
}

} // namespace

int UILayoutTree::addNode(UINodeKind kind) {
    UINode node;
    node.id = nextId_++;
    node.kind = kind;
    nodes_.push_back(node);
    return nodes_.back().id;
}

UINode* UILayoutTree::findNode(int nodeId) {
    for (auto& n : nodes_) {
        if (n.id == nodeId) return &n;
    }
    return nullptr;
}

const UINode* UILayoutTree::findNode(int nodeId) const {
    for (auto& n : nodes_) {
        if (n.id == nodeId) return &n;
    }
    return nullptr;
}

void UILayoutTree::setParent(int childId, int parentId) {
    UINode* child = findNode(childId);
    if (!child) return;

    if (child->parent != 0) {
        if (UINode* oldParent = findNode(child->parent)) {
            auto& kids = oldParent->children;
            kids.erase(std::remove(kids.begin(), kids.end(), childId), kids.end());
        }
    }
    child->parent = 0;

    if (parentId != 0) {
        if (UINode* newParent = findNode(parentId)) {
            newParent->children.push_back(childId);
            child->parent = parentId;
        }
    }
}

void UILayoutTree::removeNode(int nodeId) {
    UINode* node = findNode(nodeId);
    if (!node) return;

    setParent(nodeId, 0); // detach from its own parent's children list first

    std::vector<int> childrenCopy = node->children; // node may be invalidated by recursive erase below
    for (int childId : childrenCopy) removeNode(childId);

    if (root_ == nodeId) root_ = 0;
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [nodeId](const UINode& n) { return n.id == nodeId; }),
                 nodes_.end());
}

void UILayoutTree::resolveBindings() {
    for (auto& node : nodes_) {
        if (node.bindValue) node.resolvedValue = node.bindValue();
        node.resolvedText = node.bindText ? node.bindText() : node.text;
        if (node.bindColor) {
            node.resolvedColor = node.bindColor();
        } else if (node.fillFromValue) {
            node.resolvedColor = glm::mix(node.color, node.colorEnd, std::clamp(node.resolvedValue, 0.0f, 1.0f));
        } else {
            node.resolvedColor = node.color;
        }
    }
}

float UILayoutTree::mainAxisBasis(const UINode& child, const UINode& parent, const TextMeasureFn& textMeasure) const {
    bool row = parent.direction == FlexDirection::Row;
    const UISize& size = row ? child.width : child.height;
    float parentMain = row ? (parent.computedSize.x - parent.paddingLeft - parent.paddingRight)
                            : (parent.computedSize.y - parent.paddingTop - parent.paddingBottom);
    switch (size.mode) {
        case UISizeMode::Fixed:
            return size.value;
        case UISizeMode::Percent:
            return effectiveSizeValue(child, size) / 100.0f * parentMain;
        case UISizeMode::Auto:
        default:
            if (child.kind == UINodeKind::Text && textMeasure) {
                glm::vec2 extent = textMeasure(child.resolvedText, child.textScale);
                return row ? extent.x : extent.y;
            }
            return 0.0f;
    }
}

float UILayoutTree::crossAxisSize(const UINode& child, const UINode& parent, const TextMeasureFn& textMeasure) const {
    bool row = parent.direction == FlexDirection::Row;
    const UISize& size = row ? child.height : child.width;
    float parentCross = row ? (parent.computedSize.y - parent.paddingTop - parent.paddingBottom)
                             : (parent.computedSize.x - parent.paddingLeft - parent.paddingRight);
    float marginCrossStart = row ? child.marginTop : child.marginLeft;
    float marginCrossEnd = row ? child.marginBottom : child.marginRight;
    switch (size.mode) {
        case UISizeMode::Fixed:
            return size.value;
        case UISizeMode::Percent:
            return effectiveSizeValue(child, size) / 100.0f * parentCross;
        case UISizeMode::Auto:
        default:
            if (parent.align == AlignItems::Stretch) {
                return std::max(0.0f, parentCross - marginCrossStart - marginCrossEnd);
            }
            if (child.kind == UINodeKind::Text && textMeasure) {
                glm::vec2 extent = textMeasure(child.resolvedText, child.textScale);
                return row ? extent.y : extent.x;
            }
            return 0.0f;
    }
}

void UILayoutTree::layoutChildren(UINode& parent, const TextMeasureFn& textMeasure) {
    if (parent.children.empty()) return;

    bool row = parent.direction == FlexDirection::Row;
    glm::vec2 contentOrigin = parent.computedPosition + glm::vec2(parent.paddingLeft, parent.paddingTop);
    glm::vec2 contentSize(parent.computedSize.x - parent.paddingLeft - parent.paddingRight,
                          parent.computedSize.y - parent.paddingTop - parent.paddingBottom);
    float mainSize = row ? contentSize.x : contentSize.y;
    float crossSize = row ? contentSize.y : contentSize.x;

    struct ChildLayout {
        UINode* node;
        float basis;
        float marginStart;
        float marginEnd;
        float finalMain;
        float crossSizeVal;
    };
    std::vector<ChildLayout> layout;
    layout.reserve(parent.children.size());

    float usedMain = 0.0f;
    for (int childId : parent.children) {
        UINode* child = findNode(childId);
        if (!child) continue;
        float basis = mainAxisBasis(*child, parent, textMeasure);
        float marginStart = row ? child->marginLeft : child->marginTop;
        float marginEnd = row ? child->marginRight : child->marginBottom;
        layout.push_back({child, basis, marginStart, marginEnd, basis, 0.0f});
        usedMain += basis + marginStart + marginEnd;
    }
    if (layout.empty()) return;
    usedMain += parent.gap * static_cast<float>(layout.size() - 1);

    float remaining = mainSize - usedMain;
    if (remaining > 0.0f) {
        float totalGrow = 0.0f;
        for (auto& cl : layout) totalGrow += cl.node->flexGrow;
        if (totalGrow > 0.0f) {
            for (auto& cl : layout) cl.finalMain = cl.basis + remaining * (cl.node->flexGrow / totalGrow);
            remaining = 0.0f;
        }
    } else if (remaining < 0.0f) {
        float totalShrinkWeight = 0.0f;
        for (auto& cl : layout) totalShrinkWeight += cl.node->flexShrink * cl.basis;
        if (totalShrinkWeight > 0.0f) {
            for (auto& cl : layout) {
                float weight = (cl.node->flexShrink * cl.basis) / totalShrinkWeight;
                cl.finalMain = std::max(0.0f, cl.basis + remaining * weight);
            }
            remaining = 0.0f;
        }
        // No shrinkable children: content overflows mainSize -- an
        // honest no-wrap overflow, same documented scope cut as this
        // header's own class comment.
    }
    float leftoverForJustify = (remaining > 0.0f) ? remaining : 0.0f;

    for (auto& cl : layout) cl.crossSizeVal = crossAxisSize(*cl.node, parent, textMeasure);

    size_t n = layout.size();
    float startOffset = 0.0f;
    float effectiveGap = parent.gap;
    switch (parent.justify) {
        case JustifyContent::Start:
            break;
        case JustifyContent::Center:
            startOffset = leftoverForJustify * 0.5f;
            break;
        case JustifyContent::End:
            startOffset = leftoverForJustify;
            break;
        case JustifyContent::SpaceBetween:
            if (n > 1) effectiveGap = parent.gap + leftoverForJustify / static_cast<float>(n - 1);
            break;
        case JustifyContent::SpaceAround: {
            float extraPerChild = leftoverForJustify / static_cast<float>(n);
            startOffset = extraPerChild * 0.5f;
            effectiveGap = parent.gap + extraPerChild;
            break;
        }
    }

    float cursor = startOffset;
    for (auto& cl : layout) {
        cursor += cl.marginStart;

        float crossOffset = 0.0f;
        switch (parent.align) {
            case AlignItems::Start:
            case AlignItems::Stretch:
                crossOffset = 0.0f;
                break;
            case AlignItems::Center:
                crossOffset = (crossSize - cl.crossSizeVal) * 0.5f;
                break;
            case AlignItems::End:
                crossOffset = crossSize - cl.crossSizeVal;
                break;
        }

        glm::vec2 pos = row ? glm::vec2(contentOrigin.x + cursor, contentOrigin.y + crossOffset)
                             : glm::vec2(contentOrigin.x + crossOffset, contentOrigin.y + cursor);
        glm::vec2 size = row ? glm::vec2(cl.finalMain, cl.crossSizeVal) : glm::vec2(cl.crossSizeVal, cl.finalMain);

        cl.node->computedPosition = pos;
        cl.node->computedSize = size;

        cursor += cl.finalMain + cl.marginEnd + effectiveGap;

        if (!cl.node->children.empty()) layoutChildren(*cl.node, textMeasure);
    }
}

void UILayoutTree::computeLayout(glm::vec2 availableSize, const TextMeasureFn& textMeasure) {
    UINode* root = findNode(root_);
    if (!root) return;

    root->computedPosition = glm::vec2(0.0f);
    root->computedSize = availableSize;
    layoutChildren(*root, textMeasure);
}

} // namespace engine::core
