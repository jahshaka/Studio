// ScenePicker characterisation: picking on the document, no renderer, no GL.
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include "irisgl/irisglfwd.h"
#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/document/assets/mesh.h"
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

    // mesh hits report their triangle (V-hold vertex snap reads its corners)
    {
        ScenePicker::screenSegment(cam, W, H, QPointF(W / 2, H / 2), a, b);
        const ScenePick pick = ScenePicker::nearest(ScenePicker::pickAll(doc, a, b, cam->getGlobalPosition()));
        CHECK(pick.triangleIndex >= 0, "mesh hit carries a triangle index");
        auto tris = front->getMesh()->getTriMesh()->triangles;
        CHECK(pick.triangleIndex < tris.size(), "triangle index is in range");
        const auto &tri = tris[pick.triangleIndex];
        const QMatrix4x4 xf = front->getGlobalTransform();
        float nearest = 1e9f;
        for (const QVector3D &c : { xf * tri.a, xf * tri.b, xf * tri.c })
            nearest = std::min(nearest, (c - pick.hitPoint).length());
        std::printf("    nearest triangle corner is %.3f from the hit point\n", nearest);
        CHECK(nearest < 2.0f, "the indexed triangle's corners surround the hit point");
    }

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
    CHECK(ScenePicker::nearest(hits).triangleIndex == -1, "sphere hits carry no triangle index");
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

    // ---- orthographic picking (Views dropdown, 2026-08-31): rays must be
    // PARALLEL (view forward), origin offset across the ortho extent — not
    // fanned out from the eye like perspective. The engine renders the same
    // extent (OgreView setOrthoWindow spans 2*orthoSize, the document's
    // ortho(-s..+s) convention), so clicks land where the pixels are.
    {
        auto odoc = iris::Scene::create();
        auto ocam = iris::CameraNode::create();
        ocam->setLocalPos(QVector3D(0, 10, 0));
        ocam->setLocalRot(QQuaternion::fromEulerAngles(-90, 0, 0)); // straight down
        ocam->nearClip = 0.1f; ocam->farClip = 100.0f;
        ocam->setProjection(iris::CameraProjection::Orthogonal);
        ocam->setOrthagonalZoom(5.0f);             // half-extent: world x in [-5, 5]
        odoc->getRootNode()->addChild(ocam);
        auto east = cubeAt(odoc, QVector3D(4, 0, 0), "east");
        auto west = cubeAt(odoc, QVector3D(-4, 0, 0), "west");

        QVector3D a1, b1, a2, b2;
        // world x=+4 with half-extent 5 -> screen x fraction 0.5 + 4/10 = 0.9
        ScenePicker::screenSegment(ocam, W, H, QPointF(W * 0.9, H * 0.5), a1, b1);
        auto oh = ScenePicker::pickAll(odoc, a1, b1, ocam->getGlobalPosition());
        CHECK(!oh.isEmpty() && ScenePicker::nearest(oh).node == east,
              "ortho: right-side click picks the east cube");
        ScenePicker::screenSegment(ocam, W, H, QPointF(W * 0.1, H * 0.5), a2, b2);
        oh = ScenePicker::pickAll(odoc, a2, b2, ocam->getGlobalPosition());
        CHECK(!oh.isEmpty() && ScenePicker::nearest(oh).node == west,
              "ortho: left-side click picks the west cube");
        const QVector3D d1 = (b1 - a1).normalized(), d2 = (b2 - a2).normalized();
        CHECK(QVector3D::dotProduct(d1, d2) > 0.9999f, "ortho rays are parallel (not eye-fanned)");
        CHECK(std::abs(a1.x() - 4.0f) < 0.05f, "ortho ray origin offsets across the extent (x=+4)");
        ScenePicker::screenSegment(ocam, W, H, QPointF(W * 0.5, H * 0.5), a1, b1);
        CHECK(ScenePicker::pickAll(odoc, a1, b1, ocam->getGlobalPosition()).isEmpty(),
              "ortho: centre click (no cube at origin) misses");
    }

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
