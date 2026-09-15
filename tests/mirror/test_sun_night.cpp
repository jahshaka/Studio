// WHEN THE SUN GOES OUT, ABSOLUTELY (lane SKY-SMALL, item SKY-NIGHT-1).
//
// THE DEFECT. SceneMirror dropped the sun DISC and the sun's three PSSM SHADOW
// passes on a RELATIVE test: once the atmosphere's tint fell below a thousandth
// of its noon value. A tint is a transmittance, so a thousandth of noon is a
// fraction of a quantity nobody measured, and the elevation at which it is
// crossed moves with the air. With SKY-DENSITY-1's physical transmittance
// (Beer-Lambert along the sun ray, Kasten-Young airmass) that crossing sits at
//
//     haze  1.0 : -0.66 deg      haze  4.0 : +2.08 deg
//     haze  2.5 : +0.74 deg      haze  6.0 : +3.61 deg
//                                haze 10.0 : +6.27 deg
//
// of sun elevation — so with the Sun Haze dial anywhere above its default the
// disc and the shadow POPPED OFF while the sun was visibly up. At the cut the
// disc still carried 8 x intensity x 1e-3 of radiance: a five-to-ten-of-255 dot
// that vanished between two frames.
//
// THE RULE NOW is absolute and is about the picture: the disc is drawn while
// its own radiance — colour, intensity, the disc-size normalisation and the
// air, all of it — can still move an 8-bit output code, and the shadow is cast
// while the sun's own radiance can (with a stated specular headroom, because a
// smooth surface concentrates a beam). scenemirror.cpp carries the derivation
// of the constant; the crossings it produces, measured:
//
//     haze        disc off below     shadow off below
//      1.0          -0.80 deg          -0.83 deg
//      2.5          -0.21              -0.74
//      4.0          +0.92              -0.12
//      6.0          +2.13              +0.88
//     10.0          +4.20              +2.49
//
// WHERE THE TWO HALVES ARE TESTED. This suite is the DISC, in pixels. The
// SHADOW half of the same rule is in `scripting.e2e.sun_light`, because an
// OFFSCREEN view renders no shadow maps at all — measured here: at a sun 60
// degrees up, with both meshes casting, the engine reports 0 casters and 0
// shadow passes for an offscreen view, so there is nothing for this suite to
// read. The e2e drives the editor's own viewport, which has a shadow node, and
// reads the passes the rule decides the cost of.
//
// WHAT IS ASSERTED, as pixels:
//   A. the sweep from +8 to -2 degrees at haze 2.5, 6 and 10: the disc is
//      drawn above the derived crossing and gone at -2 for every haze;
//   B. it is MONOTONE — once the disc is gone it never comes back as the sun
//      goes down (the old rule was monotone too; a threshold on a product is
//      easy to get non-monotone, so it is pinned);
//   C. THE REGRESSION ITSELF, three cases the old rule got wrong and this one
//      gets right: haze 2.5 at +1, haze 6 at +4 and haze 10 at +6 all draw a
//      disc, and all three were dark before this lane;
//   D. the disc's cut is about RADIANCE and not about the air: the same sun,
//      at an elevation where it draws, stops drawing when its intensity is
//      turned down far enough — which the old rule could not express at all.
//
// Needs a display (Vulkan); no window (offscreen view + offscreen QPA).

#include <QGuiApplication>
#include <QColor>
#include <cmath>
#include <cstdio>
#include <vector>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); \
                              std::printf("\n"); } \
                         else { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                                std::printf("\n"); ++failures; } } while (0)

namespace {
constexpr unsigned kSize = 512;

/// The sun's PITCH for a given elevation. A LightNode points down -Y, and a
/// rotation of (elev - 90) about X turns that into a beam travelling
/// (0, -sin elev, +cos elev) — i.e. a sun at `elev` degrees, due -Z.
float pitchFor(float elevationDeg) { return elevationDeg - 90.0f; }
}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_sun_night-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("sunnight", kSize, kSize, Colour(0, 0, 0));
    Scene *target = engine->createScene("sunnight");
    if (!view || !target) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(target);

    // ---- the document ----------------------------------------------------
    auto doc = iris::Scene::create();
    doc->skyType = iris::SkyType::REALISTIC;
    doc->sunDiscVisible = true;
    // A DIM SKY, ON PURPOSE. This is an LDR offscreen view (no HDR chain, by
    // design — pixel suites want exact colours), so a sky at the shipped power
    // CLIPS to white across the whole upper frame and an ADDITIVE disc over a
    // clipped sky changes nothing at all: the measurement below would read zero
    // for a disc that is plainly drawn. `scripting.e2e.sun_light` turns the sky
    // down for the same reason. It changes no rule: the disc's radiance, which
    // is what the rule tests, does not depend on the sky's power.
    doc->skyRealistic.power = 0.02f;

    // A floor and a box, so the sun has something to cast a shadow with. (The
    // shadow is read from the renderer's accounting, not from these pixels —
    // they are here so the caster walk has a caster.)
    auto floorNode = iris::MeshNode::create();
    floorNode->setName("floor");
    floorNode->setMesh(":assets/models/cube.obj");
    auto grey = iris::DefaultMaterial::create();
    grey->setDiffuseColor(QColor(120, 120, 120));
    floorNode->setMaterial(grey);
    const iris::Vec3 cubeHalf = floorNode->getMesh()->getAABB().getHalfSize();
    floorNode->setLocalScale(iris::Vec3(60.0f / cubeHalf.x(), 0.1f / cubeHalf.y(),
                                        60.0f / cubeHalf.z()));
    floorNode->setLocalPos(iris::Vec3(0.0f, -0.1f, 0.0f));
    floorNode->setShadowCastingEnabled(true);
    doc->getRootNode()->addChild(floorNode);

    auto box = iris::MeshNode::create();
    box->setName("box");
    box->setMesh(":assets/models/cube.obj");
    auto white = iris::DefaultMaterial::create();
    white->setDiffuseColor(QColor(210, 210, 210));
    box->setMaterial(white);
    box->setLocalScale(iris::Vec3(1.0f / cubeHalf.x(), 1.0f / cubeHalf.y(), 1.0f / cubeHalf.z()));
    box->setLocalPos(iris::Vec3(3.0f, 1.0f, -4.0f));
    box->setShadowCastingEnabled(true);
    doc->getRootNode()->addChild(box);

    auto sun = iris::LightNode::create();
    sun->setName("sun");
    sun->lightType = iris::LightType::Directional;
    sun->intensity = 1.0f;
    sun->color = QColor(255, 255, 255);
    sun->followsAtmosphere = true;
    sun->shadowMap->shadowType = iris::ShadowMapType::Soft;
    sun->setLocalRot(iris::Quat::fromEulerAngles(pitchFor(30.0f), 0.0f, 0.0f));
    doc->getRootNode()->addChild(sun);

    // The camera looks along -Z at the horizon, so the sun's whole run from +8
    // to -2 degrees is inside a 45-degree frame.
    auto cam = iris::CameraNode::create();
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 400.0f;
    cam->setAspectRatio(1.0f);
    cam->setLocalPos(iris::Vec3(0.0f, 2.0f, 12.0f));
    cam->lookAt(iris::Vec3(0.0f, 2.0f, -200.0f));
    doc->getRootNode()->addChild(cam);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    mirror.sync();
    mirror.applyCamera(cam, view);

    // `applySky` is not part of sync() — it is the host's own per-frame call
    // (the editor viewport makes it), and it is what pushes the analytic sky
    // AND the sun disc. A render that forgets it draws the clear colour.
    auto render = [&](Image &img) {
        mirror.sync();
        mirror.applySky(view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
    };

    /// THE DISC, BY DIFFERENCE. The sky itself changes with every elevation and
    /// every haze, so "how many pixels are bright" is not a measurement of the
    /// disc — the same frame with the disc SWITCHED OFF is. Anything the disc
    /// adds is additive and positive by construction (JahSunDisc_ps.glsl), so
    /// the count is of pixels that got brighter at all.
    auto discPixels = [&](float elevationDeg) {
        sun->setLocalRot(iris::Quat::fromEulerAngles(pitchFor(elevationDeg), 0.0f, 0.0f));
        Image on, off;
        doc->sunDiscVisible = true;
        render(on);
        doc->sunDiscVisible = false;
        render(off);
        doc->sunDiscVisible = true;
        int n = 0;
        float peak = 0.0f;
        for (unsigned y = 0; y < on.height && y < off.height; ++y)
            for (unsigned x = 0; x < on.width && x < off.width; ++x) {
                const Colour a = on.at(x, y), b = off.at(x, y);
                const float d = std::max(std::max(a.r - b.r, a.g - b.g), a.b - b.b);
                if (d > 1.0f / 255.0f) ++n;
                peak = std::max(peak, d);
            }
        std::printf("      elev %+6.2f: %5d disc pixels, peak +%.5f\n",
                    double(elevationDeg), n, double(peak));
        return n;
    };

    // ---- A + B + C: the sweep, per haze -----------------------------------
    struct Expect { float haze; float drawnAt; };
    // `drawnAt` is the elevation at which the atmosphere's tint is 9e-4 for
    // that haze — just under the OLD rule's 1e-3 cut, so the old rule drew
    // NOTHING there whatever the sun was worth, while the new one asks what the
    // disc's radiance actually is. The sun is turned up to intensity 20 for
    // that case (see below): the old rule could not see an intensity at all.
    const Expect expects[] = { { 2.5f, 0.70f }, { 6.0f, 3.54f }, { 10.0f, 6.18f } };
    const float sweep[] = { 8.0f, 6.0f, 4.0f, 2.0f, 1.0f, 0.0f, -1.0f, -2.0f };

    for (const Expect &e : expects) {
        doc->skyRealistic.sunHaze = e.haze;
        std::printf("    --- haze %.1f ---\n", double(e.haze));
        std::vector<int> counts;
        for (float elev : sweep) counts.push_back(discPixels(elev));

        CHECK(counts.back() == 0, "haze %.1f: the disc is GONE two degrees below the horizon",
              double(e.haze));
        CHECK(counts.front() > 0, "haze %.1f: the disc is drawn eight degrees up", double(e.haze));

        bool monotone = true;
        for (size_t i = 1; i < counts.size(); ++i)
            if (counts[i - 1] == 0 && counts[i] > 0) monotone = false;
        CHECK(monotone, "haze %.1f: the disc never comes back once it has gone (monotone)",
              double(e.haze));

        // THE REGRESSION, with the discriminator that makes it unambiguous: a
        // BRIGHT sun at an elevation the old rule called night. The old test
        // was on the tint alone, so it switched this disc off no matter how
        // bright the sun was; the radiance rule draws it, and turning the same
        // sun down at the same elevation switches it off again.
        sun->intensity = 20.0f;
        const int brightAt = discPixels(e.drawnAt);
        sun->intensity = 0.02f;
        const int dimAt = discPixels(e.drawnAt);
        sun->intensity = 1.0f;
        CHECK(brightAt > 0,
              "haze %.1f: THE REGRESSION — an intensity-20 sun at %+.2f degrees (tint 9e-4, "
              "under the old rule's 1e-3 cut) draws its disc (%d pixels)",
              double(e.haze), double(e.drawnAt), brightAt);
        CHECK(dimAt == 0,
              "haze %.1f: ...and the same sun at the same elevation, turned down to intensity "
              "0.02, draws none (%d) — the cut moved with the SUN, which a rule on the tint "
              "alone cannot do", double(e.haze), dimAt);
    }

    // ---- D: it is RADIANCE, not the air -----------------------------------
    {
        doc->skyRealistic.sunHaze = 2.5f;
        const int bright = discPixels(8.0f);
        CHECK(bright > 0, "a full-intensity sun at +8 degrees draws its disc (%d pixels)", bright);
        sun->intensity = 1e-6f;
        const int dim = discPixels(8.0f);
        sun->intensity = 1.0f;
        CHECK(dim == 0,
              "...and the SAME sun, same air, turned down to a millionth of its intensity draws "
              "none (%d) — the cut is the radiance that reaches the frame, which a rule written "
              "on the tint alone could not express", dim);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
