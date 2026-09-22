// THE EDITOR'S GROUND GRID IS A HELPER DRAWN ON TOP OF THE FLOOR
// (GIZMO-2 item 5; owner, ledger §370: "the grid should be a gizmo-class asset:
// today when you turn the grid on it MERGES with the floor; it should always be
// on top of the floor, but objects on top of it should cover it").
//
// WHAT IT USED TO DO. SceneMirror put the floor grid 1 cm BELOW its own plane
// (mGridFloorOffset = -0.01) so a ground plane would occlude it: invisible from
// above — the axis views had to flip the sign by hand — and fighting with the
// ground wherever it poked through, which is the "merges" in the report.
//
// WHAT IT DID NEXT (GIZMO-2). The line grid was drawn 1 cm ABOVE its plane, with
// the depth test on, so geometry standing on the floor still covered it.
//
// GRID-2 (2026-09-20): the grid is no longer a line mesh with a lift — it is a
// SHADER QUAD (Types.h GridDesc, OgreGrid.cpp, JahGrid_ps.glsl) that draws the
// lines analytically from the camera ray and writes the plane's own depth with
// a relative bias toward the camera, which Vulkan honours because the quad is a
// polygon (the line grid could take no bias at all — measured then: the same
// frame byte for byte at every mDepthBiasConstant). The claims below are the
// same claims, re-measured on the quad; D changed from "the lift's knee" to "no
// lift is needed".
//
// THE FIVE CLAIMS, as pixels:
//   A. the grid's lines are drawn over a floor at its own height, WITHOUT
//      z-fighting — measured against the same scene with the floor dropped out
//      of the way, so the bar is the grid's own pixel count, not a guess;
//   B. a box standing on the floor COVERS the grid inside its silhouette;
//   C. the grid is a HELPER: a view with the editor's furniture switched off
//      (View::setHelpersVisible(false) — what the Player page is, and what the
//      user's Scene-grade screenshot does) draws none of it;
//   D. the lift is at the KNEE of its curve: 1 mm is not enough and 2 cm is no
//      better (printed as a sweep, so the next tuning starts from data);
//   E. the ORTHO top view — the case the old offset's sign hid completely.
//
// Needs a display (Vulkan); no window (offscreen view + offscreen QPA).

#include "irisgl/core/math/quat.h"

#include "tests/support/testmesh.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <QByteArray>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/geometry/aabb.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

constexpr unsigned kSize = 512;

/// The grid is coloured PURE RED for this suite (setGridColours) so a grid pixel
/// is unmistakable against a grey floor and a green box.
bool isGrid(const Colour &c) { return c.r > 0.45f && c.g < 0.30f && c.b < 0.30f; }
bool isBox(const Colour &c)  { return c.g > 0.25f && c.g > c.r * 1.6f && c.g > c.b * 1.6f; }

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_grid_floor-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("grid", kSize, kSize, Colour(0, 0, 0.35f));
    Scene *target = engine->createScene("grid");
    if (!view || !target) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(target);
    target->setAmbient(Colour(0.35f, 0.35f, 0.35f), Colour(0.25f, 0.25f, 0.25f));

    // ---- the document: a floor at y=0 and a box standing on it ----
    auto doc = iris::Scene::create();

    // THE FLOOR: a shipped cube.obj, flattened, with its TOP FACE at exactly
    // y = 0 — the plane the grid is drawn in.
    //
    // A hand-built iris::Mesh is NOT enough here and the first cut of this
    // suite got it wrong: SceneMirror never mirrored it (the mesh reaches the
    // engine through the node's mesh SOURCE), so the "floor" rendered nothing
    // at all and every depth claim below was being made against empty
    // background. A mesh the document can name is the only floor that exists.
    auto floorNode = iris::MeshNode::create();
    floorNode->setName("floor");
    floorNode->setMesh(testmesh::load(":assets/models/cube.obj"));
    auto grey = iris::DefaultMaterial::create();
    grey->setDiffuseColor(QColor(90, 90, 90));
    floorNode->setMaterial(grey);
    // THE MESH'S OWN HALF-SIZE, read rather than assumed: getMeshRadius() is 1
    // for cube.obj and the first cut of this suite took that to mean a
    // half-extent of 1/sqrt(3), which put the "floor" 7 cm above the grid and
    // hid it in every case below.
    const iris::Vec3 cubeHalf = floorNode->getMesh()->getAABB().getHalfSize();
    std::printf("    cube.obj half-size: (%.3f, %.3f, %.3f), radius %.3f\n",
                double(cubeHalf.x()), double(cubeHalf.y()), double(cubeHalf.z()),
                double(floorNode->getMeshRadius()));
    const float kFloorHalfThickness = 0.10f;
    floorNode->setLocalScale(iris::Vec3(20.0f / cubeHalf.x(),
                                        kFloorHalfThickness / cubeHalf.y(),
                                        20.0f / cubeHalf.z()));
    floorNode->setLocalPos(iris::Vec3(0.0f, -kFloorHalfThickness, 0.0f));
    doc->getRootNode()->addChild(floorNode);

    auto box = iris::MeshNode::create();
    box->setName("box");
    box->setMesh(testmesh::load(":assets/models/cube.obj"));
    auto green = iris::DefaultMaterial::create();
    green->setDiffuseColor(QColor(0, 220, 0));
    box->setMaterial(green);
    // A 1 m cube standing ON the floor (its top face at y = 1).
    box->setLocalScale(iris::Vec3(0.5f / cubeHalf.x(), 0.5f / cubeHalf.y(), 0.5f / cubeHalf.z()));
    box->setLocalPos(iris::Vec3(0.0f, 0.5f, 0.0f));
    doc->getRootNode()->addChild(box);

    auto sun = iris::LightNode::create();
    sun->intensity = 1.2f;
    sun->setLocalRot(iris::Quat::fromEulerAngles(-55.0f, 25.0f, 0.0f));
    sun->setLocalPos(iris::Vec3(0.0f, 9.0f, 0.0f));
    doc->getRootNode()->addChild(sun);

    auto cam = iris::CameraNode::create();
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 200.0f;
    cam->setAspectRatio(1.0f);
    cam->setLocalPos(iris::Vec3(0.0f, 4.5f, 7.0f));
    cam->lookAt(iris::Vec3(0, 0.4f, 0));
    doc->getRootNode()->addChild(cam);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    mirror.sync();
    mirror.applyCamera(cam, view);

    auto render = [&](Image &img) {
        mirror.sync();
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
    };
    auto count = [&](const Image &img, bool (*pred)(const Colour &)) {
        int n = 0;
        for (unsigned y = 0; y < img.height; ++y)
            for (unsigned x = 0; x < img.width; ++x)
                if (pred(img.at(x, y))) ++n;
        return n;
    };

    // Frames on disk when asked for (spikes/gizmo-2): nothing in a gate.
    const auto dump = [](const Image &src, const char *name) {
        const QByteArray dir = qgetenv("JAH_GRID_SHOT_DIR");
        if (dir.isEmpty()) return;
        QImage out(int(src.width), int(src.height), QImage::Format_RGB888);
        for (unsigned y = 0; y < src.height; ++y)
            for (unsigned x = 0; x < src.width; ++x) {
                const Colour c = src.at(x, y);
                out.setPixel(int(x), int(y), qRgb(int(std::min(1.0f, c.r) * 255.0f),
                                                  int(std::min(1.0f, c.g) * 255.0f),
                                                  int(std::min(1.0f, c.b) * 255.0f)));
            }
        out.save(QString::fromUtf8(dir) + "/" + QString::fromUtf8(name));
    };

    Image img;
    render(img);
    {   // is the FLOOR actually there? (a neutral grey that is neither the blue
        // clear colour nor the green box)
        int grey = 0;
        for (unsigned y = 0; y < img.height; ++y)
            for (unsigned x = 0; x < img.width; ++x) {
                const Colour c = img.at(x, y);
                if (std::fabs(c.r - c.g) < 0.05f && std::fabs(c.g - c.b) < 0.05f && c.r > 0.05f)
                    ++grey;
            }
        const Colour bottom = img.at(img.width / 2, img.height - 8);
        std::printf("    the floor: %d neutral pixels; the bottom of the frame reads "
                    "%.2f %.2f %.2f\n", grey, bottom.r, bottom.g, bottom.b);
    }
    const int gridOff = count(img, isGrid);
    const int boxPixels = count(img, isBox);
    std::printf("    grid off: %d grid-coloured pixels, %d box pixels\n", gridOff, boxPixels);
    CHECK(gridOff == 0, "with the grid off there is nothing grid-coloured in the frame");
    CHECK(boxPixels > 500, "the box is in frame (the thing that has to cover the grid)");

    // ---- A: the grid draws over the floor ----
    mirror.setGridColours(Colour(1.0f, 0.0f, 0.0f, 0.99f), Colour(1.0f, 0.0f, 0.0f, 0.99f));
    mirror.setGridExtent(100.0f);                 // the editor's own extent
    mirror.setGrid(true, 1.0f, SceneMirror::GridPlane::Floor);
    render(img);
    const int gridOn = count(img, isGrid);
    dump(img, "grid-over-floor.png");

    // THE BAR IS THE GRID'S OWN PIXEL COUNT with nothing to fight: the same
    // scene with the floor dropped a metre out of the way. A z-fighting grid
    // loses a large fraction of its lines to the floor (measured at 11,218 of
    // 19,075 with the lift removed — the dashed lines in the frame), so "no
    // z-fighting" is a comparison, not a threshold somebody chose.
    floorNode->setLocalPos(iris::Vec3(0.0f, -1.0f - kFloorHalfThickness, 0.0f));
    Image clear_;
    render(clear_);
    const int gridClear = count(clear_, isGrid);
    floorNode->setLocalPos(iris::Vec3(0.0f, -kFloorHalfThickness, 0.0f));
    render(img);
    std::printf("    grid over a floor at its own height: %d pixels; with the floor out of the "
                "way: %d (%d%%)\n", gridOn, gridClear,
                gridClear ? gridOn * 100 / gridClear : 0);
    CHECK(gridOn * 100 > gridClear * 95,
          "A: the grid is drawn over a floor at its own height with no z-fighting — it keeps "
          "95 % of the pixels it has with nothing to fight");

    // ---- D: the coplanar case needs no lift ------------------------------
    //
    // GRID-2: the grid is a shader quad that writes the PLANE's own depth with
    // a relative bias toward the camera (one part in ten thousand of the
    // distance), so a floor lying ON the plane, or the shipped ground at its
    // +1e-4 m, draws under the grid without a lift — and a floor raised by a
    // real amount is real geometry and covers it, as anything standing on the
    // plane does. The sweep is kept as the record of that behaviour.
    {
        int atZero = 0;
        for (float lift : { 0.0f, 1e-4f, 0.02f, 0.10f }) {
            floorNode->setLocalPos(iris::Vec3(0.0f, lift - kFloorHalfThickness, 0.0f));
            render(img);
            const int n = count(img, isGrid);
            std::printf("    floor %+.4f m above the grid plane: %d grid pixels\n", double(lift), n);
            if (lift == 0.0f) atZero = n;
        }
        floorNode->setLocalPos(iris::Vec3(0.0f, 1e-4f - kFloorHalfThickness, 0.0f));
        render(img);
        const int atShipped = count(img, isGrid);
        CHECK(atShipped * 100 > atZero * 95 && atZero * 100 > gridClear * 95,
              "D: a floor exactly on the plane and the shipped ground at +1e-4 m both keep the "
              "grid — the analytic depth with its relative bias needs no lift");
        floorNode->setLocalPos(iris::Vec3(0.0f, 0.10f - kFloorHalfThickness, 0.0f));
        render(img);
        const int raised = count(img, isGrid);
        CHECK(raised * 100 < gridClear * 50,
              "D: ...while a floor raised 10 cm is geometry standing on the plane and covers "
              "most of it");
        floorNode->setLocalPos(iris::Vec3(0.0f, -kFloorHalfThickness, 0.0f));
        render(img);
    }

    // ---- F: the lines are ONE PIXEL wide at every distance ------------
    //
    // GRID-2's whole point against the line mesh: a line's width is a shader
    // constant in pixels, not a rasteriser's. Measured on a line that runs
    // ACROSS the screen (this camera looks down the z axis, so the z-lines are
    // horizontal on screen): the row with the most grid pixels in the near
    // band is such a line, and its thickness is the run of grid rows through
    // it at the frame's centre column.
    {
        render(img);
        unsigned bestRow = 0, bestCount = 0;
        for (unsigned y = img.height * 3 / 4; y < img.height; ++y) {
            unsigned n = 0;
            for (unsigned x = 0; x < img.width; ++x) if (isGrid(img.at(x, y))) ++n;
            if (n > bestCount) { bestCount = n; bestRow = y; }
        }
        const unsigned cx = img.width / 2 + 3;      // off the vertical centre line
        unsigned up = 0, down = 0;
        while (bestRow > up + 1 && isGrid(img.at(cx, bestRow - up - 1))) ++up;
        while (bestRow + down + 1 < img.height && isGrid(img.at(cx, bestRow + down + 1))) ++down;
        const unsigned thickness = isGrid(img.at(cx, bestRow)) ? up + down + 1 : 0;
        std::printf("    the widest near line: row %u with %u grid px; its thickness at column %u: "
                    "%u px\n", bestRow, bestCount, cx, thickness);
        CHECK(thickness >= 1 && thickness <= 3,
              "F: a grid line is a pixel wide in the near field (one to three rows through an "
              "anti-aliased edge) — the width is the shader's, not the rasteriser's");
    }

    // ---- B: the box covers it ----
    // The box's own silhouette, found by colour, then sampled well inside it:
    // whatever the perspective does, the middle of that box is the box.
    unsigned bx0 = kSize, bx1 = 0, by0 = kSize, by1 = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x)
            if (isBox(img.at(x, y))) {
                bx0 = std::min(bx0, x); bx1 = std::max(bx1, x);
                by0 = std::min(by0, y); by1 = std::max(by1, y);
            }
    const unsigned cx = (bx0 + bx1) / 2, cy = (by0 + by1) / 2;
    const unsigned q = (bx1 - bx0) / 6;                      // a sixth of its width
    int insideGrid = 0, insideBox = 0;
    for (unsigned y = cy - q; y <= cy + q; ++y)
        for (unsigned x = cx - q; x <= cx + q; ++x) {
            if (isGrid(img.at(x, y))) ++insideGrid;
            if (isBox(img.at(x, y))) ++insideBox;
        }
    std::printf("    the box's silhouette is x %u..%u, y %u..%u; inside its middle "
                "(%u x %u px): %d box pixels, %d grid pixels\n", bx0, bx1, by0, by1,
                2 * q + 1, 2 * q + 1, insideBox, insideGrid);
    CHECK(insideBox > (2 * int(q) + 1) * (2 * int(q) + 1) / 2 && insideGrid == 0,
          "B: a box standing on the floor covers the grid — the depth test still decides "
          "against real geometry");

    // ...and the floor beside the box still has it.
    int besideGrid = 0;
    for (unsigned y = by1; y < img.height; ++y)
        for (unsigned x = 0; x < bx0; ++x)
            if (isGrid(img.at(x, y))) ++besideGrid;
    std::printf("    the floor in front of and beside the box: %d grid pixels\n", besideGrid);
    CHECK(besideGrid > 20, "B: while the open floor beside it keeps its grid");

    // ---- C: it is a HELPER ----
    view->setHelpersVisible(false);
    render(img);
    const int helpersOff = count(img, isGrid);
    std::printf("    helpers off: %d grid pixels (the Player's view, and the user's "
                "Scene-grade screenshot)\n", helpersOff);
    CHECK(helpersOff == 0, "C: a view with the editor's furniture switched off draws no grid");
    CHECK(count(img, isBox) > 500, "C: ...and still draws the scene");
    view->setHelpersVisible(true);

    // ---- E: the ORTHO top view -------------------------------------------
    //
    // The case the old sign hid completely (owner 2026-09-07, "the axis-view
    // grid is not there"), which is why gridFloorOffsetForView existed at all.
    // With the grid ABOVE its plane there is nothing left for that function to
    // decide, and it is deleted.
    {
        floorNode->setLocalPos(iris::Vec3(0.0f, 1e-4f - kFloorHalfThickness, 0.0f));
        box->setVisible(false);
        cam->setProjection(iris::CameraProjection::Orthogonal);
        cam->orthoSize = 6.0f;
        cam->nearClip = 0.1f;
        cam->farClip = 1000.0f;              // the editor camera's own range
        cam->setLocalPos(iris::Vec3(0.0f, 20.0f, 0.0f));
        cam->lookAt(iris::Vec3(0, 0, 0));
        cam->update(0.0f);
        mirror.applyCamera(cam, view);
        render(img);
        dump(img, "grid-ortho-top.png");
        const int fromAbove = count(img, isGrid);
        std::printf("    ortho top view, floor at +1e-4 (the shipped ground's height): "
                    "%d grid pixels\n", fromAbove);
        CHECK(fromAbove > 200, "E: the grid is visible from straight above, over a floor at the "
                               "shipped ground's height — the case the old offset's sign hid");
        box->setVisible(true);
        cam->setProjection(iris::CameraProjection::Perspective);
        cam->setLocalPos(iris::Vec3(0.0f, 4.5f, 7.0f));
        cam->lookAt(iris::Vec3(0, 0.4f, 0));
        cam->update(0.0f);
        mirror.applyCamera(cam, view);
        floorNode->setLocalPos(iris::Vec3(0.0f, -kFloorHalfThickness, 0.0f));
    }

    mirror.setSource(nullptr);
    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
