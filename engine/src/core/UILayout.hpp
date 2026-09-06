#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::core {

// Kronos ("Native Vector UI Engine" -- v0.4.0 Creator Suite): a real
// flexbox subset over core::UIRenderer's existing rect/text primitives.
// Every real HUD element in this engine today (Application.cpp's health
// bar, ultimate bar, chat panel, settings/leaderboard overlays) is
// hand-positioned: manual pixel offsets, `kBarSize.x * healthFrac`
// computed inline at every call site. This gives that code a declarative
// node tree instead -- author layout/flex properties once, bind the
// properties that change frame-to-frame (a fraction, a label, a color)
// to a live getter, and let computeLayout() do the arithmetic every
// caller was previously duplicating by hand.
//
// Deliberately real-but-bounded, same "state the cut plainly" convention
// core/UvTools.hpp's applyAutoUnwrap() already uses for this codebase:
// no flex-wrap (a row/column that overflows its container simply
// overflows, same as CSS flexbox with wrap:nowrap), no per-child
// align-self (align-items applies uniformly to every child of one
// parent), no z-index/absolute-positioning escape hatch. Real and
// useful for the HUD/panel layouts this engine actually has -- not a
// claim to be a general-purpose UI toolkit's full flexbox spec.
//
// Headlessly testable on purpose (glm + std only, no core/UIRenderer.hpp,
// no volk.h) -- same split ShaderGraph.hpp/ShaderGraphCodegen.hpp already
// establish for computational logic vs. its GPU-facing rendering. Text
// auto-sizing needs real glyph metrics, which live in UIRenderer -- see
// TextMeasureFn below for how that dependency is injected rather than
// linked in directly.
enum class FlexDirection { Row, Column };
enum class JustifyContent { Start, Center, End, SpaceBetween, SpaceAround };
enum class AlignItems { Start, Center, End, Stretch };

enum class UISizeMode {
    Auto,    // content-driven: measured text extent for a Text node, 0 for anything else (see header comment)
    Fixed,   // `value` is a real pixel size
    Percent, // `value` is 0..100, resolved against the parent's own content-box size on that axis
};

struct UISize {
    UISizeMode mode = UISizeMode::Auto;
    float value = 0.0f;
};

enum class UINodeKind { Container, Rect, Text };

// One node's real, resolved-per-frame paint/layout state. Every
// `bind*` callback, when set, overwrites the corresponding plain field
// during UILayoutTree::resolveBindings() -- an unset binding leaves the
// authored default alone, same "an unassigned slot is a no-op"
// convention core::Texture's default-white-texture fallback already
// established for this engine's material system. A binding can be a
// raw C++ lambda closing over a `float&`/game object (e.g.
// `[this]{ return health / maxHealth; }`) or, via ScriptUiLayoutApi, a
// wrapped Luau function call -- both sides see the exact same
// std::function seam, so neither language gets a richer or poorer
// binding surface than the other.
struct UINode {
    int id = 0;
    int parent = 0; // 0 == no parent (ids start at 1, matching studio::ShaderGraph's own nextId_ convention)
    UINodeKind kind = UINodeKind::Container;

    FlexDirection direction = FlexDirection::Row;
    JustifyContent justify = JustifyContent::Start;
    AlignItems align = AlignItems::Stretch;
    UISize width;
    UISize height;
    float flexGrow = 0.0f;
    float flexShrink = 1.0f;
    float gap = 0.0f;

    float paddingLeft = 0.0f, paddingTop = 0.0f, paddingRight = 0.0f, paddingBottom = 0.0f;
    float marginLeft = 0.0f, marginTop = 0.0f, marginRight = 0.0f, marginBottom = 0.0f;

    // Authored paint defaults -- Rect uses `color` directly; Text uses
    // `color`/`text`/`textScale`. `colorEnd`, together with
    // `bindValue`/`fillFromValue`, drives the one built-in "gameplay
    // value" pattern this engine's own HUD code actually needs (a
    // health/ultimate/progress fill): when `fillFromValue` is true and
    // `bindValue` resolves to v in [0,1], this node's main-axis Percent
    // size becomes v*100 and its color lerps color->colorEnd by v --
    // the exact two computations Application.cpp's health-bar code
    // currently does by hand (see kBarSize.x * healthFrac and its own
    // color literals) collapsed into one declarative node.
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 1.0f};
    std::string text;
    float textScale = 1.0f;
    bool fillFromValue = false;

    std::function<float()> bindValue;
    std::function<std::string()> bindText;
    std::function<glm::vec4()> bindColor;

    std::vector<int> children;

    // Written by resolveBindings()/computeLayout() -- read-only outputs
    // for the render step (UIRenderer::drawLayoutTree). `resolvedValue`
    // defaults to 1.0 (a node with no bindValue fills/colors as if fully
    // "on", matching every existing hand-authored full-size HUD bar).
    float resolvedValue = 1.0f;
    std::string resolvedText;
    glm::vec4 resolvedColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 computedPosition{0.0f};
    glm::vec2 computedSize{0.0f};
};

// Real glyph-extent lookup, injected rather than linked in -- see this
// header's own class comment. `scale` matches UIRenderer::drawText()'s
// own `scale` parameter; the returned vec2 is (width, height) in real
// pixels. Passing nullptr to computeLayout() (the default) means every
// Auto-sized Text node falls back to a 0x0 basis -- an honest, explicit
// scope boundary, not a silent wrong answer, for a caller that hasn't
// wired real metrics in (e.g. a headless test).
using TextMeasureFn = std::function<glm::vec2(const std::string& text, float scale)>;

// Kronos: the real, owning tree -- addNode()/setParent()/removeNode()
// are the only ways to mutate node relationships, same
// "one place enforces the invariants" shape studio::ShaderGraph already
// establishes for its own node/pin/link data.
class UILayoutTree {
public:
    [[nodiscard]] int addNode(UINodeKind kind = UINodeKind::Container);

    // Removes `nodeId` and its whole subtree, detaching it from its
    // parent's children list first. A no-op (not an error) if `nodeId`
    // is unknown or already root-less -- same forgiving contract
    // ShaderGraph::removeNode() already uses.
    void removeNode(int nodeId);

    // Detaches `childId` from whatever parent it currently has (if any),
    // then appends it to `parentId`'s children. Passing parentId == 0
    // just detaches -- the real way to remove a node from the visible
    // tree without deleting it outright.
    void setParent(int childId, int parentId);

    void setRoot(int nodeId) { root_ = nodeId; }
    [[nodiscard]] int root() const { return root_; }

    [[nodiscard]] UINode* findNode(int nodeId);
    [[nodiscard]] const UINode* findNode(int nodeId) const;
    [[nodiscard]] const std::vector<UINode>& nodes() const { return nodes_; }

    // Real, must be called once per frame before computeLayout(): calls
    // every live node's bindValue/bindText/bindColor getters (in
    // whatever order nodes() happens to store them -- bindings are pure
    // reads of external state, not order-dependent on each other) and
    // writes resolvedValue/resolvedText/resolvedColor. A node with no
    // binding for a given field just keeps its previous resolved value
    // (initialized from the authored default the first time), matching
    // core::Audio::setCategoryVolume()'s own "unset stays at its last
    // real value" convention.
    void resolveBindings();

    // Real flexbox layout, recursing from root() down: `availableSize`
    // is root's own content box in pixels (callers wanting a
    // screen-relative offset add it to every computedPosition
    // themselves, the same "caller owns screen placement" split
    // UIRenderer::drawRect()'s own topLeftPx parameter already uses). A
    // no-op if root() names an unknown node.
    void computeLayout(glm::vec2 availableSize, const TextMeasureFn& textMeasure = nullptr);

private:
    void layoutChildren(UINode& parent, const TextMeasureFn& textMeasure);
    [[nodiscard]] float mainAxisBasis(const UINode& child, const UINode& parent, const TextMeasureFn& textMeasure) const;
    [[nodiscard]] float crossAxisSize(const UINode& child, const UINode& parent, const TextMeasureFn& textMeasure) const;

    int nextId_ = 1;
    int root_ = 0;
    std::vector<UINode> nodes_; // indexed by linear scan on `id`, same convention as studio::ShaderGraph::nodes_
};

} // namespace engine::core
