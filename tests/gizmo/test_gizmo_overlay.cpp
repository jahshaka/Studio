// Gizmo overlay through the engine: the translation gizmo draws on top, no GL, no window.
#include "irisgl/core/math/vec.h"
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPointF>
#include <QString>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "viewport/translationgizmo.h"
#include "viewport/rotationgizmo.h"
#include "viewport/scalegizmo.h"
#include "viewport/gizmomeshes.h"
#include "viewport/gizmooverlay.h"
#include "irisgl/mirror/scenemirror.h"
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
    cam->setLocalPos(iris::Vec3(0, 0, 6)); cam->lookAt(iris::Vec3(0, 0, 0));
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 100.0f;
    cam->setAspectRatio(1.0f);
    doc->getRootNode()->addChild(cam);
    SceneMirror mirror(target); mirror.setSource(doc); mirror.sync(); mirror.applyCamera(cam, view);

    TranslationGizmo gizmo;                          // loads app/models/axis_*.obj (no GL needed)
    CHECK(gizmo.drawItems(iris::Vec3(), iris::Vec3(0,0,-1), iris::Vec3(0,0,-1)).isEmpty(), "nothing selected -> no items");
    GizmoOverlay overlay(target);
    overlay.update(&gizmo, cam->getGlobalPosition(), iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    Image img; view->readPixels(img);
    CHECK(countNonBg(img) == 0, "unselected gizmo draws nothing");

    gizmo.setSelectedNode(node);
    gizmo.updateSize(cam);
    auto items = gizmo.drawItems(cam->getGlobalPosition(), iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
    std::printf("    translation gizmo: %d draw items, scale %.2f\n", items.size(), gizmo.getGizmoScale());
    CHECK(items.size() == 4, "translation gizmo describes 4 handles");
    for (int i = 0; i < items.size(); ++i) {
        MeshData md; const bool ok = SceneMirror::toMeshData(items[i].mesh.data(), md);
        const iris::Vec3 p = items[i].transform.column(3).toVector3D(), sx = items[i].transform.column(0).toVector3D();
        std::printf("    item %d: mesh=%p ok=%d verts=%zu tris=%zu pos=(%.2f %.2f %.2f) colScaleX=%.2f colour=%d,%d,%d\n", i,
                    (void*)items[i].mesh.data(), ok, md.vertexCount(), md.triangleCount(), p.x(), p.y(), p.z(), sx.length(),
                    items[i].colour.red(), items[i].colour.green(), items[i].colour.blue());
    }
    overlay.update(&gizmo, cam->getGlobalPosition(), iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
    CHECK(overlay.visibleItems() == 4, "overlay shows 4 items");
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const int drawn = countNonBg(img);
    std::printf("    pixels drawn by the gizmo: %d\n", drawn);
    CHECK(drawn > 5, "the gizmo is visible in the frame (handles are thin at this distance)");
    CHECK(hasColour(img, 237/255.f, 66/255.f, 66/255.f), "X handle is red");
    CHECK(hasColour(img, 122/255.f, 204/255.f, 44/255.f), "Y handle is green");

    // ------------------------------------------------------------------
    // THE PLANE HANDLES, THROUGH THE ENGINE (GIZMO-1 item 3, 2026-09-15).
    //
    // They are picked in PIXELS, so they need a PICK VIEW to exist at all —
    // which is why every check above still counts four handles: a gizmo that
    // has never been shown in a viewport draws no plane squares, exactly as it
    // picks no rotation rings. Given one, the three squares join the picture,
    // and the two that are EDGE-ON to this head-on camera stay out of it.
    {
        // A BIGGER VIEW FOR THIS ONE CHECK (GIZMO-2 item 1): the plane handle is
        // an OUTLINE now — four tubes drawn one pixel wide, like the rotation
        // rings — so at 128x128, where the whole gizmo covers 28 pixels, the
        // frame lands under half a pixel and blends away. 512 is the same
        // picture with enough pixels in it to name a colour.
        View *big = engine->createOffscreenView("gizmo-planes", 512, 512, Colour(0.1f, 0.1f, 0.1f));
        big->setScene(target);
        mirror.applyCamera(cam, big);
        gizmo.setPickView(cam, 512.0f, 512.0f);
        gizmo.updateSize(cam);
        auto withPlanes = gizmo.drawItems(cam->getGlobalPosition(), iris::Vec3(0, 0, -1),
                                          iris::Vec3(0, 0, -1));
        std::printf("    with a pick view: %d draw items (4 handles + the XY square; the YZ and "
                    "XZ squares are edge-on to this camera)\n", withPlanes.size());
        CHECK(withPlanes.size() == 5, "the XY plane handle joins the four, and the two edge-on "
                                      "squares are withheld");
        overlay.update(&gizmo, cam->getGlobalPosition(), iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
        CHECK(overlay.visibleItems() == 5, "and the overlay shows all five");
        Image bigImg;
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        big->readPixels(bigImg);
        CHECK(hasColour(bigImg, 179/255.f, 135/255.f, 55/255.f),
              "the XY plane handle is on screen, in its two axes' mixed colour");
        // AN OUTLINE, NOT A FILLED QUAD (owner §368): the middle of the square
        // shows the scene behind it. The square spans [0, span] on x and y at
        // this camera (which looks down -Z), so its centre projects to the
        // pixel half a span out on each axis — inside the frame, and nothing of
        // the gizmo is drawn there.
        {
            const float scale = gizmo.getGizmoScale() * 0.05f;
            const iris::Vec3 mid(0.5f * GizmoMeshes::kPlaneHandleSpan * scale,
                                 0.5f * GizmoMeshes::kPlaneHandleSpan * scale, 0.0f);
            QPointF px;
            const bool projected = gizmo.projectToPixel(mid, px);
            const Colour c = projected ? bigImg.at(unsigned(px.x()), unsigned(px.y()))
                                       : Colour(1, 1, 1);
            std::printf("    the middle of the XY square reads %.0f %.0f %.0f (background is "
                        "26 26 26)\n", c.r * 255, c.g * 255, c.b * 255);
            CHECK(projected && isBg(c), "and it is an OUTLINE: the middle of the square is "
                                        "the background, not the handle's colour");
        }
        mirror.applyCamera(cam, view);
        engine->destroyView(big);
        // Back to the state the rest of the suite expects.
        gizmo.setPickView(iris::CameraNodePtr(), 0.0f, 0.0f);
        overlay.update(&gizmo, cam->getGlobalPosition(), iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    }

    // ------------------------------------------------------------------
    // THE FOV SWEEP (fix wave 2026-09-07). updateSize used to feed DEGREES to
    // qTan and then DIVIDE by the result: at fov 75 that is tan(37.5 rad) =
    // -0.199, so gizmoScale went negative — every handle transform mirrored
    // (read on the rig as a 180-degree flip) and every hit radius, which is
    // gizmoScale * handleScale, went negative too, so nothing could be picked.
    // The gate: positive and monotonically increasing across the whole range a
    // camera can be authored at, plus the calibration pin at 45.
    {
        const float savedAngle = cam->angle;
        gizmo.setSelectedNode(node);
        cam->angle = 45.0f; gizmo.updateSize(cam);
        const float at45 = gizmo.getGizmoScale();
        // distance is 6 (camera at z=6, node at the origin); 4.33*6*tan(22.5deg)
        // is the pre-fix 6/tan(22.5 rad) to five decimals — nothing moves at 45.
        std::printf("    gizmoScale at fov 45: %.5f (legacy 1/tan(22.5rad)*6 = %.5f)\n",
                    at45, 6.0f / std::tan(22.5f));
        // 4.33 is the rounded 4.3310 that solves it exactly, so the agreement
        // is to 0.02% (0.0025 world units at this distance), not to the bit.
        CHECK(std::abs(at45 - 6.0f / std::tan(22.5f)) < 0.01f,
              "fov 45 reproduces the legacy gizmo size (calibration)");

        float prev = -1.0f;
        bool positive = true, monotonic = true;
        for (float fov = 30.0f; fov <= 120.5f; fov += 5.0f) {
            cam->angle = fov;
            gizmo.updateSize(cam);
            const float s = gizmo.getGizmoScale();
            if (!(s > 0.0f) || !std::isfinite(s)) { positive = false; std::printf("    fov %.0f -> scale %.4f (NOT POSITIVE)\n", fov, s); }
            if (s <= prev) { monotonic = false; std::printf("    fov %.0f -> scale %.4f (NOT INCREASING, prev %.4f)\n", fov, s, prev); }
            prev = s;
        }
        CHECK(positive, "gizmoScale stays positive across fov 30..120");
        CHECK(monotonic, "gizmoScale grows with the angle of view (screen-constant sizing)");

        // The rig's exact repro: at fov 75 the scale — and therefore every hit
        // radius derived from it — used to be negative.
        cam->angle = 75.0f; gizmo.updateSize(cam);
        std::printf("    gizmoScale at fov 75: %.4f (was %.4f before the fix)\n",
                    gizmo.getGizmoScale(), 6.0f / std::tan(37.5f));
        CHECK(gizmo.getGizmoScale() > 0.0f, "fov 75 (the reported flip) is positive");

        // Sanity at the extremes: a 1-degree lens and a 179-degree one both
        // produce a finite, positive scale rather than a divide-by-zero.
        cam->angle = 1.0f;   gizmo.updateSize(cam); const float atMin = gizmo.getGizmoScale();
        cam->angle = 179.0f; gizmo.updateSize(cam); const float atMax = gizmo.getGizmoScale();
        CHECK(atMin > 0.0f && std::isfinite(atMin) && atMax > atMin && std::isfinite(atMax),
              "the clamped extremes stay finite and ordered");

        cam->angle = savedAngle;
        gizmo.updateSize(cam);
    }

    // Highlight: aim the ray at the X handle's colour spot -> it turns yellow.
    // (Hit-testing is the gizmo's own; here we only prove colour changes flow through.)
    gizmo.clearSelectedNode();
    overlay.update(&gizmo, cam->getGlobalPosition(), iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countNonBg(img) == 0, "cleared selection hides the gizmo");

    // Rotation and scale gizmos describe their handles too.
    RotationGizmo rot; rot.setSelectedNode(node); rot.updateSize(cam);
    CHECK(rot.drawItems(cam->getGlobalPosition(), iris::Vec3(0,0,-1), iris::Vec3(0,0,-1)).size() == 4, "rotation gizmo: 3 axis rings + screen-facing outer ring");
    ScaleGizmo scl; scl.setSelectedNode(node); scl.updateSize(cam);
    CHECK(scl.drawItems(cam->getGlobalPosition(), iris::Vec3(0,0,-1), iris::Vec3(0,0,-1)).size() == 4, "scale gizmo: 4 handles");
    {
        auto ri = rot.drawItems(cam->getGlobalPosition(), iris::Vec3(0,0,-1), iris::Vec3(0,0,-1));
        for (int i = 0; i < ri.size(); ++i) {
            MeshData md; const bool ok = SceneMirror::toMeshData(ri[i].mesh.data(), md);
            float mx = 0; for (size_t v = 0; v < md.vertexCount(); ++v) mx = std::max(mx, std::abs(md.positions[v*3]));
            std::printf("    ring %d: ok=%d verts=%zu tris=%zu scaleX=%.2f maxX=%.2f colour=%d,%d,%d\n", i, ok, md.vertexCount(), md.triangleCount(),
                        ri[i].transform.column(0).toVector3D().length(), mx, ri[i].colour.red(), ri[i].colour.green(), ri[i].colour.blue());
        }
    }
    overlay.update(&rot, cam->getGlobalPosition(), iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    std::printf("    rotation pixels drawn (reused overlay): %d\n", countNonBg(img));
    overlay.clear();
    GizmoOverlay overlay2(target);
    overlay2.update(&rot, cam->getGlobalPosition(), iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    std::printf("    rotation pixels drawn (fresh overlay):  %d\n", countNonBg(img));
    overlay2.clear();
    CHECK(countNonBg(img) > 5, "rotation gizmo is visible");
    CHECK(hasColour(img, 237/255.f, 66/255.f, 66/255.f) || hasColour(img, 122/255.f, 204/255.f, 44/255.f) || hasColour(img, 58/255.f, 122/255.f, 240/255.f), "rotation rings carry axis colours");

    // ------------------------------------------------------------------
    // THE DRAG MARKER (GIZMO-2 item 3; owner §366/§371, Unreal's shape).
    //
    // While a ring is being dragged the gizmo also draws a small disc at the
    // centre and a line with an arrowhead running out along THAT ring's axis to
    // the ring's radius, in the ring's own colour. Driven here through the very
    // calls a mouse press makes, drawn through the real overlay, read back as
    // pixels: the X ring is grabbed (the camera looks down -Z, so that ring is
    // edge-on and its axis points to the right of the frame), and the arrow is
    // the only red thing to the right of the centre while the ring itself is
    // highlighted yellow.
    // EVIDENCE ON DISK when asked for it (spikes/gizmo-2): the same buffers the
    // assertions read, as PNGs. Off unless the environment names a directory, so
    // the suite writes nothing during a gate.
    const auto saveShot = [](const Image &src, const char *name) {
        const QByteArray dir = qgetenv("JAH_GIZMO2_SHOT_DIR");
        if (dir.isEmpty()) return;
        QImage out(int(src.width), int(src.height), QImage::Format_RGB888);
        for (unsigned y = 0; y < src.height; ++y)
            for (unsigned x = 0; x < src.width; ++x) {
                const Colour c = src.at(x, y);
                out.setPixel(int(x), int(y), qRgb(int(std::min(1.0f, c.r) * 255.0f),
                                                  int(std::min(1.0f, c.g) * 255.0f),
                                                  int(std::min(1.0f, c.b) * 255.0f)));
            }
        const QString path = QString::fromUtf8(dir) + "/" + QString::fromUtf8(name);
        std::printf("    wrote %s: %d\n", qPrintable(path), int(out.save(path)));
    };

    {
        View *big = engine->createOffscreenView("gizmo-drag", 512, 512, Colour(0.1f, 0.1f, 0.1f));
        big->setScene(target);
        mirror.applyCamera(cam, big);
        GizmoOverlay dragOverlay(target);

        RotationGizmo drag;
        drag.setSelectedNode(node);
        drag.setPickView(cam, 512.0f, 512.0f);
        drag.updateSize(cam);
        const float ringR = drag.getGizmoScale() * GizmoMeshes::kRotationHandleScale;
        // A point ON the X ring (its circle lies in the YZ plane) and away from
        // where the Z ring crosses it.
        const iris::Vec3 onXRing(0.0f, 0.55f * ringR, 0.83f * ringR);
        QPointF grab;
        const bool projected = drag.projectToPixel(onXRing, grab);
        float px = -1.0f;
        const QString ring = projected ? drag.ringNameAtPixel(grab, px) : QString();
        std::printf("    the pixel on the X ring picks '%s' at %.2f px\n", qPrintable(ring),
                    double(px));
        CHECK(ring == QLatin1String("x"), "the drag starts on the X ring");

        const iris::Vec3 eye = cam->getGlobalPosition();
        const iris::Vec3 rayDir = (onXRing - eye).normalized();
        const iris::Vec3 viewDir = cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1));
        drag.startDragging(eye, rayDir, viewDir);
        CHECK(drag.isDragging(), "and the gizmo is dragging");
        auto dragItems = drag.drawItems(eye, rayDir, viewDir);
        std::printf("    dragging: %d draw items (the ring, the hub disc, the axis arrow)\n",
                    dragItems.size());
        CHECK(dragItems.size() == 3, "a ring under drag draws the ring plus the two marker parts");
        if (dragItems.size() == 3) {
            CHECK(dragItems[0].colour == QColor(255, 255, 0), "the dragged ring stays highlighted");
            CHECK(dragItems[1].colour == QColor(237, 66, 66) &&
                  dragItems[2].colour == QColor(237, 66, 66),
                  "and the hub and arrow carry the dragged ring's OWN colour (X = red)");
        }
        dragOverlay.update(&drag, eye, rayDir, viewDir);
        Image dragImg;
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        big->readPixels(dragImg);
        int redRight = 0, farthest = 0;
        for (unsigned y = 0; y < dragImg.height; ++y)
            for (unsigned x = 270; x < dragImg.width; ++x) {
                const Colour c = dragImg.at(x, y);
                if (std::abs(c.r - 237/255.f) < 0.2f && std::abs(c.g - 66/255.f) < 0.2f &&
                    std::abs(c.b - 66/255.f) < 0.2f) { ++redRight; farthest = int(x); }
            }
        std::printf("    the arrow: %d red pixels right of the centre, reaching x = %d "
                    "(the ring's own radius projects to about x = %d)\n", redRight, farthest,
                    int(256 + 512 * 0.5 * ringR / (6.0f * std::tan(float(M_PI) * 22.5f / 180.f))));
        CHECK(redRight > 20, "the axis arrow is on screen, pointing out along the dragged "
                             "ring's axis");

        saveShot(dragImg, "rotate-dragging.png");

        // THE SAME MARKER FROM A 3/4 VIEW, on the Y ring — and the reason the
        // hub lies in the ring's PLANE rather than facing the camera: the two
        // parts are complementary. A ring seen edge-on shows a line and its
        // arrow across the frame (the capture above); a ring seen face-on shows
        // a disc and an arrow pointing at the eye. Whichever way the camera is
        // turned, one of the two is legible.
        {
            cam->setLocalPos(iris::Vec3(5, 4, 5));
            cam->lookAt(iris::Vec3(0, 0, 0));
            cam->update(0.0f);
            mirror.applyCamera(cam, big);
            RotationGizmo iso;
            iso.setSelectedNode(node);
            iso.setPickView(cam, 512.0f, 512.0f);
            iso.updateSize(cam);
            const float r = iso.getGizmoScale() * GizmoMeshes::kRotationHandleScale;
            // On the Y ring (its circle lies in XZ) and away from the two axes,
            // where the X and Z rings cross it.
            const iris::Vec3 onYRing(0.707f * r, 0.0f, 0.707f * r);
            const iris::Vec3 isoEye = cam->getGlobalPosition();
            const iris::Vec3 isoDir = (onYRing - isoEye).normalized();
            const iris::Vec3 isoView =
                cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1));
            QPointF isoPx; float isoD = -1.0f;
            const QString isoRing = iso.projectToPixel(onYRing, isoPx)
                                        ? iso.ringNameAtPixel(isoPx, isoD) : QString();
            iso.startDragging(isoEye, isoDir, isoView);
            auto isoItems = iso.drawItems(isoEye, isoDir, isoView);
            std::printf("    3/4 view: the pixel on the Y ring picks '%s'; dragging draws %d "
                        "items\n", qPrintable(isoRing), isoItems.size());
            CHECK(isoRing == QLatin1String("y") && isoItems.size() == 3,
                  "the marker draws for a ring grabbed from a 3/4 view too");
            dragOverlay.update(&iso, isoEye, isoDir, isoView);
            Image isoImg;
            for (int i = 0; i < 2; ++i) engine->renderOneFrame();
            big->readPixels(isoImg);
            saveShot(isoImg, "rotate-dragging-iso.png");
            iso.endDragging();
        }

        drag.endDragging();
        auto released = drag.drawItems(eye, rayDir, viewDir);
        std::printf("    released: %d draw items\n", released.size());
        CHECK(!drag.isDragging() && released.size() == 4,
              "and at release the marker is gone: the four rings and nothing else");
        dragOverlay.clear();
        mirror.applyCamera(cam, view);
        engine->destroyView(big);
    }

    mirror.setSource(nullptr);
    engine->destroyView(view); engine->destroyScene(target); engine.reset();
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
