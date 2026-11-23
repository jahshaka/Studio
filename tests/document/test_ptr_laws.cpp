// document.ptr_laws — THE SELF-HANDLE LAW of the document's std pointers (NOQT-DOCUMENT-1 stage 2, ruling 6, law 1).
//
// The document's nodes are owned by std::shared_ptr, and a node takes its own handle through
// `weak_from_this().lock()`: a node no shared_ptr owns yet answers NULL, never a throw. The other
// spelling, `shared_from_this()`, THROWS std::bad_weak_ptr for an unowned object, and a SceneNode can
// be unowned: its constructor is public (scenenode.h), so SceneNode is the one type this row exercises.
// NOT EXERCISED, and why: Scene's constructor is public, but a Scene has NO self-handle (its
// enable_shared_from_this base was deleted as unused, NOQT-DOCUMENT-1 stage 2) — a lane that gives
// Scene a self-handle again owes this row a Scene case. Bone has a self-handle (Bone::addChild) but
// cannot exist unowned: `Bone(){}` is private (skeleton.h, before `public:`) and Bone::create wraps
// it at once — a change that makes a Bone constructible unowned owes this row a Bone case.
// The laws of ownership itself — a subtree dies with its last handle, the back-links are weak — are
// services.memory's, section 3.
//
// No engine, no display: the document's own store (nodegraph.h) needs neither.
#include <cstdio>
#include <exception>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scenenode.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// Runs `f`; a throw is a FAIL named by `what` (the law: never a throw).
template <typename F>
static void noThrow(const char *what, F &&f)
{
    try {
        f();
        std::printf("ok:   %s does not throw\n", what);
    } catch (const std::exception &e) {
        std::printf("FAIL: %s threw (%s)\n", what, e.what());
        ++failures;
    }
}

static void testUnownedNode()
{
    std::printf("\n-- a node no shared_ptr owns answers a NULL self-handle --\n");
    iris::SceneNode unowned;                     // nothing owns it
    const iris::SceneNodePtr child = iris::SceneNode::create();
    unowned.addChild(child);
    CHECK(child->hasParent(), "control: the child is linked under the unowned parent");

    iris::SceneNodePtr parent = child;           // non-null before the read: the read must clear it
    noThrow("getParent() under an unowned parent", [&] { parent = child->getParent(); });
    CHECK(!parent, "getParent() of an unowned parent answers null");

    noThrow("removeFromParent() of an unowned node", [&] { unowned.removeFromParent(); });

    unowned.removeChild(child);
    CHECK(!child->hasParent(), "the child leaves the unowned parent");
}

static void testOwnedNode()
{
    std::printf("\n-- the CONTROL: an owned node answers its owner --\n");
    const iris::SceneNodePtr parent = iris::SceneNode::create();
    const iris::SceneNodePtr child = iris::SceneNode::create();
    parent->addChild(child);

    iris::SceneNodePtr read;
    noThrow("getParent() under an owned parent", [&] { read = child->getParent(); });
    CHECK(read != nullptr, "getParent() of an owned parent is non-null");
    CHECK(read == parent, "...and is the owner's own handle");

    const auto kids = parent->children();
    CHECK(kids.size() == 1 && kids.first() == child, "children() answers the owned child's own handle");

    noThrow("removeFromParent() of an owned node", [&] { child->removeFromParent(); });
    CHECK(!child->hasParent(), "removeFromParent() of an owned node detaches it");
}

int main()
{
    testUnownedNode();
    testOwnedNode();
    if (failures) std::printf("\ndocument.ptr_laws: FAILED (%d)\n", failures);
    else std::printf("\ndocument.ptr_laws: PASSED\n");
    std::fflush(stdout);
    return failures ? 1 : 0;
}
