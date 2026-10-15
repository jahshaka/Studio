// mirror.doc_store — THE DOCUMENT'S TRANSFORM STORE AND THE ENGINE'S OWN NODES AGREE (ENGINE-UPGRADE-U2). The
// document owns its transforms and its hierarchy (nodegraph.h's store: local TRS, parent, ordered children, a world
// derived by Ogre's own composition rule); the ENGINE owns its own nodes, told each written node's LOCAL transform,
// parent and graph class through the frame record (the extract), composing the rest itself. So, on a 1,000-node
// fixture with non-uniform scales, mirrored and rendered:
//   * every node's WORLD from the store equals Ogre's derived transform after the frame (position per metre of scene,
//     rotation per component, scale per unit of scale) to 1e-6 after a keepTransform reparent of 50 nodes,
//     a non-uniform scale on 50, a rotation on 50, zero-scale parents, and a reparent into and out of them;
//   * every node's PARENT and CHILDREN from the store equal Ogre's (a count row; the ORDER is the store's alone —
//     Ogre's removeChild is swap-and-pop — and case H pins that a removal keeps it);
//   * THE RE-EXPRESSION (SC1-REEXPRESS-SCALE-1): a keepTransform reparent under a rotated, non-uniformly scaled
//     parent keeps the world pose; under a zero-scale parent it stores no NaN or infinity.
// 2 FRAMES per case, 7 cases: 14 engine frames (H renders none); I renders 9 (the extract, counted). Law: equality with the engine, and the round trip of the inverse.
// FIRST, before any Qt header: EnginePrivate.h names a `slots` variable Qt's keyword macro would eat.
#include "EnginePrivate.h"   // OgreScene::node: the engine's own node for a NodeId (a suite read)
#include <QGuiApplication>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "OgreSceneNode.h"

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/trs.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "../support/testmesh.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

namespace {

struct Lcg {
    std::uint32_t s = 0x9e3779b9u;
    float unit() { s = s * 1664525u + 1013904223u; return float(s >> 8) / float(1u << 24); }   // [0, 1)
    float range(float a, float b) { return a + (b - a) * unit(); }
    int below(int n) { return int(unit() * float(n)) % n; }
};

iris::Quat randomRot(Lcg &r)
{
    iris::Quat q(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1), r.range(-1, 1));
    return q.normalized();
}

int depthOf(const iris::SceneNode *n)
{
    int d = 0;
    for (const iris::graph::DocNode *p = n->docNode().parent; p; p = p->parent) ++d;
    return d;
}

bool isUnder(const iris::SceneNode *n, const iris::SceneNode *ancestor)
{
    for (const iris::graph::DocNode *p = &n->docNode(); p; p = p->parent)
        if (p->owner == ancestor) return true;
    return false;
}

const SceneMirror *gMirror = nullptr;
const jahshaka::engine::detail::OgreScene *gScene = nullptr;
/// The ENGINE's own node for a document node (the mirror's NodeId -> the scene's Ogre node).
Ogre::SceneNode *ogre(const iris::SceneNode *n) { return gScene->node(gMirror->engineNode(n)); }

bool finite(const iris::Vec3 &v) { return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z()); }

struct Diff { double pos = 0, rot = 0, scale = 0; int exact = 0, nodes = 0; double extent = 1; };

// The store's world against Ogre's derived transform, as the last updateSceneGraph left it (no `_get*Updated`: that
// is the engine's read, and the frame has just made the cache current).
Diff compare(const std::vector<iris::SceneNodePtr> &nodes)
{
    Diff d;
    for (const auto &n : nodes) {
        const iris::graph::Trs w = iris::graph::world(n->docNode());
        d.extent = std::max(d.extent, double(std::fabs(w.pos.x())));
        d.extent = std::max(d.extent, double(std::fabs(w.pos.y())));
        d.extent = std::max(d.extent, double(std::fabs(w.pos.z())));
    }
    for (const auto &n : nodes) {
        const iris::graph::Trs w = iris::graph::world(n->docNode());
        const Ogre::SceneNode *o = ogre(n.data());
        const Ogre::Vector3 p = o->_getDerivedPosition();
        const Ogre::Quaternion q = o->_getDerivedOrientation();
        const Ogre::Vector3 s = o->_getDerivedScale();
        const double dp = std::max({ std::fabs(double(w.pos.x()) - p.x), std::fabs(double(w.pos.y()) - p.y),
                                     std::fabs(double(w.pos.z()) - p.z) }) / d.extent;   // per metre of scene
        const double dq = std::max({ std::fabs(double(w.rot.scalar()) - q.w), std::fabs(double(w.rot.x()) - q.x),
                                     std::fabs(double(w.rot.y()) - q.y), std::fabs(double(w.rot.z()) - q.z) });
        // Scale per unit of scale (a chain of eight non-uniform factors reaches tens).
        const auto rel = [](double a, double b) { return std::fabs(a - b) / std::max(1.0, std::fabs(b)); };
        const double ds = std::max({ rel(w.scale.x(), s.x), rel(w.scale.y(), s.y), rel(w.scale.z(), s.z) });
        d.pos = std::max(d.pos, dp);
        d.rot = std::max(d.rot, dq);
        d.scale = std::max(d.scale, ds);
        if (w.pos.x() == p.x && w.pos.y() == p.y && w.pos.z() == p.z && w.rot.scalar() == q.w && w.rot.x() == q.x &&
            w.rot.y() == q.y && w.rot.z() == q.z && w.scale.x() == s.x && w.scale.y() == s.y && w.scale.z() == s.z)
            ++d.exact;
        ++d.nodes;
    }
    return d;
}

// Parent and children: the store's against the ENGINE's hierarchy (document-driven Ogre children only). The children
// as a SET: Ogre-Next's Node::removeChild is swap-and-pop (efficientVectorRemove, OgreNode.cpp:547), so Ogre's child
// ORDER is not the document's after any removal — the store's order is (case H pins it).
int hierarchyMismatches(const std::vector<iris::SceneNodePtr> &nodes)
{
    int bad = 0;
    for (const auto &n : nodes) {
        const Ogre::SceneNode *o = ogre(n.data());
        if (!o) { ++bad; continue; }
        const iris::SceneNode *sp = n->getParent().data();
        const Ogre::Node *want = (sp && gMirror->engineNode(sp)) ? ogre(sp) : nullptr;   // null = the scene root
        const Ogre::Node *got = o->getParent();
        if (want ? got != want : (got && gMirror->engineNode(sp) != 0)) { ++bad; continue; }
        std::vector<const Ogre::Node *> ogreKids, storeKids;
        for (std::size_t i = 0; i < o->numChildren(); ++i) {
            const Ogre::Node *c = o->getChild(i);
            for (int k = 0; k < n->childCount(); ++k)
                if (ogre(n->childAt(k)) == c) { ogreKids.push_back(c); break; }
        }
        for (int i = 0; i < n->childCount(); ++i) storeKids.push_back(ogre(n->childAt(i)));
        std::sort(ogreKids.begin(), ogreKids.end());
        std::sort(storeKids.begin(), storeKids.end());
        if (ogreKids != storeKids) ++bad;
    }
    return bad;
}

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const auto cfg = enginetest::testEngineConfig("test_doc_store-ogre.log");
    std::string err;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("docstore", 32, 32, Colour(0, 0, 0));
    Scene *target = engine->createScene("docstore");
    if (!view || !target) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(target);
    enginetest::testCameraLookAt(view, Vec3(0, 0, 30), Vec3(0, 0, 0));

    // ---- the fixture: 1,000 nodes, depth <= 8, non-uniform scales (some mirrored), random rotations ----
    Lcg rng;
    auto doc = iris::Scene::create();
    std::vector<iris::SceneNodePtr> nodes;
    nodes.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        auto n = iris::SceneNode::create();
        n->setLocalPos(iris::Vec3(rng.range(-3, 3), rng.range(-3, 3), rng.range(-3, 3)));
        n->setLocalRot(randomRot(rng));
        iris::Vec3 sc(rng.range(0.6f, 1.6f), rng.range(0.6f, 1.6f), rng.range(0.6f, 1.6f));
        if (rng.unit() < 0.05f) sc.setX(-sc.x());
        n->setLocalScale(sc);
        iris::SceneNode *parent = doc->getRootNode().data();
        if (i >= 10) {
            for (int tries = 0; tries < 8; ++tries) {
                iris::SceneNode *cand = nodes[std::size_t(rng.below(i))].data();
                if (depthOf(cand) < 8) { parent = cand; break; }
            }
        }
        parent->addChild(n, false);
        nodes.push_back(n);
    }
    SceneMirror mirror(target);
    mirror.setSource(doc);
    gMirror = &mirror;
    gScene = static_cast<const jahshaka::engine::detail::OgreScene *>(target);
    auto frames = [&] { mirror.sync(); enginetest::frames(engine, 1); enginetest::frames(engine, 1); };
    auto verdict = [&](const char *tag) {
        const Diff d = compare(nodes);
        const int h = hierarchyMismatches(nodes);
        CHECK(d.pos <= 1e-6 && d.rot <= 1e-6 && d.scale <= 1e-6,
              "%-28s the store's world = the engine's: pos %.2e/m (scene %.0f m)  rot %.2e  scale %.2e  "
              "(%d of %d bit-exact)", tag, d.pos, d.extent, d.rot, d.scale, d.exact, d.nodes);
        CHECK(h == 0, "%-28s parent + children from the store = Ogre's (%d mismatches of %zu)", tag, h,
              nodes.size());
    };
    frames();
    verdict("A initial");



    // B: 50 keepTransform reparents (the re-expression: the world must hold, measured on the store itself).
    {
        double worst = 0;
        int done = 0;
        for (int k = 0; k < 400 && done < 50; ++k) {
            iris::SceneNodePtr n = nodes[std::size_t(rng.below(1000))];
            iris::SceneNodePtr p = nodes[std::size_t(rng.below(1000))];
            if (n == p || isUnder(p.data(), n.data()) || depthOf(p.data()) >= 7) continue;
            const iris::graph::Trs before = iris::graph::world(n->docNode());
            p->addChild(n, true);
            const iris::graph::Trs after = iris::graph::world(n->docNode());
            const double e = std::max({ std::fabs(double(before.pos.x()) - after.pos.x()),
                                        std::fabs(double(before.pos.y()) - after.pos.y()),
                                        std::fabs(double(before.pos.z()) - after.pos.z()) }) /
                             std::max(1.0, double(before.pos.length()));
            worst = std::max(worst, e);
            ++done;
        }
        CHECK(done == 50 && worst <= 1e-5, "B keepTransform x%d keeps the world (worst %.2e per metre)", done, worst);
    }
    frames();
    verdict("B reparent x50");

    for (int k = 0; k < 50; ++k)
        nodes[std::size_t(rng.below(1000))]->setLocalScale(
            iris::Vec3(rng.range(0.5f, 2.0f), rng.range(0.5f, 2.0f), rng.range(0.5f, 2.0f)));
    frames();
    verdict("C non-uniform scale x50");

    for (int k = 0; k < 50; ++k) nodes[std::size_t(rng.below(1000))]->setLocalRot(randomRot(rng));
    frames();
    verdict("D rotation x50");

    // E: ZERO-SCALE PARENTS — five nodes that have children flattened on one axis each.
    std::vector<iris::SceneNodePtr> flat;
    for (const auto &n : nodes)
        if (flat.size() < 5 && n->childCount() > 0 && depthOf(n.data()) < 6) flat.push_back(n);
    for (std::size_t i = 0; i < flat.size(); ++i) {
        iris::Vec3 s = flat[i]->getLocalScale();
        s[int(i % 3)] = 0.0f;
        flat[i]->setLocalScale(s);
    }
    frames();
    verdict("E zero-scale parents");

    // F: reparent INTO and OUT OF the flattened parents, keepTransform, plus a world write under one
    // (SC1-REEXPRESS-SCALE-1: the singular axis is guarded per axis — no NaN, no infinity stored).
    {
        bool allFinite = true;
        for (std::size_t i = 0; i < flat.size(); ++i) {
            iris::SceneNodePtr into = nodes[999 - i * 7];
            if (isUnder(flat[i].data(), into.data()) || depthOf(flat[i].data()) >= 7) continue;
            flat[i]->addChild(into, true);                                // into: the flattened axis is singular
            into->setGlobalTransform(into->getGlobalTransform());         // a world write under it
            iris::SceneNodePtr outOf = flat[i]->childAt(0)->sharedFromThis();
            doc->getRootNode()->addChild(outOf, true);                    // out of: the world was flattened
            for (const auto &n : { into, outOf }) {
                allFinite = allFinite && finite(n->getLocalPos()) && finite(n->getLocalScale()) &&
                            std::isfinite(n->getLocalRot().scalar()) && std::isfinite(n->getLocalRot().x());
            }
        }
        CHECK(allFinite, "F re-expression under a zero-scale parent stores no NaN / infinity");
    }
    frames();
    verdict("F into + out of zero scale");

    // G: THE SHEARED PARENT. A parent scaled (3, 1, 1) and rotated, a child rotated 45 degrees under it: a matrix
    // product of the two would carry the shear Ogre's rule drops; the store and the engine agree (verdict), and the
    // keepTransform reparent out from under it keeps the world.
    {
        auto p = iris::SceneNode::create();
        p->setLocalScale(iris::Vec3(3, 1, 1));
        p->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(0, 1, 0), 30.0f));
        auto c = iris::SceneNode::create();
        c->setLocalPos(iris::Vec3(1, 2, 0));
        c->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(0, 0, 1), 45.0f));
        doc->getRootNode()->addChild(p, false);
        p->addChild(c, false);
        nodes.push_back(p);
        nodes.push_back(c);
        // What a MATRIX product says (the premise the store's design rests on): its basis is not orthogonal.
        iris::Vec3 mp, ms;
        iris::Quat mr;
        const float shear = iris::decomposeTRS(p->getGlobalTransform() * c->getLocalTransform(), mp, mr, ms);
        std::printf("    a matrix product of the chain carries a shear of %.3f (|cos| between basis axes); "
                    "Ogre's TRS rule carries none\n", shear);
        CHECK(shear > 0.1f, "G the premise: the matrix product IS sheared here (%.3f)", shear);
        const iris::graph::Trs before = iris::graph::world(c->docNode());
        doc->getRootNode()->addChild(c, true);
        const iris::graph::Trs after = iris::graph::world(c->docNode());
        const double e = std::max({ std::fabs(double(before.pos.x()) - after.pos.x()),
                                    std::fabs(double(before.pos.y()) - after.pos.y()),
                                    std::fabs(double(before.pos.z()) - after.pos.z()),
                                    std::fabs(double(before.rot.scalar()) - after.rot.scalar()),
                                    std::fabs(double(before.scale.x()) - after.scale.x()) });
        CHECK(e <= 1e-6, "G keepTransform out from under the sheared parent keeps the world (%.2e)", e);
        p->addChild(c, true);
        frames();
        verdict("G sheared parent");
    }

    // H: THE SIBLING ORDER IS THE DOCUMENT'S. Four children a b c d; b leaves: the store says a c d (Ogre's own
    // vector, swap-and-pop, says a d c — the order the outliner, undo's sibling index and the file used to inherit).
    {
        auto p = iris::SceneNode::create();
        doc->getRootNode()->addChild(p, false);
        iris::SceneNodePtr k[4];
        for (auto &c : k) { c = iris::SceneNode::create(); p->addChild(c, false); }
        doc->getRootNode()->addChild(k[1], true);
        CHECK(p->childCount() == 3 && p->childAt(0) == k[0].data() && p->childAt(1) == k[2].data() &&
                  p->childAt(2) == k[3].data() && k[3]->siblingIndex() == 2,
              "H a removal keeps the siblings' order (a c d; d's sibling index 2)");
        p->insertChild(0, k[1], true);
        CHECK(p->childAt(0) == k[1].data() && k[0]->siblingIndex() == 1, "H an insert at index 0 lands at 0");
    }

    // I: THE EXTRACT, COUNTED (U3's bar, the served half): the frame record carries one NodeRecord per WRITTEN node —
    // a still frame none, one mover one, the root of a 10,000-node subtree one (its LOCAL transform; the engine
    // composes the subtree itself) — and the subtree lands where the store says.
    {
        frames();                                                   // anything owed from H lands first
        frames();
        const unsigned still = engine->frameInputStatus().nodeRecords;
        CHECK(still == 0, "I a still frame sends 0 node records (got %u)", still);
        nodes[17]->setLocalPos(nodes[17]->getLocalPos() + iris::Vec3(0.1f, 0, 0));
        mirror.sync(); enginetest::frames(engine, 1);
        const unsigned one = engine->frameInputStatus().nodeRecords;
        CHECK(one == 1, "I one mover sends 1 node record (got %u)", one);
        auto big = iris::SceneNode::create();
        doc->getRootNode()->addChild(big, false);
        std::vector<iris::SceneNodePtr> bigKids;
        bigKids.reserve(10000);
        for (int i = 0; i < 10000; ++i) {
            auto c = iris::SceneNode::create();
            c->setLocalPos(iris::Vec3(float(i % 100) * 0.1f, float(i / 100) * 0.1f, 0.0f));
            big->addChild(c, false);
            bigKids.push_back(c);
        }
        frames();                                                   // mirrored: 10,001 records, once
        big->setLocalPos(iris::Vec3(5, 0, 0));
        big->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(0, 1, 0), 20.0f));
        mirror.sync(); enginetest::frames(engine, 1);
        const unsigned rootMove = engine->frameInputStatus().nodeRecords;
        enginetest::frames(engine, 1);
        CHECK(rootMove == 1, "I the root of a 10,000-node subtree written sends 1 node record (got %u)", rootMove);
        double worst = 0;
        for (const auto &c : bigKids) {
            const iris::graph::Trs w = iris::graph::world(c->docNode());
            const Ogre::Vector3 p = ogre(c.data())->_getDerivedPosition();
            worst = std::max({ worst, std::fabs(double(w.pos.x()) - p.x), std::fabs(double(w.pos.y()) - p.y),
                               std::fabs(double(w.pos.z()) - p.z) });
        }
        CHECK(worst <= 1e-5, "I ...and the engine composed the subtree where the store says (worst %.2e m)", worst);
    }

    // J: A REPARENT RE-TELLS THE WHOLE SUBTREE ITS CLASS (the stage-2 read's defect). Ogre's Node::setParent migrates
    // the reparented node's subtree into the new parent's memory manager and leaves the attachments where they were;
    // each document descendant must get its OWN class back. (J1) a Movable group with a mesh child dragged under a
    // static group: the child stays dynamic, Item and node, and after the group moves it DRAWS where the store says
    // (red before the fix: the child left in the static manager under a dynamic parent, drawn at its old pose).
    // (J2) the reverse: a static group with a mesh child moved from under a static group to the root keeps the child
    // static (red before the fix: the child dynamic for good, the static multiplier lost).
    {
        const iris::MeshPtr cube = testmesh::load(":assets/models/cube.obj");
        const iris::MaterialPtr mat = iris::PbrMaterial::create();
        auto meshChild = [&](const iris::Vec3 &at) {
            auto mn = iris::MeshNode::create();
            mn->setMesh(cube);
            mn->setMaterial(mat);
            mn->setLocalPos(at);
            return mn;
        };
        auto ground = iris::SceneNode::create();
        auto groundKid = meshChild(iris::Vec3(0, -1, 0));
        auto sg = iris::SceneNode::create();
        auto sgKid = meshChild(iris::Vec3(0, 1, 0));
        auto mover = iris::SceneNode::create();
        auto moverKid = meshChild(iris::Vec3(1, 0, 0));
        mover->setMobility(iris::Mobility::Movable);
        ground->setLocalPos(iris::Vec3(0, 0, -4));
        mover->setLocalPos(iris::Vec3(3, 0, 0));
        doc->getRootNode()->addChild(ground, false);
        ground->addChild(groundKid, false);
        ground->addChild(sg, false);
        sg->addChild(sgKid, false);
        doc->getRootNode()->addChild(mover, false);
        mover->addChild(moverKid, false);
        ground->applyStaticDefaults();
        mover->applyStaticDefaults();
        for (int k = 0; k < 4000; ++k) {
            mirror.sync();
            enginetest::frames(engine, 1);
            if (k > 2 && !mirror.staticSettleInProgress()) break;
        }
        // The engine's class of a node and of its attachments, against the document's.
        auto classAgrees = [&](const iris::SceneNodePtr &n) {
            Ogre::SceneNode *o = ogre(n.data());
            if (!o || o->isStatic() != n->isStaticInGraph()) return false;
            for (size_t i = 0; i < o->numAttachedObjects(); ++i)
                if (o->getAttachedObject(i)->isStatic() != o->isStatic()) return false;
            return true;
        };
        CHECK(ground->isStaticInGraph() && sgKid->isStaticInGraph() && !mover->isStaticInGraph() &&
                  !moverKid->isStaticInGraph() && classAgrees(ground) && classAgrees(sgKid) && classAgrees(mover) &&
                  classAgrees(moverKid),
              "J the fixture: a static group (static mesh child), a Movable group (dynamic mesh child), engine = document");
        ground->addChild(mover, true);                          // the reparent command's own call
        frames();
        CHECK(!mover->isStaticInGraph() && !moverKid->isStaticInGraph() && classAgrees(mover) &&
                  classAgrees(moverKid),
              "J1 a Movable group dragged under a static group: it and its mesh child stay dynamic, node and Item "
              "(engine child static: %d)", int(ogre(moverKid.data())->isStatic()));
        for (int step = 1; step <= 3; ++step) {
            mover->setLocalPos(mover->getLocalPos() + iris::Vec3(0.5f, 0.25f, 0));
            frames();
        }
        {
            Vec3 drawn; Quat qd;
            const NodeId id = mirror.engineNode(moverKid.data());
            const bool ok = id && target->nodeWorldPoseDrawn(id, drawn, qd);
            const iris::graph::Trs w = iris::graph::world(moverKid->docNode());
            const double e = std::fabs(double(w.pos.x()) - drawn.x) + std::fabs(double(w.pos.y()) - drawn.y) +
                             std::fabs(double(w.pos.z()) - drawn.z);
            CHECK(ok && e < 1e-4, "J1 ...the group moved three times: its child DRAWS where the store says "
                  "(|drawn - store| = %.2e m)", e);
        }
        doc->getRootNode()->addChild(sg, false);                // a static group out to the root
        frames();
        CHECK(sg->isStaticInGraph() && sgKid->isStaticInGraph() && classAgrees(sg) && classAgrees(sgKid),
              "J2 a static group moved to the root keeps its mesh child static, node and Item (engine child "
              "static: %d)", int(ogre(sgKid.data())->isStatic()));
        {
            Vec3 drawn; Quat qd;
            const NodeId id = mirror.engineNode(sgKid.data());
            const bool ok = id && target->nodeWorldPoseDrawn(id, drawn, qd);
            const iris::graph::Trs w = iris::graph::world(sgKid->docNode());
            const double e = std::fabs(double(w.pos.x()) - drawn.x) + std::fabs(double(w.pos.y()) - drawn.y) +
                             std::fabs(double(w.pos.z()) - drawn.z);
            CHECK(ok && e < 1e-4, "J2 ...and draws where the store says (|drawn - store| = %.2e m)", e);
        }
    }

    mirror.setSource(nullptr);
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
