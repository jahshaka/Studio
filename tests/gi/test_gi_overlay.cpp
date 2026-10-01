// THE GI VOLUME OVERLAY (LIGHTING_FIX fix 9).
//
// WHY IT EXISTS. The lit volume is the single most consequential thing in a GI
// scene that a user cannot see. An object outside it gets no bounce and, since
// fix 3, no VCT ambient either — and the only symptom is "that corner looks
// wrong". `world.giStatus()` has reported the resolved boxes as NUMBERS since
// the reflections lane; this draws them.
//
// WHAT IS ASSERTED, and how the "the box's corners match giStatus's bounds"
// acceptance is made measurable without a world-to-screen verb: the engine's
// camera helper puts its lookAt TARGET at the centre pixel, so aiming at a
// world point and reading the middle of the frame answers "is the wire HERE?"
// exactly.
//
// THE VOLUME IS THE RENDERER'S LIT VOLUME — the outer cascade, camera-centred (D8; the scene-fitted
// the document's bounds pin, its rows, the Fit button and world.fitGiBounds are
// deleted — the fit is the only behaviour left). So the box is not typed here;
// it is MEASURED out of `giStatus()` after the first solve, and the scene is
// laid out to make the two probes below legible: THREE separate cubes, each
// owning one axis of the union's max face, standing off to +X over empty space.
// Nothing solid is therefore at the (maxX, maxY, maxZ) corner or at the
// volume's centre, which is what makes "the wire is HERE" mean the wire.
//
//   * aim at the volume's max corner  -> the wire is there;
//   * aim at the volume's centre      -> nothing (the box is hollow, and its
//                                        far face's EDGES are not on axis);
//   * walk the camera                 -> the box FOLLOWED it (the lit volume is
//                                        the outer cascade, camera-centred): the
//                                        wire is at the new corner and the old
//                                        one is empty;
//   * turn the overlay off            -> the corner is empty again;
//   * turn GI off with the overlay on -> nothing is drawn at all (it describes
//                                        GI, it is not decoration).
//
// It links the mirror as well as the engine, like gi.coalesce: the overlay is
// mirror-side (it reads giStatus and builds the line meshes) and the helper
// channel it rides is engine-side.
#include <QGuiApplication>

#include "../support/testmesh.h"
#include <cmath>
#include <cstdio>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-overlay-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("overlay", 128, 128, Colour(0, 0, 0));
    Scene *escene = engine->createScene("overlay");
    view->setScene(escene);

    auto doc = iris::Scene::create();
    doc->giMode = iris::GiMode::VCT;
    doc->giQuality = iris::GiQuality::LOW;
    doc->giUpdateBudget = 1;
    doc->giNumBounces = 1;
    // ZERO AMBIENT is now "no Sky Light in the document" (SKY_LIGHT_SPEC.md §6):
    // ambient is the skylight and nothing else, so a scene with no Sky Light
    // pushes 27 zeros — which is exactly what the two lines that used to stand
    // here (ambientColor black + ambientFromSky off) were spelling out.
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = QColor(0, 0, 0);
    auto mat = iris::PbrMaterial::create();
    mat->setBaseColor(QColor(200, 200, 200));
    const auto cubeAt = [&](const char *name, const iris::Vec3 &pos) {
        auto n = iris::MeshNode::create();
        n->setName(QString::fromLatin1(name));
        n->setMesh(testmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj")));
        n->setLocalPos(pos);
        n->setMaterial(mat);
        doc->getRootNode()->addChild(n);
        return n;
    };
    // THE SCENE IS THE VOLUME. Three unit cubes off to +X over empty space, one
    // per axis of the union's max face: cubeX owns max X, cubeY owns max Y,
    // cubeZ owns max Z. Their union is x [6,10], y [-1,3], z [-6,-2] — the box
    // this suite used to TYPE — and no cube is anywhere near its (10, 3, -2)
    // corner or its (8, 1, -4) centre, which are the two points probed below.
    // (Three cubes rather than one was first chosen to keep the voxelizer fed
    // against a pin whose `build` threw over an empty region; the device-side
    // feed (ATOM-VOXEL-2) builds an empty volume instead, and the fixture keeps
    // its three cubes because the probed box is their union.)
    auto cubeX = cubeAt("cubeX", iris::Vec3(9.5f, -0.5f, -5.5f));
    cubeAt("cubeY", iris::Vec3(6.5f, 2.5f, -5.5f));
    cubeAt("cubeZ", iris::Vec3(6.5f, -0.5f, -2.5f));

    auto sun = iris::LightNode::create();
    sun->setName("sun");
    sun->lightType = iris::LightType::Directional;
    sun->intensity = 2.0f;
    sun->shadowMap->shadowType = iris::ShadowMapType::None;
    sun->setLocalPos(iris::Vec3(0.0f, 6.0f, 0.0f));
    doc->getRootNode()->addChild(sun);

    SceneMirror mirror(escene);
    mirror.setSource(doc);
    auto cam = iris::CameraNode::create();

    // Aim the document camera at `target` from `eye`, sync, render, and report
    // the centre region. `lookAt` puts the target at the centre pixel, so this
    // answers "is anything drawn AT this world point?".
    const auto lookAndSample = [&](const iris::Vec3 &eye, const iris::Vec3 &target) {
        cam->setLocalPos(eye);
        cam->lookAt(target);
        cam->update(0.0f);
        doc->refresh();
        mirror.sync();
        // applySky as well as applyEnvironment: without it the view renders the
        // engine's default sky, which is bright, and every probe below would
        // read 1.0 whatever the overlay did.
        mirror.applySky(view);
        mirror.applyEnvironment(view, engine.get());
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        Image img; view->readPixels(img);
        // A 5x5 window: a one-pixel wireframe aimed at exactly can still land a
        // pixel off, and the assertion is "the wire is HERE", not "the
        // rasteriser rounded the way I expected".
        float best = 0.0f;
        Colour bc;
        for (int y = 61; y <= 67; ++y)
            for (int x = 61; x <= 67; ++x) {
                const Colour c = img.at(x, y);
                const float l = (c.r + c.g + c.b) / 3.0f;
                if (l > best) { best = l; bc = c; }
            }
        std::printf("      [at (%.1f,%.1f,%.1f): %.3f  rgb %.2f %.2f %.2f]\n",
                    target.x(), target.y(), target.z(), best, bc.r, bc.g, bc.b);
        return best;
    };

    // HEAD-ON, not corner-on, and that is load-bearing: seen down a diagonal a
    // wireframe box puts its FAR corner very near the middle of the frame, so
    // the "the box is hollow" probe would read the far corner's wire instead of
    // empty space (measured, and it is what the first version of this suite got
    // wrong). Straight down -Z the image centre is the middle of a FACE at both
    // ends of the box, which is exactly what "hollow" means.
    const iris::Vec3 farEye(8.0f, 1.0f, 12.0f);

    // THE BOX IS READ, NEVER ASSUMED: one settle, then the resolved volume out
    // of giStatus is what every aim below is derived from. `maxCorner` is the
    // (maxX, maxY, maxZ) corner; `midPoint` the volume's CENTRE — the box is a
    // wireframe, so a ray through its middle enters and leaves through the
    // middle of two FACES and crosses no edge, which is what makes "the wire is
    // at the corner" mean the corner and not "somewhere in that direction".
    const auto settle = [&](int frames) {
        for (int f = 0; f < frames; ++f) {
            doc->refresh();
            mirror.sync();
            mirror.applySky(view);
            mirror.applyEnvironment(view, engine.get());
            mirror.applyCamera(cam, view);
            engine->renderOneFrame();
        }
    };
    const auto maxCorner = [&] {
        const GiStatus st = escene->giStatus();
        return iris::Vec3(st.boundsMax.x, st.boundsMax.y, st.boundsMax.z);
    };
    const auto midPoint = [&] {
        const GiStatus st = escene->giStatus();
        return iris::Vec3((st.boundsMin.x + st.boundsMax.x) * 0.5f,
                          (st.boundsMin.y + st.boundsMax.y) * 0.5f,
                          (st.boundsMin.z + st.boundsMax.z) * 0.5f);
    };
    cam->setLocalPos(farEye);
    cam->lookAt(iris::Vec3(8.0f, 1.0f, -4.0f));
    cam->update(0.0f);
    settle(30);
    {
        const GiStatus st = escene->giStatus();
        std::printf("   the AUTOMATIC volume: %.2f %.2f %.2f .. %.2f %.2f %.2f\n",
                    st.boundsMin.x, st.boundsMin.y, st.boundsMin.z,
                    st.boundsMax.x, st.boundsMax.y, st.boundsMax.z);
        // THE LIT VOLUME IS THE OUTERMOST CASCADE (D4-PHOTON-TIERS deleted the
        // scene-fitted volume the overlay used to draw): a box centred on the
        // camera, so it is a non-empty box around the eye.
        CHECK(st.boundsMax.x > st.boundsMin.x && st.boundsMin.x < farEye.x() &&
                  st.boundsMax.x > farEye.x(),
              "the lit volume is the outer cascade's box, around the eye");
    }
    const iris::Vec3 corner = maxCorner();
    const iris::Vec3 centre = midPoint();

    // ---- off by default ----------------------------------------------------
    CHECK(!mirror.giVolumeOverlay(), "the overlay is OFF by default (it is a diagnostic)");
    const float cornerOff = lookAndSample(farEye, corner);
    std::printf("   overlay off, aimed at the max corner: %.3f\n", cornerOff);
    CHECK(cornerOff < 0.02f, "...and draws nothing");

    // ---- on ----------------------------------------------------------------
    mirror.setGiVolumeOverlay(true);
    CHECK(mirror.giVolumeOverlay(), "the toggle reads back");
    const float cornerOn = lookAndSample(farEye, corner);
    const float centreOn = lookAndSample(farEye, centre);
    std::printf("   overlay on:  max corner %.3f   volume centre %.3f\n", cornerOn, centreOn);
    // THE ACCEPTANCE: the box's corner is where giStatus says the bounds are.
    CHECK(cornerOn > 0.10f, "the wire box's max CORNER is exactly at giStatus's boundsMax");
    CHECK(centreOn < 0.02f, "...and the box is HOLLOW: nothing through its centre");

    // ---- move the CAMERA: the box must follow it -----------------------------
    // The lit volume is camera-centred, so the way to move it is to walk: 12 m
    // along +X and +Z re-centres the outer cascade (Low's outer step is a few metres).
    // Diagonally (X and Z): a walk along one axis leaves the old max corner ON the
    // new box's edge that runs along that axis.
    const iris::Vec3 walkedEye = farEye + iris::Vec3(12.0f, 0.0f, 12.0f);
    cam->setLocalPos(walkedEye);
    cam->update(0.0f);
    settle(30);
    const iris::Vec3 newCorner = maxCorner();
    std::printf("   after walking 12 m: new corner %.2f %.2f %.2f\n",
                newCorner.x(), newCorner.y(), newCorner.z());
    CHECK(newCorner.x() > corner.x() + 2.0f && newCorner.z() > corner.z() + 2.0f,
          "the lit volume FOLLOWED the camera");
    const float oldCornerAfter = lookAndSample(walkedEye, corner);
    const float newCornerAfter = lookAndSample(walkedEye, newCorner);
    std::printf("   after the walk:  old corner %.3f   new corner %.3f\n",
                oldCornerAfter, newCornerAfter);
    CHECK(newCornerAfter > 0.10f, "the box MOVED with the volume the renderer reports");
    CHECK(oldCornerAfter < 0.02f, "...and left the old corner empty");
    (void)cubeX;

    // ---- GI off: the overlay describes GI, it is not decoration -------------
    doc->giMode = iris::GiMode::OFF;
    const float withGiOff = lookAndSample(walkedEye, newCorner);
    std::printf("   GI off, overlay still on: %.3f\n", withGiOff);
    CHECK(withGiOff < 0.02f, "with GI off the overlay draws NOTHING");

    // ---- and the toggle really is the switch --------------------------------
    doc->giMode = iris::GiMode::VCT;
    const float backOn = lookAndSample(walkedEye, newCorner);
    mirror.setGiVolumeOverlay(false);
    const float toggledOff = lookAndSample(walkedEye, newCorner);
    std::printf("   GI back on: %.3f   overlay toggled off: %.3f\n", backOn, toggledOff);
    CHECK(backOn > 0.10f, "turning GI back on brings the box back");
    CHECK(toggledOff < 0.02f, "turning the overlay off hides it");

    doc->giMode = iris::GiMode::OFF;
    doc->refresh();
    mirror.sync();
    mirror.applyEnvironment(view, engine.get());
    mirror.setSource(iris::ScenePtr());
    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
