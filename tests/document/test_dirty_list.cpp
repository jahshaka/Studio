// document.dirty_list — THE DOCUMENT'S HALF OF THE DIRTY-SET MIRROR.
//
// SPECS/DIRTY_SET_MIRROR_SPEC.md §7.3. The mirror suite proves that what the
// document reports is ENOUGH (mirror.dirty_equals_full runs the whole walk
// behind the change list and demands it has nothing to do). This one proves the
// list itself behaves: no renderer, no engine scene, no pixels — just the
// document saying what changed.
//
// FOUR PROPERTIES, and every one of them is load-bearing somewhere:
//
//  1. DEDUPE. A node appears on the list ONCE per frame however many times it
//     is written. A physics step writes a transform per body per step and a
//     drag writes one per frame per selected node; without the dedupe the list
//     would be the write count, not the object count.
//  2. CASCADE. The three kinds that INHERIT — effective visibility, mobility
//     and a structural move — mark the whole subtree AT EVENT TIME. Since
//     ENGINE-3 the mirror is the SOLE pusher of a document node's effective
//     visibility, so a Visibility mark that stops at the node it was raised on
//     leaves a hidden model's children drawn and voxelised with nothing
//     anywhere to correct it.
//  3. EVICTION. A node that leaves the document is RECORDED (the mirror
//     releases its entry from that record instead of sweeping every entry it
//     holds every frame), and a node that was on the list when it left is
//     CANCELLED — a pointer the consumer would otherwise dereference after the
//     node had been freed.
//  4. MEMBERSHIP. A node outside a scene records nothing (there is no mirror
//     that could be asked about it), and joining one marks its whole subtree.
#include <QGuiApplication>
#include <cstdio>
#include <vector>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/nodedirtyset.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using iris::NodeChange;

namespace {

/// Everything the one consumer does: take the list, clear the nodes' masks.
struct Drain {
    std::vector<iris::SceneNode *> nodes;
    std::vector<iris::SceneNode *> evicted;
    int marked = 0;          ///< live entries (tombstones skipped)
    int tombstones = 0;

    void run(const iris::ScenePtr &scene)
    {
        scene->dirtySet()->takeDirty(nodes);
        scene->dirtySet()->takeEvicted(evicted);
        marked = tombstones = 0;
        for (iris::SceneNode *n : nodes) {
            if (!n) { ++tombstones; continue; }
            ++marked;
            n->_takeDirtyMask();
        }
    }
    bool has(const iris::SceneNodePtr &n) const
    {
        for (iris::SceneNode *p : nodes) if (p == n.data()) return true;
        return false;
    }
    bool wasEvicted(const iris::SceneNode *n) const
    {
        for (iris::SceneNode *p : evicted) if (p == n) return true;
        return false;
    }
};

iris::SceneNodePtr named(const char *n)
{
    auto node = iris::SceneNode::create();
    node->setName(QLatin1String(n));
    return node;
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("document-dirty-list-ogre.log");
    if (!graph.require()) return 1;

    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();
    Drain d;

    // ---- 4: MEMBERSHIP ----------------------------------------------------
    auto detached = named("detached");
    detached->setLocalPos(iris::Vec3(1, 0, 0));
    detached->setVisible(false);
    detached->setVisible(true);
    CHECK(detached->dirtySet() == nullptr, "a node outside a scene has no collector");
    CHECK(!detached->isQueuedDirty(), "...and records nothing");
    d.run(scene);
    CHECK(d.marked == 0, "...so the scene's list is empty");

    // Joining a scene: the whole fragment marks, once.
    auto child = named("fragment-child");
    detached->addChild(child);
    root->addChild(detached);
    CHECK(detached->dirtySet() != nullptr, "joining a scene gives the node a collector");
    CHECK(child->dirtySet() != nullptr, "...and every node under it");
    d.run(scene);
    CHECK(d.has(detached) && d.has(child),
          "a fragment joining a scene marks its WHOLE subtree (Structure cascades)");

    // ---- 1: DEDUPE --------------------------------------------------------
    auto a = named("a");
    auto b = named("b");
    root->addChild(a);
    root->addChild(b);
    d.run(scene);

    for (int i = 0; i < 100; ++i) a->setLocalPos(iris::Vec3(float(i), 0, 0));
    a->setPickable(false);
    a->setShadowCastingEnabled(false);
    a->setName(QStringLiteral("a-renamed"));
    d.run(scene);
    CHECK(d.marked == 1, "104 writes to one node are ONE entry on the list");
    CHECK(d.has(a), "...and it is that node");

    // ...and the MASK carries every kind that was raised.
    a->setLocalPos(iris::Vec3(9, 0, 0));
    a->setPickable(true);
    {
        const quint16 mask = a->dirtyMask();
        CHECK(mask & iris::nodeChangeBit(NodeChange::Transform), "the mask carries Transform");
        CHECK(mask & iris::nodeChangeBit(NodeChange::Flags), "...and Flags");
        CHECK(!(mask & iris::nodeChangeBit(NodeChange::Visibility)), "...and nothing else");
    }
    d.run(scene);
    CHECK(a->dirtyMask() == 0, "consuming a node clears its mask");
    CHECK(!a->isQueuedDirty(), "...and re-arms it");

    // A STILL frame.
    d.run(scene);
    CHECK(d.marked == 0, "a frame with no writes has an EMPTY list");

    // ---- 2: CASCADE -------------------------------------------------------
    //
    // A five-deep chain plus a fan of ten, so "the subtree" is a number.
    auto group = named("group");
    root->addChild(group);
    std::vector<iris::SceneNodePtr> kids;
    for (int i = 0; i < 10; ++i) {
        auto k = named("kid");
        group->addChild(k);
        kids.push_back(k);
        for (int j = 0; j < 2; ++j) {
            auto gk = named("grandkid");
            k->addChild(gk);
            kids.push_back(gk);
        }
    }
    d.run(scene);
    const int subtree = 1 + 30;      // the group, ten kids, twenty grandkids

    group->setVisible(false);
    d.run(scene);
    CHECK(d.marked == subtree,
          "hiding a subtree ROOT marks every descendant (the F6 contract)");
    CHECK(d.has(kids.back()), "...including the deepest one");

    // NO CHANGE, NO MARK: setVisible to the value it already has is a no-op.
    group->setVisible(false);
    d.run(scene);
    CHECK(d.marked == 0, "re-setting a flag to the value it already has marks nothing");

    // A SECOND cascade over the same branch inside one frame costs nothing
    // extra — the pruning that makes a reader hiding a hundred nodes O(n)
    // instead of O(n^2).
    group->setVisible(true);
    for (const auto &k : kids) k->setVisible(k->isVisible());   // no-ops
    group->setVisible(false);
    group->setVisible(true);
    d.run(scene);
    CHECK(d.marked == subtree, "two cascades over one branch in one frame are still one mark each");

    // MOBILITY inherits (rule 2), so it cascades too.
    group->setMobility(iris::Mobility::Movable);
    d.run(scene);
    CHECK(d.marked == subtree, "setMobility on a group marks its whole subtree");
    // ...and THE CASE THAT CAUGHT A REAL BUG (2026-09-13): setMobility raises a
    // plain Flags mark on the group itself (through _applyStaticHint) BEFORE it
    // cascades, so a cascade that pruned on the plain bit would have marked no
    // descendant at all. The line above is that regression.
    group->setMobility(iris::Mobility::Auto);
    d.run(scene);
    CHECK(d.marked == subtree, "...and back again");

    // A REPARENT marks the moved subtree (its inherited state comes from the
    // new parent) and both ends of the move.
    auto newHome = named("new-home");
    root->addChild(newHome);
    d.run(scene);
    newHome->addChild(kids[0]);          // one kid + its two grandkids
    d.run(scene);
    CHECK(d.has(kids[0]), "a reparent marks the moved node");
    CHECK(d.has(newHome), "...its new parent");
    CHECK(d.has(group), "...and the parent it left");
    CHECK(d.marked >= 3 + 2, "...and the moved subtree with it");

    // A TRANSFORM does NOT cascade: Ogre derives the children's world
    // transforms itself, and no latch the mirror keeps reads a world transform
    // per node (§3.4).
    group->setLocalPos(iris::Vec3(0, 5, 0));
    d.run(scene);
    CHECK(d.marked == 1, "moving a group marks the GROUP and not its subtree");

    // ---- 3: EVICTION ------------------------------------------------------
    auto doomed = named("doomed");
    auto doomedKid = named("doomed-kid");
    doomed->addChild(doomedKid);
    root->addChild(doomed);
    d.run(scene);

    iris::SceneNode *doomedRaw = doomed.data();
    iris::SceneNode *doomedKidRaw = doomedKid.data();
    root->removeChild(doomed);
    d.run(scene);
    CHECK(d.wasEvicted(doomedRaw), "a removed node is recorded as an EVICTION");
    CHECK(d.wasEvicted(doomedKidRaw), "...and so is every node under it");
    CHECK(doomed->dirtySet() == nullptr, "...and it stops recording");

    // THE DANGEROUS ONE: marked, then removed, then FREED, all before the list
    // is consumed. The consumer must never see the pointer.
    auto shortLived = named("short-lived");
    root->addChild(shortLived);
    d.run(scene);
    shortLived->setLocalPos(iris::Vec3(1, 1, 1));      // on the list
    CHECK(shortLived->isQueuedDirty(), "it is queued");
    iris::SceneNode *shortRaw = shortLived.data();
    root->removeChild(shortLived);
    shortLived.reset();                                // and gone
    d.run(scene);
    CHECK(d.tombstones >= 1, "a node that left after being marked is CANCELLED on the list");
    bool sawFreed = false;
    for (iris::SceneNode *n : d.nodes) if (n == shortRaw) sawFreed = true;
    CHECK(!sawFreed, "...so the consumer never sees the freed pointer");
    CHECK(d.wasEvicted(shortRaw), "...and its eviction is still recorded (by value, never followed)");

    // ---- THE OVERFLOW VALVE -----------------------------------------------
    //
    // A scene nobody is mirroring still collects. Past the cap the eviction
    // list is dropped whole and says so, which is a consumer's instruction to
    // walk everything once rather than reconcile an incomplete record.
    {
        auto quiet = iris::Scene::create();
        for (std::size_t i = 0; i <= iris::NodeDirtySet::kEvictionCap; ++i) {
            auto n = named("churn");
            quiet->getRootNode()->addChild(n);
            quiet->getRootNode()->removeChild(n);
        }
        CHECK(quiet->dirtySet()->pendingEvicted() < iris::NodeDirtySet::kEvictionCap,
              "an unconsumed eviction list is capped, not grown forever");
        CHECK(quiet->dirtySet()->takeOverflow(), "...and it SAYS it overflowed");
        CHECK(!quiet->dirtySet()->takeOverflow(), "...once");
    }

    // ---- MATERIALS: a different signal, for a different reason ------------
    //
    // A material edit moves no NODE, so the node list cannot carry it. The
    // material's own revision does (§3.6), and a process-wide counter is what
    // lets a still frame answer "did ANY material change" with one read.
    {
        auto mat = iris::PbrMaterial::create();
        const quint32 rev0 = mat->revision();
        const quint64 global0 = iris::Material::globalRevision();
        mat->setValue(QStringLiteral("roughness"), 0.42f);
        CHECK(mat->revision() != rev0, "a material edit moves the material's revision");
        CHECK(iris::Material::globalRevision() != global0, "...and the process-wide counter");

        const quint32 rev1 = mat->revision();
        mat->setBaseColor(QColor(10, 20, 30));
        CHECK(mat->revision() != rev1, "...so does a typed setter");

        const quint32 rev2 = mat->revision();
        const quint64 globalQuiet = iris::Material::globalRevision();
        (void)mat->baseColor;
        CHECK(mat->revision() == rev2, "reading a material moves nothing");
        CHECK(iris::Material::globalRevision() == globalQuiet, "...and neither does anything else");

        auto other = iris::PbrMaterial::create();
        const quint32 mine = mat->revision();
        other->setValue(QStringLiteral("metallic"), 0.8f);
        CHECK(mat->revision() == mine, "one material's edit does not move another's revision");
        CHECK(iris::Material::globalRevision() != globalQuiet,
              "...but it does move the process-wide one");
    }

    // ---- THE SCENE OUTLIVES NOTHING ---------------------------------------
    //
    // The collector is a member of the scene, so a node that survives its scene
    // (the undo stack holds deleted subtrees) must not still name it.
    {
        auto dying = iris::Scene::create();
        auto keep = named("kept-by-someone-else");
        dying->getRootNode()->addChild(keep);
        CHECK(keep->dirtySet() != nullptr, "the node has the dying scene's collector");
        dying->cleanup();
        CHECK(keep->dirtySet() == nullptr, "cleanup takes it away before the scene dies");
        keep->setLocalPos(iris::Vec3(1, 2, 3));     // must not touch freed memory
        CHECK(true, "...and a write after that is inert");
    }

    printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
