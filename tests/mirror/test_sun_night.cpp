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
// air, all of it — can still move an 8-bit output code AFTER THE VIEW'S
// EXPOSURE, and the shadow is cast while the sun's own radiance can, with the
// pipeline's own specular clamp as the headroom (a smooth surface concentrates
// a beam). scenemirror.cpp carries the derivation.
//
// THE CROSSINGS IT PRODUCES, and note they MOVE WITH THE GRADE, which is the
// whole point of carrying the exposure: one 8-bit code of output is a smaller
// slice of scene radiance the further the chain opens up.
//
//     haze |     old rule | disc off below | shadow off below
//          |              |  gain 1 / 1.70 / 30.9
//      1.0 |   -0.66 deg  | -0.79 / -0.80 / -0.83 | -0.83 / -0.84 / -0.84
//      2.5 |   +0.74      | -0.13 / -0.27 / -0.64 | -0.82 / -0.82 / -0.83
//      4.0 |   +2.08      | +1.00 / +0.85 / +0.19 | -0.58 / -0.62 / -0.76
//      6.0 |   +3.61      | +2.24 / +2.05 / +1.24 | +0.26 / +0.19 / -0.16
//     10.0 |   +6.27      | +4.35 / +4.08 / +2.97 | +1.67 / +1.58 / +1.15
//
// (gain 1 = HDR off, which is what THIS suite's offscreen view runs at; 1.70 =
// the default HDR grade; 30.9 = exposure +2 with exposureMax +4, a perfectly
// ordinary bright grade. At that last one a disc at haze 6 and +2.13 degrees —
// where a rule frozen at the default gain would have dropped it — is still
// worth 23 output codes, which is the pop this lane exists to remove.)
//
// BOTH HALVES OF THE RULE ARE HERE. The disc is measured in pixels; the shadow
// is measured as the renderer's own pass count, because at the elevations this
// rule is about the sun's light is a thousandth of an output code — there is no
// shadow left to photograph, and what the rule decides is whether three
// full-view-frustum passes are rendered for it. (`scripting.e2e.sun_light`
// covers the shadow half a second time, in the editor's own viewport.)
//
// A NOTE FOR THE NEXT READER, because this suite's first cut got it wrong and
// wrote the mistake down as an engine fact: an offscreen view renders shadows
// perfectly well. `OgreView::mShadows` simply defaults to FALSE and the HOST
// turns it on (enginesceneviewport.cpp:620) — a suite that forgets
// `view->setShadows(true)` measures its own setup, not the engine.
//
// WHAT IS ASSERTED:
//   A. the sweep from +8 to -2 degrees at haze 2.5, 6 and 10: the disc is
//      drawn above the derived crossing and gone at -2 for every haze;
//   B. it is MONOTONE — once the disc is gone it never comes back as the sun
//      goes down (the old rule was monotone too; a threshold on a product is
//      easy to get non-monotone, so it is pinned);
//   C. THE REGRESSION ITSELF, three cases the old rule got wrong and this one
//      gets right — a bright sun at the elevation where the tint is 9e-4, just
//      under the old rule's 1e-3 cut, at each of the three hazes;
//   D. THE SHADOW: cast at +4 degrees at haze 2.5 and 6, gone two degrees below
//      the horizon at every haze, and — the regression — still cast at haze 6
//      two degrees up, where the old relative rule had stopped it at +3.6;
//   E. the disc's cut is about RADIANCE and not about the air: the same sun,
//      at an elevation where it draws, stops drawing when its intensity is
//      turned down far enough — which the old rule could not express at all.
//
// Needs a display (Vulkan); no window (offscreen view + offscreen QPA).

#include <QGuiApplication>

#include "tests/support/testmesh.h"
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
    // THE HOST'S CALL, which a suite has to make for itself (see the header):
    // without it this view has no shadow node and the shadow half below would
    // be measuring the setup rather than the rule.
    view->setShadows(true);

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
    {
        // THROUGH THE ONE WRITER (SKY-WRITE-1), like every other write of these
        // dials — a suite that bypasses the law it sits beside is the next
        // reader's counter-example.
        iris::SkyRealistic r = doc->skyRealistic;
        r.power = 0.02f;
        doc->setSkyRealistic(r);
    }

    // A floor and a box, so the sun has something to cast a shadow with. (The
    // shadow is read from the renderer's accounting, not from these pixels —
    // they are here so the caster walk has a caster.)
    auto floorNode = iris::MeshNode::create();
    floorNode->setName("floor");
    floorNode->setMesh(testmesh::load(":assets/models/cube.obj"));
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
    box->setMesh(testmesh::load(":assets/models/cube.obj"));
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
        { iris::SkyRealistic r = doc->skyRealistic; r.sunHaze = e.haze; doc->setSkyRealistic(r); }
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

    // ---- D: the shadow ----------------------------------------------------
    //
    // The renderer's own answer, not a pixel count: the engine's shadow-pass
    // counters are OPT-IN and asking is what arms them, so every reading here
    // is arm -> render -> read. (A first read reports countersMeasured = false
    // and null counters; this helper never measures off one.)
    auto sunCastPasses = [&](float elevationDeg) {
        sun->setLocalRot(iris::Quat::fromEulerAngles(pitchFor(elevationDeg), 0.0f, 0.0f));
        Image img;
        engine->shadowStatus();          // arm
        render(img);                     // ...and render a frame under the listeners
        const ShadowStatus st = engine->shadowStatus();
        if (!st.countersMeasured) { std::printf("FAIL: counters not armed\n"); ++failures; }
        std::printf("      elev %+6.2f: %u shadow passes\n",
                    double(elevationDeg), st.shadowPassesLastFrame);
        return st.shadowPassesLastFrame;
    };
    {
        { iris::SkyRealistic r = doc->skyRealistic; r.sunHaze = 2.5f; doc->setSkyRealistic(r); }
        const unsigned up25 = sunCastPasses(4.0f);
        const unsigned down25 = sunCastPasses(-2.0f);
        CHECK(up25 > 0, "D: haze 2.5 — the sun casts at +4 degrees (%u passes)", up25);
        CHECK(down25 < up25,
              "D: haze 2.5 — and casts less two degrees below the horizon (%u)", down25);

        { iris::SkyRealistic r = doc->skyRealistic; r.sunHaze = 6.0f; doc->setSkyRealistic(r); }
        const unsigned up6 = sunCastPasses(4.0f);
        // THE REGRESSION. At haze 6 and two degrees up the air has taken the
        // transmittance to 3.2e-5 — a thirtieth of the old rule's cut, so the
        // old rule had stopped the shadow — while the sun's radiance is still
        // four thousand times the step at which it stops being able to darken a
        // pixel (4,212x at a gain of one, 7,166x at the default HDR grade).
        const unsigned regression6 = sunCastPasses(2.0f);
        const unsigned down6 = sunCastPasses(-2.0f);
        CHECK(up6 > 0, "D: haze 6 — the sun casts at +4 degrees (%u passes)", up6);
        CHECK(regression6 > 0,
              "D: haze 6 — THE REGRESSION: it still casts two degrees up (%u passes), where "
              "the old relative rule had stopped its shadow at +3.6 degrees", regression6);
        CHECK(down6 < regression6,
              "D: haze 6 — and casts less two degrees below the horizon (%u)", down6);

        { iris::SkyRealistic r = doc->skyRealistic; r.sunHaze = 10.0f; doc->setSkyRealistic(r); }
        const unsigned up10 = sunCastPasses(8.0f);
        const unsigned down10 = sunCastPasses(-2.0f);
        CHECK(down10 < up10,
              "D: haze 10 — nothing is cast two degrees below the horizon (%u against %u up)",
              down10, up10);
        { iris::SkyRealistic r = doc->skyRealistic; r.sunHaze = 2.5f; doc->setSkyRealistic(r); }
    }

    // ---- E: it is RADIANCE, not the air -----------------------------------
    {
        { iris::SkyRealistic r = doc->skyRealistic; r.sunHaze = 2.5f; doc->setSkyRealistic(r); }
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
