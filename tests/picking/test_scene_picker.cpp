// ScenePicker characterisation: picking on the document, no renderer, no GL.
#include <QGuiApplication>
#include <QVector3D>
#include <cstdio>
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "viewport/scenepicker.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static iris::MeshNodePtr cubeAt(iris::ScenePtr doc, const QVector3D &pos, const char *name, iris::SceneNodePtr parent = nullptr) {
    auto n = iris::MeshNode::create();
    n->setName(name);
    n->setMesh(":assets/models/cube.obj");
    const float r = n->getMeshRadius(); const float s = r > 0 ? 1.0f / r : 1.0f;
    n->setLocalScale(QVector3D(s, s, s));
    n->setLocalPos(pos);
    (parent ? parent : doc->getRootNode())->addChild(n);
    return n;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    auto doc = iris::Scene::create();
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(QVector3D(0, 0, 6)); cam->lookAt(QVector3D(0, 0, 0));
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 100.0f;
    doc->getRootNode()->addChild(cam);
    auto front = cubeAt(doc, QVector3D(0, 0, 0), "front");
    const int W = 200, H = 200;
    QVector3D a, b;

    // centre pixel -> hits the cube; corner -> nothing
    ScenePicker::screenSegment(cam, W, H, QPointF(W / 2, H / 2), a, b);
    auto hits = ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition());
    CHECK(!hits.isEmpty(), "centre ray hits the cube");
    CHECK(ScenePicker::nearest(hits).node == front, "nearest hit is the cube");
    std::printf("    hit point %.2f %.2f %.2f (front face z should be ~+1)\n",
                ScenePicker::nearest(hits).hitPoint.x(), ScenePicker::nearest(hits).hitPoint.y(), ScenePicker::nearest(hits).hitPoint.z());
    CHECK(ScenePicker::nearest(hits).hitPoint.z() > 0.5f, "nearest hit is on the front face");
    ScenePicker::screenSegment(cam, W, H, QPointF(3, 3), a, b);
    CHECK(ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition()).isEmpty(), "corner ray misses");

    // a second cube behind the first: the nearer one wins
    auto back = cubeAt(doc, QVector3D(0, 0, -4), "back");
    ScenePicker::screenSegment(cam, W, H, QPointF(W / 2, H / 2), a, b);
    hits = ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition());
    CHECK(hits.size() >= 2, "both cubes on the ray");
    CHECK(ScenePicker::nearest(hits).node == front, "nearer cube wins");
    front->setLocalPos(QVector3D(10, 0, 0));       // move the front one away
    hits = ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition());
    CHECK(ScenePicker::nearest(hits).node == back, "after moving it, the back cube is picked");
    front->setLocalPos(QVector3D(0, 0, 0));

    // unpickable nodes are skipped unless forced
    front->setPickable(false);
    hits = ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition());
    CHECK(ScenePicker::nearest(hits).node == back, "unpickable front cube is skipped");
    hits = ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition(), true);
    CHECK(ScenePicker::nearest(hits).node == front, "forcePickable picks it anyway");
    front->setPickable(true);

    // lights pick as spheres
    auto light = iris::LightNode::create();
    light->setLocalPos(QVector3D(-2.5f, 0, 0));
    doc->getRootNode()->addChild(light);
    ScenePicker::screenSegment(cam, W, H, QPointF(W / 2, H / 2), a, b);
    hits = ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition());
    bool lightHit = false; for (auto &h : hits) if (h.node == light) lightHit = true;
    CHECK(!lightHit, "light off the ray is not hit");
    light->setLocalPos(QVector3D(0, 0, 2.5f));      // between camera and cube
    hits = ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition());
    CHECK(ScenePicker::nearest(hits).node == light, "light sphere on the ray is nearest");
    hits = ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition(), false, false);
    CHECK(ScenePicker::nearest(hits).node == front, "lights can be excluded");
    doc->getRootNode()->removeChild(light);

    // root-vs-child rule
    auto holder = iris::SceneNode::create();
    doc->getRootNode()->addChild(holder);
    doc->getRootNode()->removeChild(front);
    holder->addChild(front, false);
    front->setAttached(true);                      // "part of" its parent
    auto sibling = cubeAt(doc, QVector3D(2, 0, 0), "sibling", holder);
    sibling->setAttached(true);                    // a second part of the same asset
    auto r1 = ScenePicker::resolveRootSelection(front, nullptr, true);
    CHECK(r1 == holder, "first click on an attached child selects its root");
    auto r2 = ScenePicker::resolveRootSelection(front, holder, true);
    CHECK(r2 == front, "click on the already-selected root drills down to the part");
    auto r3 = ScenePicker::resolveRootSelection(front, back, true);
    CHECK(r3 == holder, "click from a different selection goes back to the root");
    auto r4 = ScenePicker::resolveRootSelection(front, sibling, true);
    CHECK(r4 == holder, "click while a SIBLING part is selected selects the root, not the part");
    auto r5 = ScenePicker::resolveRootSelection(front, front, true);
    CHECK(r5 == front, "re-click on the selected part keeps it");
    CHECK(ScenePicker::resolveRootSelection(front, nullptr, false) == front, "rule disabled: the child");

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
