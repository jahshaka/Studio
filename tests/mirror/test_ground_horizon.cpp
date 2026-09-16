// mirror.ground_horizon — A HIDE IS NOT A DELETE (DRAG-1; render audit IRISGL
// I-2, and the owner's smoke report "hiding the plane changed its size").
//
// WHAT THE MIRROR DOES. The default floor is a 100 m square, and the ground
// reads as infinite because SceneMirror owns a 4 km HORIZON plane behind it,
// built from the floor's own mesh and material and fitted to the floor's UV map
// (syncGroundHorizon). The horizon follows the floor: no floor, no horizon.
//
// THE DEFECT. "No floor" was tested as `!floor->isVisibleInScene()`, and BOTH
// branches of that — the floor was deleted, and the floor was merely HIDDEN —
// detached the horizon's mesh and released its material, which the cache sweep
// then reclaimed. So hiding the ground destroyed the 4 km plane, and SHOWING it
// again had to rebuild the mesh, re-fit the UV map over the floor's vertices
// and create a material: the user saw the ground go from infinite to a 100 m
// square, and come back a frame or more later. Hiding a thing must not destroy
// it.
//
// THE THREE CLAIMS:
//   A. showing the floor gives an infinite ground: a pixel far past the floor's
//      own 100 m edge is GROUND, not background;
//   B. a HIDE keeps the horizon's mesh and material alive — the engine's object
//      census does not fall — and the horizon is merely invisible;
//   C. a SHOW is infinite again in the VERY NEXT FRAME, with no rebuild: the
//      far pixel is ground on the first frame after the show, and the census is
//      where it was before the hide.
//
// The DELETE half of the old behaviour is deliberately left alone and is not
// asserted here: a deleted floor must still take its horizon's material with
// it, which is what the branch that survives exists for.
#include <QGuiApplication>
#include <QColor>
#include <cstdio>
#include <cmath>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace { constexpr unsigned kSize = 256; }

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_ground_horizon-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // A sky-blue background, so "the ground ended" is unmistakable.
    View *view = engine->createOffscreenView("horizon", kSize, kSize, Colour(0.0f, 0.2f, 0.8f));
    Scene *target = engine->createScene("horizon");
    if (!view || !target) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(target);
    target->setAmbient(Colour(0.5f, 0.5f, 0.5f), Colour(0.4f, 0.4f, 0.4f));

    auto doc = iris::Scene::create();
    auto floorNode = iris::MeshNode::create();
    floorNode->setName("Ground");
    floorNode->setMesh(":/models/ground.obj");
    floorNode->defaultFloor = true;              // what makes it THE floor
    auto grey = iris::DefaultMaterial::create();
    grey->setDiffuseColor(QColor(200, 200, 200));
    floorNode->setMaterial(grey);
    doc->getRootNode()->addChild(floorNode);

    auto sun = iris::LightNode::create();
    sun->intensity = 1.2f;
    sun->setLocalRot(iris::Quat::fromEulerAngles(-45.0f, 20.0f, 0.0f));
    doc->getRootNode()->addChild(sun);

    // A camera low and level, looking at the horizon: the ground fills the
    // bottom half and the sky the top, and the band just below the skyline is
    // MANY HUNDREDS of metres away — past the floor's own 100 m edge, so only
    // the horizon plane can fill it.
    auto cam = iris::CameraNode::create();
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 4000.0f;
    cam->setAspectRatio(1.0f);
    cam->setLocalPos(iris::Vec3(0.0f, 1.6f, 0.0f));
    cam->lookAt(iris::Vec3(0.0f, 1.55f, -100.0f));
    doc->getRootNode()->addChild(cam);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    mirror.sync();
    mirror.applyCamera(cam, view);

    Image img;
    const auto renderN = [&](int n) {
        for (int i = 0; i < n; ++i) { mirror.sync(); engine->renderOneFrame(); }
        view->readPixels(img);
    };
    const auto isSky = [](const Colour &c) { return c.b > c.r * 1.8f && c.b > 0.25f; };
    // The row just below the skyline: the farthest GROUND pixels in the frame.
    const auto groundBandIsGround = [&]() {
        int ground = 0, total = 0;
        for (unsigned y = kSize / 2 + 1; y < kSize / 2 + 9; ++y)
            for (unsigned x = kSize / 4; x < 3 * kSize / 4; ++x) {
                ++total;
                if (!isSky(img.at(x, y))) ++ground;
            }
        return total ? double(ground) / double(total) : 0.0;
    };
    const auto census = [&]() {
        ObjectCounts c;
        engine->objectCounts(c);
        return c;
    };

    // ---- A. the ground is infinite ---------------------------------------
    renderN(6);
    const double shownFrac = groundBandIsGround();
    const ObjectCounts before = census();
    std::printf("   shown: %.1f%% of the far band is ground; census meshes %u materials %u nodes %u\n",
                100.0 * shownFrac, before.meshes, before.materials, before.nodes);
    CHECK(shownFrac > 0.95, "A: with the floor shown, the ground reaches the horizon");

    // ---- B. a HIDE keeps the horizon alive -------------------------------
    floorNode->setVisible(false);
    renderN(6);
    const ObjectCounts hidden = census();
    const double hiddenFrac = groundBandIsGround();
    std::printf("   hidden: %.1f%% of the far band is ground; census meshes %u materials %u nodes %u\n",
                100.0 * hiddenFrac, hidden.meshes, hidden.materials, hidden.nodes);
    CHECK(hiddenFrac < 0.05, "B: a hidden floor takes its horizon with it (nothing is drawn)");
    CHECK(hidden.meshes >= before.meshes,
          "B: A HIDE DESTROYS NO MESH — the horizon's 4 km plane is still there");
    CHECK(hidden.materials >= before.materials,
          "B: ...and no material: hiding a thing must not destroy it");

    // ---- C. a SHOW is infinite in the very next frame --------------------
    floorNode->setVisible(true);
    mirror.sync();
    engine->renderOneFrame();
    view->readPixels(img);
    const double firstFrameFrac = groundBandIsGround();
    const ObjectCounts shown = census();
    std::printf("   first frame after the show: %.1f%% of the far band is ground; "
                "census meshes %u materials %u\n",
                100.0 * firstFrameFrac, shown.meshes, shown.materials);
    CHECK(firstFrameFrac > 0.95,
          "C: THE GROUND IS INFINITE AGAIN IN THE VERY NEXT FRAME (no rebuild, no size change)");
    CHECK(shown.meshes == before.meshes && shown.materials == before.materials,
          "C: ...and the census is exactly where it was: nothing was rebuilt");

    std::printf("\n%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
