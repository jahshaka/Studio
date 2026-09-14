// document.movement_epoch — THE VIEWER IS NOT THE VIEWED.
//
// `iris::graph::transformWrites()` is the whole renderer's answer to "did
// anything move?" (nodegraph.h). Four O(scene) walks hang off it — the GI
// movement scan, the shadow-caster scan, and the two GI signatures the mirror
// reads every frame — plus the mirror's own still-frame gates (the floor's
// world, the skeleton share's world comparisons, the static settle).
//
// A CAMERA used to bump it. Nothing those consumers scan can change because
// the camera moved, and the bill was measured on the render review's
// 8,404-node lattice (RR2, 2026-09-14): the mirror's GI push cost 0.024 ms per
// frame while the camera was still and 10.28 ms on 92% of FLYING frames — the
// same scene, the same path, GI the only difference (0.014 ms with it off).
// Flying re-ran every scan in the program, every frame.
//
// So this suite asserts the rule and its ONE exception, at the document layer
// where the rule lives:
//
//   1. moving a MESH counts (the counter exists for exactly this),
//   2. moving a CAMERA does not — through every setter a camera can be flown
//      with, and through the animation path,
//   3. a camera WITH CHILDREN counts again, because moving it moves them,
//   4. ...and stops counting when the child goes away.
//
// Document only: NULL render system, no display.
#include <cstdio>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

unsigned long long writes() { return iris::graph::transformWrites(); }

/// Fly a node the way the free camera and every script verb do.
void fly(const iris::SceneNodePtr &n, float t)
{
    n->setLocalPos(iris::Vec3(t, 2.0f, -t));
    n->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(0.0f, t * 10.0f, 0.0f)));
    n->setLocalScale(iris::Vec3(1, 1, 1));
    n->rotate(iris::Quat::fromEulerAngles(iris::Vec3(0.0f, 1.0f, 0.0f)));
}

}   // namespace

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    enginetest::DocumentGraph graph;
    if (!graph.ok()) { std::printf("FAIL: headless engine: %s\n", graph.error().c_str()); return 1; }

    auto scene = iris::Scene::create();
    auto cube = iris::MeshNode::create();
    scene->getRootNode()->addChild(cube);
    auto cam = iris::CameraNode::create();
    scene->getRootNode()->addChild(cam);

    // 1. THE RULE. A mesh moving is scene movement, every time.
    {
        const unsigned long long before = writes();
        fly(cube, 1.0f);
        const unsigned long long after = writes();
        CHECK(after > before, "moving a MESH counts as scene movement");
        std::printf("      (a mesh's three writes moved the epoch by %llu)\n", after - before);
    }

    // 2. THE VIEWER. 200 camera writes — a second and a half of flying at
    //    60 Hz — must not move the epoch by one.
    {
        const unsigned long long before = writes();
        for (int i = 0; i < 200; ++i) fly(cam, float(i) * 0.1f);
        // ...and the paths a camera also travels on: a global write (what the
        // pilot/preview paths use) and the animation tick.
        cam->setGlobalTransform(iris::Mat4());
        cam->update(0.016f);
        CHECK(writes() == before, "200 camera writes move the epoch by ZERO");
    }

    // 3. THE ENGINE'S OWN CHILDREN ARE NOT CONTENT (round-2 review, item 2).
    //    The two trees are one, and the mirror hangs a WIRE NODE off every
    //    non-view camera with a visible body (and a -Y adapter off every light,
    //    and a projector box off every decal). Those carry no document node,
    //    nothing scans them, and they move only because we moved them — so a
    //    camera that has them is still the viewer. Counting Ogre's children
    //    instead of the document's un-exempted the play-mode subject and every
    //    cinematic camera, which is the whole of the cost this removes.
    {
        const iris::graph::NodeHandle wire =
            iris::graph::createNode(iris::graph::sceneOf(cam->graphNode()), cam->graphNode(),
                                    nullptr);
        CHECK(wire != nullptr, "an ENGINE child (a camera body wire) hangs off the camera");
        CHECK(iris::graph::childCount(cam->graphNode()) == 1u, "...and Ogre sees it as a child");
        const unsigned long long before = writes();
        for (int i = 0; i < 50; ++i) fly(cam, float(i));
        CHECK(writes() == before,
              "a camera with the ENGINE's own child is still not scene movement");
        iris::graph::destroyNode(wire);
    }

    // 4. THE EXCEPTION. A camera that carries something of the DOCUMENT's moves
    //    that something.
    {
        auto carried = iris::MeshNode::create();
        cam->addChild(carried);
        const unsigned long long before = writes();
        fly(cam, 7.0f);
        CHECK(writes() > before, "a camera WITH A DOCUMENT CHILD counts again (it moves the child)");

        // 5. ...and the moment the child goes, the camera is free again.
        carried->removeFromParent();
        const unsigned long long after = writes();
        for (int i = 0; i < 50; ++i) fly(cam, float(i));
        CHECK(writes() == after, "...and stops counting when the child is gone");
    }

    std::printf(failures ? "FAILED (%d)\n" : "PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
