// document.physics_writeback — THE PHYSICS WRITE-BACK WRITES ONLY WHAT MOVED,
// AND THE DOCUMENT IT LEAVES IS THE ONE THE WRITE-EVERYTHING LOOP LEFT
// (SPEED-CPU item 1, perf audit 2026-10-03 D1).
//
// Scene::advance used to write EVERY rigid body's world transform back into its
// node on EVERY frame, asleep or not — so a resting pile made every played frame
// a mover frame: a transform write per body, a mirror visit per body, and the
// movement epoch moving, which re-runs every gated engine walk over every item.
// Bullet stops integrating a body whose island sleeps; the write-back now writes
// the bodies that were ACTIVE in one of the frame's steps, and only re-checks a
// sleeping one when something else in the document wrote a transform since the
// last write-back (a reparent, an undo, a moved parent, a property track).
//
// THE ARM, and why it is exact. After a frame, the OLD write — node->
// setGlobalPosRot(the body's world transform) — is performed here for every
// body, and the node's local position and rotation must not change by a single
// bit: what the write-everything loop would have produced is what the document
// already holds. Bullet never reads a node, so the bodies' own evolution is the
// same under both loops, and "the old write is a no-op after every checked
// frame" is "the poses are equal over N frames". The check itself is a
// transform write, so it runs every OTHER frame: the frame after a check takes
// the re-check path and the frame after that the moved-only path, and both are
// verified. A second pass checks EVERY frame (the re-check path alone).
//
// THE GAIN, counted rather than timed: once the pile sleeps, a frame makes ZERO
// transform writes (graph::transformWrites() does not move across advance()).
//
// THE EDGES the write-everything loop handled by accident and this one must
// handle on purpose — each re-pinned exactly as before:
//   * a sleeping body's node written WITHOUT telling the body (an undo, a
//     property track): pinned back to the body's pose on the next frame;
//   * a sleeping body under a GROUP that moves: its node re-pinned to the
//     body's world pose (the body did not move, the parent did);
//   * a body NESTED under another body that moves.
// Document only: NULL render system, no display.

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicsproperties.h"

#include "btBulletDynamicsCommon.h"

#include <QGuiApplication>

#include <cstdio>
#include <cstring>
#include <vector>

#include "../support/documentgraph.h"

static int gFailures = 0;
#define CHECK(cond, msg)                                                                  \
    do {                                                                                  \
        if (cond) std::printf("ok: %s\n", msg);                                           \
        else { std::printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++gFailures; } \
    } while (0)

namespace {

constexpr float kDt = 1.0f / 60.0f;

iris::SceneNodePtr box(const QString &guid, const iris::Vec3 &pos, float mass, const iris::Vec3 &scale)
{
    auto n = iris::MeshNode::create();
    n->setGUID(guid);
    n->setLocalPos(pos);
    n->setLocalScale(scale);
    n->isPhysicsBody = true;
    n->physicsProperty.objectMass = mass;
    n->physicsProperty.shape = iris::PhysicsCollisionShape::Cube;
    n->physicsProperty.type = mass > 0.0f ? iris::PhysicsType::RigidBody : iris::PhysicsType::Static;
    return n;
}

bool sameBits(const iris::Vec3 &a, const iris::Vec3 &b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool sameBits(const iris::Quat &a, const iris::Quat &b)
{
    return a.scalar() == b.scalar() && a.x() == b.x() && a.y() == b.y() && a.z() == b.z() &&
           std::signbit(a.scalar()) == std::signbit(b.scalar());
}

/// THE OLD WRITE, for every body: returns how many nodes it CHANGED.
int oldWriteChanges(iris::Scene &scene, iris::Environment &env, const std::vector<iris::SceneNodePtr> &bodies)
{
    int changed = 0;
    for (const iris::SceneNodePtr &n : bodies) {
        btRigidBody *body = env.hashBodies.value(n->getGUID(), nullptr);
        if (!body) continue;
        const btTransform &t = body->getWorldTransform();
        const btVector3 p = t.getOrigin();
        const btQuaternion r = t.getRotation();
        const iris::Vec3 pos0 = n->getLocalPos();
        const iris::Quat rot0 = n->getLocalRot();
        n->setGlobalPosRot(iris::Vec3(p.x(), p.y(), p.z()), iris::Quat(r.w(), r.x(), r.y(), r.z()));
        if (!sameBits(pos0, n->getLocalPos()) || !sameBits(rot0, n->getLocalRot())) ++changed;
    }
    (void)scene;
    return changed;
}

struct Pile
{
    iris::ScenePtr scene;
    iris::SceneNodePtr group;          ///< a plain (non-body) parent of some bodies
    iris::SceneNodePtr carrier;        ///< a body with a body nested under it
    std::vector<iris::SceneNodePtr> bodies;   ///< every DYNAMIC body
};

Pile buildPile(int side)
{
    Pile p;
    p.scene = iris::Scene::create();
    auto root = p.scene->getRootNode();
    auto ground = box("ground", iris::Vec3(0, -0.5f, 0), 0.0f, iris::Vec3(60, 0.5f, 60));
    root->addChild(ground);
    p.group = iris::SceneNode::create();
    p.group->setGUID("group");
    p.group->setLocalPos(iris::Vec3(0, 0, 30));
    root->addChild(p.group);
    for (int i = 0; i < side; ++i)
        for (int j = 0; j < side; ++j) {
            const QString guid = QStringLiteral("box-%1-%2").arg(i).arg(j);
            const iris::Vec3 at(float(i - side / 2) * 1.0f, 0.45f, float(j - side / 2) * 1.0f);
            auto b = box(guid, at, 1.0f, iris::Vec3(0.4f, 0.4f, 0.4f));
            // A quarter of the pile lives under the group (a world position
            // the group's offset is taken off), the rest at the root.
            if (j % 4 == 0) { b->setLocalPos(at - p.group->getLocalPos()); p.group->addChild(b); }
            else root->addChild(b);
            p.bodies.push_back(b);
        }
    // A body nested under another body (the carrier falls from higher up).
    p.carrier = box("carrier", iris::Vec3(-20, 3.0f, -20), 1.0f, iris::Vec3(0.5f, 0.5f, 0.5f));
    root->addChild(p.carrier);
    auto rider = box("rider", iris::Vec3(0, 3.0f, 0), 1.0f, iris::Vec3(0.3f, 0.3f, 0.3f));
    p.carrier->addChild(rider);
    p.bodies.push_back(p.carrier);
    p.bodies.push_back(rider);
    return p;
}

bool allAsleep(iris::Environment &env)
{
    for (auto it = env.hashBodies.cbegin(); it != env.hashBodies.cend(); ++it)
        if (it.value()->getInvMass() > 0.0f && it.value()->isActive()) return false;
    return true;
}

/// One run: `checkEvery` = 2 (both paths) or 1 (the re-check path alone).
void run(int checkEvery)
{
    std::printf("-- the old write checked every %d frame(s)\n", checkEvery);
    Pile pile = buildPile(12);
    auto env = pile.scene->getPhysicsEnvironment();
    env->initializePhysicsWorldFromScene(pile.scene->getRootNode());
    env->simulatePhysics();

    int changedTotal = 0, checks = 0, firstAsleep = -1;
    unsigned long long fallingWrites = 0, restWrites = 0;
    int restFrames = 0;
    for (int f = 1; f <= 600; ++f) {
        const unsigned long long w0 = iris::graph::transformWrites();
        pile.scene->advance(kDt);
        const unsigned long long w = iris::graph::transformWrites() - w0;
        const bool asleep = allAsleep(*env);
        if (asleep && firstAsleep < 0) firstAsleep = f;
        if (f <= 30) fallingWrites += w;
        // THE REST WINDOW: from 30 frames after the whole pile slept.
        if (firstAsleep > 0 && f >= firstAsleep + 30) { restWrites += w; ++restFrames; }
        if (f % checkEvery == 0) {
            changedTotal += oldWriteChanges(*pile.scene, *env, pile.bodies);
            ++checks;
        }
    }
    std::printf("    pile asleep at frame %d; writes: first 30 frames %llu, rest window %llu over %d frames;"
                " the old write changed %d node(s) over %d checks\n",
                firstAsleep, fallingWrites, restWrites, restFrames, changedTotal, checks);
    CHECK(firstAsleep > 0 && firstAsleep < 400, "the pile falls asleep (Bullet's own deactivation)");
    CHECK(fallingWrites > 0, "falling bodies are written back");
    CHECK(restFrames > 100, "the rest window is long enough to mean something");
    CHECK(restWrites == 0, "a sleeping pile's frames make ZERO transform writes");
    CHECK(changedTotal == 0, "after every checked frame the old write-everything loop would change nothing");

    // THE EDGES, at rest. Each is one frame, then the old write must again be a
    // no-op — i.e. the node was re-pinned to its body exactly as before.
    {
        // A sleeping body's node written without the body (an undo, a track).
        const iris::SceneNodePtr &n = pile.bodies[5];
        n->setLocalPos(n->getLocalPos() + iris::Vec3(1.0f, 0, 0));
        pile.scene->advance(kDt);
        CHECK(oldWriteChanges(*pile.scene, *env, pile.bodies) == 0,
              "a sleeping body's node written behind its back is pinned back to the body");
    }
    {
        // The GROUP moves: its sleeping bodies keep their world pose.
        pile.group->setLocalPos(pile.group->getLocalPos() + iris::Vec3(2.0f, 0, 0));
        pile.scene->advance(kDt);
        CHECK(oldWriteChanges(*pile.scene, *env, pile.bodies) == 0,
              "sleeping bodies under a moved group are pinned to their bodies' world pose");
    }
    {
        // The carrier is woken and moved; its nested rider (asleep) is pinned.
        CHECK(env->syncBodyToNode(pile.carrier), "the carrier body takes its node's pose");
        pile.carrier->setLocalPos(pile.carrier->getLocalPos() + iris::Vec3(0, 0.5f, 0));
        env->syncBodyToNode(pile.carrier);
        for (int i = 0; i < 3; ++i) pile.scene->advance(kDt);
        CHECK(oldWriteChanges(*pile.scene, *env, pile.bodies) == 0,
              "a body nested under a moving body is pinned to its own body");
    }
    env->stopPhysics();
    env->destroyPhysicsWorld();
}

}  // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("physics-writeback-ogre.log");
    if (!graph.ok()) { std::printf("FAIL: no document graph\n"); return 1; }
    run(2);
    run(1);
    std::printf(gFailures ? "FAILED: %d\n" : "all passed\n", gFailures);
    return gFailures ? 1 : 0;
}
