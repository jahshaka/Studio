// THE EDITOR'S GROUND GRID IS A HELPER DRAWN ON TOP OF THE FLOOR
// (GIZMO-2 item 5; owner, ledger §370: "the grid should be a gizmo-class asset:
// today when you turn the grid on it MERGES with the floor; it should always be
// on top of the floor, but objects on top of it should cover it").
//
// WHAT IT USED TO DO. SceneMirror put the floor grid a hair BELOW y=0
// (mGridFloorOffset = -0.01) so the ground would occlude it instead of
// z-fighting with it — which means that in any scene with a ground plane the
// grid was simply INVISIBLE from above the floor (measured on the shipping
// build for this lane: a plain-grade screenshot of the default project with the
// grid on contains not one grid pixel), and where it did poke through it fought.
// A top view had to flip the sign of that offset to see anything at all.
//
// WHAT IT DOES NOW. The grid sits at exactly y=0, keeps its depth TEST, and its
// material carries a depth bias TOWARDS the camera (Scene::setMaterialDepthPriority)
// — so it wins against the coplanar floor at every angle and distance, and
// loses to anything genuinely in front of it.
//
// THE THREE CLAIMS, as pixels:
//   A. with the grid on, its lines are drawn OVER the floor (they were not);
//   B. a box standing on the floor COVERS the grid inside its silhouette;
//   C. the grid is a HELPER: a view with the editor's furniture switched off
//      (View::setHelpersVisible(false) — what the Player page is, and what the
//      user's Scene-grade screenshot does) draws none of it.
//
// Needs a display (Vulkan); no window (offscreen view + offscreen QPA).

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <cstdio>

#include "irisgl/irisglfwd.h"
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

/// A flat quad in the XZ plane at exactly y = 0 — the floor, coplanar with the
/// grid by construction, which is the whole subject of this suite.
iris::MeshPtr floorQuad(float half)
{
    const float h = half;
    const float pos[] = { -h, 0, -h,   h, 0, -h,   h, 0,  h,
                          -h, 0, -h,   h, 0,  h,  -h, 0,  h };
    const float nrm[] = {  0, 1, 0,  0, 1, 0,  0, 1, 0,  0, 1, 0,  0, 1, 0,  0, 1, 0 };
    auto mesh = iris::Mesh::create();
    iris::VertexLayout posLayout;
    posLayout.addAttrib(iris::VertexAttribUsage::Position, iris::AttribTypeFloat, 3, sizeof(float) * 3);
    auto pb = iris::VertexBuffer::create(posLayout);
    pb->setData((void *)pos, sizeof pos);
    mesh->addVertexBuffer(pb);
    iris::VertexLayout nrmLayout;
    nrmLayout.addAttrib(iris::VertexAttribUsage::Normal, iris::AttribTypeFloat, 3, sizeof(float) * 3);
    auto nb = iris::VertexBuffer::create(nrmLayout);
    nb->setData((void *)nrm, sizeof nrm);
    mesh->addVertexBuffer(nb);
    mesh->setPrimitiveMode(iris::PrimitiveMode::Triangles);
    mesh->setVertexCount(6);
    return mesh;
}

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

    auto floorNode = iris::MeshNode::create();
    floorNode->setName("floor");
    floorNode->setMesh(floorQuad(20.0f));
    auto grey = iris::DefaultMaterial::create();
    grey->setDiffuseColor(QColor(70, 70, 70));
    floorNode->setMaterial(grey);
    doc->getRootNode()->addChild(floorNode);

    auto box = iris::MeshNode::create();
    box->setName("box");
    box->setMesh(":assets/models/cube.obj");
    auto green = iris::DefaultMaterial::create();
    green->setDiffuseColor(QColor(0, 220, 0));
    box->setMaterial(green);
    const float radius = box->getMeshRadius();
    const float s = radius > 0.0f ? 1.0f / radius : 1.0f;   // unit radius
    box->setLocalScale(iris::Vec3(s, s, s));
    // A unit-radius cube's half-extent is 1/sqrt(3): stand it ON the floor.
    box->setLocalPos(iris::Vec3(0.0f, 0.57735f, 0.0f));
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

    Image img;
    render(img);
    const int gridOff = count(img, isGrid);
    const int boxPixels = count(img, isBox);
    std::printf("    grid off: %d grid-coloured pixels, %d box pixels\n", gridOff, boxPixels);
    CHECK(gridOff == 0, "with the grid off there is nothing grid-coloured in the frame");
    CHECK(boxPixels > 500, "the box is in frame (the thing that has to cover the grid)");

    // ---- A: the grid draws over the floor ----
    mirror.setGridColours(Colour(1.0f, 0.0f, 0.0f, 1.0f), Colour(1.0f, 0.0f, 0.0f, 1.0f));
    mirror.setGridExtent(20.0f);
    mirror.setGrid(true, 1.0f, SceneMirror::GridPlane::Floor);
    render(img);
    const int gridOn = count(img, isGrid);
    std::printf("    grid on:  %d grid pixels over a floor at exactly the grid's own height\n",
                gridOn);
    CHECK(gridOn > 200, "A: the grid is drawn ON the floor it is coplanar with — the depth "
                        "bias wins the tie (before GIZMO-2 the grid was pushed under the "
                        "floor and this was zero)");

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

    mirror.setSource(nullptr);
    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
