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
// exactly. The GI bounds are typed explicitly and placed over EMPTY SPACE, so
// nothing but the overlay can be in that part of the picture:
//
//   * aim at the volume's max corner  -> the wire is there;
//   * aim at the volume's centre      -> nothing (the box is hollow, and its
//                                        far face's EDGES are not on axis);
//   * move the bounds, re-aim         -> the wire moved with them, and the old
//                                        corner is empty;
//   * turn the overlay off            -> the corner is empty again;
//   * turn GI off with the overlay on -> nothing is drawn at all (it describes
//                                        GI, it is not decoration).
//
// It links the mirror as well as the engine, like gi.coalesce: the overlay is
// mirror-side (it reads giStatus and builds the line meshes) and the helper
// channel it rides is engine-side.
#include <QGuiApplication>
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
    doc->giAutoRefresh = true;
    doc->giNumBounces = 1;
    doc->ambientColor = QColor(0, 0, 0);
    doc->ambientFromSky = false;
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = QColor(0, 0, 0);
    // EXPLICIT bounds, over empty space off to +X: the overlay is then the only
    // thing that can be in the part of the frame this suite looks at.
    const iris::Vec3 boundsMin(6.0f, -1.0f, -6.0f), boundsMax(10.0f, 3.0f, -2.0f);
    doc->giBoundsMin = boundsMin;
    doc->giBoundsMax = boundsMax;

    auto cube = iris::MeshNode::create();
    cube->setName("cube");
    cube->setMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"));
    cube->setLocalPos(iris::Vec3(0.0f, 0.5f, 0.0f));
    auto mat = iris::PbrMaterial::create();
    mat->setBaseColor(QColor(200, 200, 200));
    cube->setMaterial(mat);
    doc->getRootNode()->addChild(cube);

    // A SECOND cube, INSIDE the explicit GI region. Not decoration: measured
    // this lane, `VctVoxelizer::build` over a region containing no geometry at
    // all leaves its AabbWorldSpace compute job with no thread groups set and
    // throws ("Shader or C++ must set threads_per_group_x..."), so the whole GI
    // arm silently fails to build and there is no volume to draw. Upstream
    // behaviour; recorded here rather than worked around, because a user CAN
    // type such a box and the honest answer is a panel warning, not a fudge.
    auto inner = iris::MeshNode::create();
    inner->setName("inner");
    inner->setMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"));
    // Tucked into the region's far upper corner and half size, so it is nowhere
    // near the camera rays this suite fires at the box's near corners.
    inner->setLocalPos(iris::Vec3(6.5f, 2.5f, -5.5f));
    inner->setLocalScale(iris::Vec3(0.5f, 0.5f, 0.5f));
    inner->setMaterial(mat);
    doc->getRootNode()->addChild(inner);

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
        doc->update(0.016f);
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
    const iris::Vec3 corner(boundsMax.x(), boundsMax.y(), boundsMax.z());
    // The volume's CENTRE: the box is a wireframe, so a ray through its middle
    // enters and leaves through the middle of two FACES and crosses no edge.
    // That is what makes "the wire is at the corner" mean the corner and not
    // "the wire is somewhere in that direction".
    const iris::Vec3 centre((boundsMin.x() + boundsMax.x()) * 0.5f,
                            (boundsMin.y() + boundsMax.y()) * 0.5f,
                            (boundsMin.z() + boundsMax.z()) * 0.5f);

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

    // Cross-check the numbers the overlay is drawing from.
    {
        const GiStatus st = escene->giStatus();
        std::printf("   giStatus bounds: %.2f %.2f %.2f .. %.2f %.2f %.2f\n",
                    st.boundsMin.x, st.boundsMin.y, st.boundsMin.z,
                    st.boundsMax.x, st.boundsMax.y, st.boundsMax.z);
        CHECK(std::fabs(st.boundsMax.x - boundsMax.x()) < 0.01f &&
              std::fabs(st.boundsMax.y - boundsMax.y()) < 0.01f &&
              std::fabs(st.boundsMax.z - boundsMax.z()) < 0.01f,
              "giStatus reports the typed bounds (so the corner above IS boundsMax)");
    }

    // ---- move the bounds: the box must follow -------------------------------
    const iris::Vec3 newMin(6.0f, -1.0f, -6.0f), newMax(9.0f, 2.0f, -3.0f);
    doc->giBoundsMin = newMin;
    doc->giBoundsMax = newMax;
    const iris::Vec3 newCorner(newMax.x(), newMax.y(), newMax.z());
    const float oldCornerAfter = lookAndSample(farEye, corner);
    const float newCornerAfter = lookAndSample(farEye, newCorner);
    std::printf("   after moving the bounds:  old corner %.3f   new corner %.3f\n",
                oldCornerAfter, newCornerAfter);
    CHECK(newCornerAfter > 0.10f, "the box MOVED to the new bounds");
    CHECK(oldCornerAfter < 0.02f, "...and left the old corner empty");

    // ---- GI off: the overlay describes GI, it is not decoration -------------
    doc->giMode = iris::GiMode::OFF;
    const float withGiOff = lookAndSample(farEye, newCorner);
    std::printf("   GI off, overlay still on: %.3f\n", withGiOff);
    CHECK(withGiOff < 0.02f, "with GI off the overlay draws NOTHING");

    // ---- and the toggle really is the switch --------------------------------
    doc->giMode = iris::GiMode::VCT;
    const float backOn = lookAndSample(farEye, newCorner);
    mirror.setGiVolumeOverlay(false);
    const float toggledOff = lookAndSample(farEye, newCorner);
    std::printf("   GI back on: %.3f   overlay toggled off: %.3f\n", backOn, toggledOff);
    CHECK(backOn > 0.10f, "turning GI back on brings the box back");
    CHECK(toggledOff < 0.02f, "turning the overlay off hides it");

    doc->giMode = iris::GiMode::OFF;
    doc->update(0.016f);
    mirror.sync();
    mirror.applyEnvironment(view, engine.get());
    mirror.setSource(iris::ScenePtr());
    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
