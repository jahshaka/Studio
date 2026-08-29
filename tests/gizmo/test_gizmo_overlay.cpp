// Gizmo overlay through the engine: the translation gizmo draws on top, no GL, no window.
#include <QGuiApplication>
#include <QVector3D>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "irisglfwd.h"
#include "scenegraph/scene.h"
#include "scenegraph/scenenode.h"
#include "scenegraph/meshnode.h"
#include "scenegraph/cameranode.h"
#include "materials/defaultmaterial.h"
#include "editor/translationgizmo.h"
#include "editor/rotationgizmo.h"
#include "editor/scalegizmo.h"
#include "editor/gizmooverlay.h"
#include "engine/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)
static bool isBg(const Colour &c) { return c.r < 0.15f && c.g < 0.15f && c.b < 0.15f; }
static int countNonBg(const Image &img) { int n = 0; for (unsigned y = 0; y < img.height; ++y) for (unsigned x = 0; x < img.width; ++x) if (!isBg(img.at(x, y))) ++n; return n; }
static bool hasColour(const Image &img, float r, float g, float b) {
    for (unsigned y = 0; y < img.height; ++y) for (unsigned x = 0; x < img.width; ++x) {
        const Colour c = img.at(x, y);
        if (std::abs(c.r - r) < 0.2f && std::abs(c.g - g) < 0.2f && std::abs(c.b - b) < 0.2f) return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    EngineConfig cfg; cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR; cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR; cfg.logFile = "test_gizmo-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine"); if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("gizmo", 128, 128, Colour(0.1f, 0.1f, 0.1f));
    Scene *target = engine->createScene("gizmo");
    view->setScene(target);
    target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));

    auto doc = iris::Scene::create();
    auto node = iris::SceneNode::create();          // an EMPTY node: nothing but the gizmo can draw
    doc->getRootNode()->addChild(node);
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(QVector3D(0, 0, 6)); cam->lookAt(QVector3D(0, 0, 0));
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 100.0f;
    cam->setAspectRatio(1.0f);
    doc->getRootNode()->addChild(cam);
    SceneMirror mirror(target); mirror.setSource(doc); mirror.sync(); mirror.applyCamera(cam, view);

    TranslationGizmo gizmo;                          // loads app/models/axis_*.obj (no GL needed)
    CHECK(gizmo.drawItems(QVector3D(), QVector3D(0,0,-1), QVector3D(0,0,-1)).isEmpty(), "nothing selected -> no items");
    GizmoOverlay overlay(target);
    overlay.update(&gizmo, cam->getGlobalPosition(), QVector3D(0, 0, -1), QVector3D(0, 0, -1));
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    Image img; view->readPixels(img);
    CHECK(countNonBg(img) == 0, "unselected gizmo draws nothing");

    gizmo.setSelectedNode(node);
    gizmo.updateSize(cam);
    auto items = gizmo.drawItems(cam->getGlobalPosition(), QVector3D(0, 0, -1), QVector3D(0, 0, -1));
    std::printf("    translation gizmo: %d draw items, scale %.2f\n", items.size(), gizmo.getGizmoScale());
    CHECK(items.size() == 4, "translation gizmo describes 4 handles");
    for (int i = 0; i < items.size(); ++i) {
        MeshData md; const bool ok = SceneMirror::toMeshData(items[i].mesh.data(), md);
        const QVector3D p = items[i].transform.column(3).toVector3D(), sx = items[i].transform.column(0).toVector3D();
        std::printf("    item %d: mesh=%p ok=%d verts=%zu tris=%zu pos=(%.2f %.2f %.2f) colScaleX=%.2f colour=%d,%d,%d\n", i,
                    (void*)items[i].mesh.data(), ok, md.vertexCount(), md.triangleCount(), p.x(), p.y(), p.z(), sx.length(),
                    items[i].colour.red(), items[i].colour.green(), items[i].colour.blue());
    }
    overlay.update(&gizmo, cam->getGlobalPosition(), QVector3D(0, 0, -1), QVector3D(0, 0, -1));
    CHECK(overlay.visibleItems() == 4, "overlay shows 4 items");
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const int drawn = countNonBg(img);
    std::printf("    pixels drawn by the gizmo: %d\n", drawn);
    CHECK(drawn > 5, "the gizmo is visible in the frame (handles are thin at this distance)");
    CHECK(hasColour(img, 237/255.f, 66/255.f, 66/255.f), "X handle is red");
    CHECK(hasColour(img, 122/255.f, 204/255.f, 44/255.f), "Y handle is green");

    // Highlight: aim the ray at the X handle's colour spot -> it turns yellow.
    // (Hit-testing is the gizmo's own; here we only prove colour changes flow through.)
    gizmo.clearSelectedNode();
    overlay.update(&gizmo, cam->getGlobalPosition(), QVector3D(0, 0, -1), QVector3D(0, 0, -1));
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countNonBg(img) == 0, "cleared selection hides the gizmo");

    // Rotation and scale gizmos describe their handles too.
    RotationGizmo rot; rot.setSelectedNode(node); rot.updateSize(cam);
    CHECK(rot.drawItems(cam->getGlobalPosition(), QVector3D(0,0,-1), QVector3D(0,0,-1)).size() == 4, "rotation gizmo: 3 axis rings + screen-facing outer ring");
    ScaleGizmo scl; scl.setSelectedNode(node); scl.updateSize(cam);
    CHECK(scl.drawItems(cam->getGlobalPosition(), QVector3D(0,0,-1), QVector3D(0,0,-1)).size() == 4, "scale gizmo: 4 handles");
    {
        auto ri = rot.drawItems(cam->getGlobalPosition(), QVector3D(0,0,-1), QVector3D(0,0,-1));
        for (int i = 0; i < ri.size(); ++i) {
            MeshData md; const bool ok = SceneMirror::toMeshData(ri[i].mesh.data(), md);
            float mx = 0; for (size_t v = 0; v < md.vertexCount(); ++v) mx = std::max(mx, std::abs(md.positions[v*3]));
            std::printf("    ring %d: ok=%d verts=%zu tris=%zu scaleX=%.2f maxX=%.2f colour=%d,%d,%d\n", i, ok, md.vertexCount(), md.triangleCount(),
                        ri[i].transform.column(0).toVector3D().length(), mx, ri[i].colour.red(), ri[i].colour.green(), ri[i].colour.blue());
        }
    }
    overlay.update(&rot, cam->getGlobalPosition(), QVector3D(0, 0, -1), QVector3D(0, 0, -1));
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    std::printf("    rotation pixels drawn (reused overlay): %d\n", countNonBg(img));
    overlay.clear();
    GizmoOverlay overlay2(target);
    overlay2.update(&rot, cam->getGlobalPosition(), QVector3D(0, 0, -1), QVector3D(0, 0, -1));
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    std::printf("    rotation pixels drawn (fresh overlay):  %d\n", countNonBg(img));
    overlay2.clear();
    CHECK(countNonBg(img) > 5, "rotation gizmo is visible");
    CHECK(hasColour(img, 237/255.f, 66/255.f, 66/255.f) || hasColour(img, 122/255.f, 204/255.f, 44/255.f) || hasColour(img, 58/255.f, 122/255.f, 240/255.f), "rotation rings carry axis colours");

    mirror.setSource(nullptr);
    engine->destroyView(view); engine->destroyScene(target); engine.reset();
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
