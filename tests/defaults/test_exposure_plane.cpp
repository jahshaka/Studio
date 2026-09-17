// defaults.exposure_plane — THE EDITOR'S DEFAULT EXPOSURE, and the symptom it
// ends (EXPOSURE-1, 2026-09-17; RENDER AUDIT A1/A3/A13, SMOKE-41 symptom 5).
//
// THE SYMPTOM, as the owner met it: drop a default plane into a new scene and
// every object in it darkens by well over half. Measured by SMOKE-41 on the
// real editor — a gold cube 53 -> 20, two spheres 70 -> 28 and 41 -> 18 — while
// the SAME scene with the exposure frozen got BRIGHTER, because a plane bounces
// light. Nothing was wrong with the plane, the GI or the renderer: the editor
// defaulted to an AVERAGING METER, and a white surface filling the frame is
// what an averaging meter is worst at. A meter is right for a camera and wrong
// for an authoring tool.
//
// WHAT THIS SUITE PINS, and why each part is here rather than somewhere else:
//
//   1  THE SYMPTOM AND THE GUARD, in pixels. The same scene, the same plane,
//      twice: under AUTO the objects must move a LOT (if they ever stop, the
//      guard below has stopped meaning anything and this suite says so), and
//      under MANUAL they must move less than 0.3 stop. That number is the
//      whole feature: a default plane may change what the objects RECEIVE
//      (bounce light is real) and must not change how the picture is DEVELOPED.
//
//   2  THE RESOLUTION, document -> renderer. One ExposureDesc crosses the
//      mirror and is converted once; Manual resolves to the chain's FIXED
//      form, Auto to the meter with its window; a driving camera substitutes
//      its own block, and `Inherit` substitutes nothing.
//
//   3  THE DEFAULTS THE READER FALLS BACK TO. SceneReader default-constructs a
//      Scene and overwrites the keys a file carries, so "absent key == the
//      constructor" is structural — and the constructor's values are asserted
//      here, which is the half a test can hold without linking Studio's io
//      (tests/mirror records why nothing links SceneReader). The runtime half
//      is scripting.e2e.exposure_defaults, which opens a SHIPPED ARCHIVE
//      written before this lane — a real file with no exposure keys at all.
//
// Runs with QT_QPA_PLATFORM=offscreen and a reachable DISPLAY (Vulkan).

#include <QGuiApplication>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
} while (0)

namespace {

Engine *gEngine = nullptr;
constexpr int kW = 256, kH = 160;

void frames(int n) { for (int i = 0; i < n; ++i) gEngine->renderOneFrame(); }

/// Mean luminance of a rectangle of the frame, 0..255. The objects are measured
/// through BOXES rather than over the whole picture, because the whole picture
/// is exactly what the plane changes — a frame mean would measure the plane.
double boxLuma(const Image &img, int x0, int y0, int x1, int y1)
{
    double sum = 0.0;
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const size_t i = (size_t(y) * size_t(kW) + size_t(x)) * 4;
            if (i + 3 >= img.rgba.size()) continue;
            sum += 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
            ++n;
        }
    }
    return n ? sum / double(n) : 0.0;
}

/// Stops between two luminances. Both are post-tonemap display values, so this
/// is a reading of the PICTURE's movement and not of the exposure term — which
/// is the honest way round: the assertion is about what the user sees.
double stopsBetween(double a, double b)
{
    if (a <= 0.01 || b <= 0.01) return 99.0;
    return std::log(b / a) / std::log(2.0);
}

/// Renders until the picture holds still and reads the three object boxes.
/// FRAMES, never wall clock (the engine has no wall clock — CLAUDE.md), and
/// "still" rather than a frame count, because the automatic arm has to be given
/// every chance to converge or its half of the comparison is unfair.
struct Objects { double gold, left, right; };
Objects settledObjects(View *v, const char *label)
{
    Image img;
    double prev = -1.0;
    for (int spent = 0; spent < 900; spent += 15) {
        frames(15);
        v->readPixels(img);
        const double whole = boxLuma(img, 0, 0, kW, kH);
        if (prev >= 0.0 && std::fabs(whole - prev) < 0.02) break;
        prev = whole;
    }
    v->readPixels(img);
    // THE THREE BOXES. The camera and the three objects are fixed, so these are
    // fixed too; part 1 prints them, so a framing change shows up as numbers
    // that stop making sense rather than as a silent pass.
    const Objects o{ boxLuma(img, 112, 60, 144, 92),
                     boxLuma(img, 44, 64, 72, 92),
                     boxLuma(img, 184, 64, 212, 92) };
    std::printf("    [%s] gold %.2f  left %.2f  right %.2f  frame %.2f  meter %.4f\n", label,
                o.gold, o.left, o.right, boxLuma(img, 0, 0, kW, kH), double(v->measuredExposureScale()));
    return o;
}

// ---------------------------------------------------------------------------
// PART 1 — the symptom, and the guard.
// ---------------------------------------------------------------------------

void part1_the_plane()
{
    View *v = gEngine->createOffscreenView("exposure-plane", kW, kH, Colour(0.117f, 0.117f, 0.117f));
    Scene *s = gEngine->createScene("exposure-plane-scene");
    if (!v || !s) { std::printf("FAIL: fixture\n"); ++failures; return; }
    v->setScene(s);

    // A NEW SCENE'S LIGHTS, in the numbers the derivation uses: a sun at
    // intensity 1 (the engine's powerScale = intensity * PI) and an ambient at
    // the default sky's linear radiance. NOT the same code path as the editor's
    // Sky Light — this is the flat `setAmbient` pair, which HlmsPbs delivers
    // through the same 1/PI split rather than through the sky's SH capture — so
    // it is the same illumination arriving by a simpler route, which is all
    // this fixture needs. Nothing here is tuned for the test.
    enginetest::addDirectionalLight(s, Vec3(-0.26f, -0.93f, -0.26f), 1.0f);
    s->setAmbient(Colour(0.117f, 0.117f, 0.117f), Colour(0.117f, 0.117f, 0.117f));

    // The gold cube and two pale companions — SMOKE-41's subjects, in the
    // geometry this fixture has (the engine test helpers build cubes; the
    // assertion is about the METER, not about a sphere).
    const NodeId gold = enginetest::addTestCube(s, Colour(0.92f, 0.74f, 0.22f), 0.0f, 0.28f);
    enginetest::setNodePosition(s, gold, Vec3(0.0f, 0.5f, 0.0f));
    enginetest::setNodeScale(s, gold, Vec3(0.8f, 0.8f, 0.8f));
    const NodeId left = enginetest::addTestCube(s, Colour(0.82f, 0.82f, 0.85f), 0.0f, 0.35f);
    enginetest::setNodePosition(s, left, Vec3(-1.5f, 0.45f, 0.2f));
    enginetest::setNodeScale(s, left, Vec3(0.7f, 0.7f, 0.7f));
    const NodeId right = enginetest::addTestCube(s, Colour(0.55f, 0.55f, 0.58f), 0.0f, 0.5f);
    enginetest::setNodeScale(s, right, Vec3(0.7f, 0.7f, 0.7f));
    enginetest::setNodePosition(s, right, Vec3(1.5f, 0.45f, 0.2f));

    enginetest::testCameraLookAt(v, Vec3(0.0f, 1.5f, 4.2f), Vec3(0.0f, 0.45f, 0.0f));

    // BOTH DESCRIPTIONS THROUGH THE ONE CONVERSION, at the document's own
    // defaults (0 stops, window -3.5..+3.5). Building them by hand is what hid
    // the window's axis for a whole gate: the exposure and the window have
    // different zeros in the renderer, and only iris::lens::toChain knows it.
    const auto describe = [](iris::ExposureMode mode) {
        iris::ExposureDesc d;
        d.mode = mode;
        d.stops = 0.0f;
        d.minStops = -3.5f;
        d.maxStops = 3.5f;
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdr = true;
        iris::lens::toChain(d, fx.exposure, fx.exposureMin, fx.exposureMax, fx.tonemapFixed);
        return fx;
    };
    const PostFxDesc autoFx   = describe(iris::ExposureMode::Auto);
    const PostFxDesc manualFx = describe(iris::ExposureMode::Manual);

    // ---- AUTO, without and with the plane -------------------------------
    v->setPostFx(autoFx);
    const Objects autoBefore = settledObjects(v, "auto, no plane");

    // THE DEFAULT PLANE: a big, pale, matte surface under the objects, filling
    // most of the frame — which is all "a default plane" means to a meter.
    const NodeId plane = enginetest::addTestCube(s, Colour(0.90f, 0.90f, 0.90f), 0.0f, 0.5f);
    enginetest::setNodePosition(s, plane, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(s, plane, Vec3(24.0f, 0.05f, 24.0f));

    const Objects autoAfter = settledObjects(v, "auto, plane");
    const double autoGold = stopsBetween(autoBefore.gold, autoAfter.gold);
    const double autoLeft = stopsBetween(autoBefore.left, autoAfter.left);
    std::printf("    AUTO moved the objects by %.2f / %.2f stops\n", autoGold, autoLeft);
    // THE CONTROL, and it is a control rather than a complaint: if an averaging
    // meter ever stops moving on this fixture, the guard below passes for the
    // wrong reason and nobody would know.
    // (Measured on this fixture at the shipped Auto window: -0.71 and -0.70
    // stops. The editor's own symptom was larger still — -1.3 stops on a real
    // new scene, SMOKE-41 — because a real scene's backdrop is sky rather than
    // this fixture's mid-grey clear. The threshold is 0.5 so the control has
    // headroom without ever reaching the 0.3 the guard allows.)
    CHECK(std::fabs(autoGold) > 0.5 || std::fabs(autoLeft) > 0.5,
          "CONTROL: under AUTO a plane filling the frame moves the objects a long way "
          "(%.2f / %.2f stops) — the symptom this default ends", autoGold, autoLeft);

    // ---- MANUAL, the same two pictures ----------------------------------
    // Remove the plane, re-measure, put it back: the manual arm must be
    // measured on its OWN before-picture, not against the automatic one.
    enginetest::setNodeScale(s, plane, Vec3(0.0001f, 0.0001f, 0.0001f));
    enginetest::setNodePosition(s, plane, Vec3(0.0f, -900.0f, 0.0f));
    v->setPostFx(manualFx);
    const Objects manBefore = settledObjects(v, "manual, no plane");

    enginetest::setNodePosition(s, plane, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(s, plane, Vec3(24.0f, 0.05f, 24.0f));
    const Objects manAfter = settledObjects(v, "manual, plane");

    const double mGold  = stopsBetween(manBefore.gold, manAfter.gold);
    const double mLeft  = stopsBetween(manBefore.left, manAfter.left);
    const double mRight = stopsBetween(manBefore.right, manAfter.right);
    std::printf("    MANUAL moved the objects by %.2f / %.2f / %.2f stops\n", mGold, mLeft, mRight);
    CHECK(std::fabs(mGold) < 0.3,
          "THE GUARD: under MANUAL a plane filling the frame moves the gold cube %.2f stops "
          "(< 0.3)", mGold);
    CHECK(std::fabs(mLeft) < 0.3,
          "THE GUARD: ...and the left object %.2f stops (< 0.3)", mLeft);
    CHECK(std::fabs(mRight) < 0.3,
          "THE GUARD: ...and the right object %.2f stops (< 0.3)", mRight);
    // AND THE DIRECTION IS PHYSICS, not a meter. With a fixed grade, adding a
    // surface can only ADD light to an object (bounce) or leave it alone; it
    // can never take light away. This fixture has no global illumination, so
    // the honest expectation here is "unchanged", and the assertion is written
    // as the INEQUALITY that holds in both worlds — which is the one an editor
    // with GI on has to keep too, and the exact absurdity the averaging meter
    // produced (more light in the room, every object darker).
    CHECK(mGold > -0.05 && mLeft > -0.05 && mRight > -0.05,
          "...and adding a bouncing surface never DARKENS an object (%.2f / %.2f / %.2f stops)",
          mGold, mLeft, mRight);

    gEngine->destroyView(v);
    gEngine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// PART 2 — the resolution: one ExposureDesc, converted once.
// ---------------------------------------------------------------------------

/// The fields of a PostFxDesc this part is about, read after a mirror sync.
struct Resolved { float exposure, lo, hi; bool fixed; };

Resolved resolveThroughMirror(const iris::ScenePtr &doc, View *view, SceneMirror &mirror,
                              const iris::CameraNodePtr &camera)
{
    mirror.sync();
    mirror.applySky(view);
    mirror.applyEnvironment(view);
    if (camera) mirror.applyCamera(camera, view);
    // applyEnvironment layers the DRIVING camera's block over the world's, and
    // the driving camera is what applyCamera just recorded — so the second call
    // is what a real host's next frame does, and it is where a camera override
    // actually lands.
    mirror.applyEnvironment(view);
    const PostFxDesc fx = view->postFx();
    return { fx.exposure, fx.exposureMin, fx.exposureMax, fx.tonemapFixed };
}

void part2_the_resolution()
{
    View *v = gEngine->createOffscreenView("exposure-resolve", 64, 64, Colour(0, 0, 0));
    Scene *target = gEngine->createScene("exposure-resolve-scene");
    if (!v || !target) { std::printf("FAIL: fixture\n"); ++failures; return; }
    v->setScene(target);

    auto doc = iris::Scene::create();
    doc->hdrEnabled = true;
    // A SKY LIGHT, because ambient is a light (SKY_LIGHT_SPEC §2) and a mirror
    // fixture without one pushes no ambient at all. Nothing in part 2 is a
    // pixel, but the sync has to be a real one.
    {
        auto skyLight = iris::LightNode::create();
        skyLight->setLightType(iris::LightType::Sky);
        doc->getRootNode()->addChild(skyLight);
    }
    auto sun = iris::LightNode::create();
    sun->setLightType(iris::LightType::Directional);
    doc->getRootNode()->addChild(sun);
    auto camera = iris::CameraNode::create();
    camera->setLocalPos(iris::Vec3(0.0f, 0.5f, 3.0f));
    camera->lookAt(iris::Vec3(0, 0, 0));
    doc->getRootNode()->addChild(camera);

    SceneMirror mirror(target);
    mirror.setSource(doc);

    const float anchor = iris::lens::exposureAnchorChain();
    const float ln2 = 0.6931472f;

    // MANUAL — the default — is the chain's FIXED form at the anchor.
    {
        const Resolved r = resolveThroughMirror(doc, v, mirror, camera);
        CHECK(r.fixed, "a MANUAL world resolves to the chain's fixed-exposure form");
        CHECK(std::fabs(r.exposure - anchor) < 1e-5f,
              "...at the derived default exposure (%.5f)", double(r.exposure));
        CHECK(std::fabs(r.lo - r.hi) < 1e-6f,
              "...and carries no window, because it measures nothing");
    }

    // A STOP IS A STOP. +1 stop is exactly ln 2 on the chain's axis, whichever
    // mode is in force.
    {
        doc->exposure = 1.0f;
        const Resolved r = resolveThroughMirror(doc, v, mirror, camera);
        CHECK(std::fabs((r.exposure - anchor) - ln2) < 1e-5f,
              "+1 stop is ln 2 on the chain's axis (%.6f)", double(r.exposure - anchor));
        doc->exposure = 0.0f;
    }

    // AUTO is the meter, and its window is converted on the same axis.
    {
        doc->exposureMode = iris::ExposureMode::Auto;
        doc->exposureMin = -2.0f;
        doc->exposureMax = 1.0f;
        const Resolved r = resolveThroughMirror(doc, v, mirror, camera);
        CHECK(!r.fixed, "an AUTO world keeps the luminance meter");
        // THE WINDOW'S OFFSET, NOT ITS WIDTH. The first cut of this lane
        // converted the window with the EXPOSURE's anchor, which is 2.43 stops
        // off the meter's, and a width-only assertion passed it: the shipped
        // -3.5..+3.5 actually bracketed [-5.93, +1.07] around the manual grade
        // and a pinned 0..0 rendered 2.43 stops dark. The anchor the window
        // converts through is the one where the meter agrees with a grey card.
        const float meter = iris::lens::meterGreyCardChain();
        CHECK(std::fabs((r.lo - meter) + 2.0f * ln2) < 1e-4f &&
                  std::fabs((r.hi - meter) - 1.0f * ln2) < 1e-4f,
              "...and its window is stops AROUND THE EXPOSURE, on the meter's axis "
              "(%.4f..%.4f, meter zero %.4f)", double(r.lo), double(r.hi), double(meter));
    }

    // ...AND THE OFFSET IS THE ONE THAT MATTERS, said as a picture: a window
    // pinned at 0..0 asks the meter for exactly the grade Manual renders, so
    // the chain's multiplier must be the manual constant to the last decimal.
    {
        doc->exposureMode = iris::ExposureMode::Auto;
        doc->exposure = 0.75f;
        doc->exposureMin = doc->exposureMax = 0.0f;
        const Resolved r = resolveThroughMirror(doc, v, mirror, camera);
        // The chain computes 1024*e^(E-2) / e^(7.5 - w) with w the clamped
        // window; Manual is e^(E-2)/0.18. Equal exactly when w is the meter's
        // grey-card value, which is what a 0-stop window must resolve to.
        const double autoMul = 1024.0 * std::exp(double(r.exposure) - 2.0) /
                               std::exp(7.5 - double(r.hi));
        const double manualMul = double(iris::lens::exposureMultiplier(r.exposure));
        CHECK(std::fabs(std::log(autoMul / manualMul) / std::log(2.0)) < 1e-4,
              "a 0-stop window asks the meter for the MANUAL grade exactly "
              "(auto %.6f vs manual %.6f)", autoMul, manualMul);
        doc->exposure = 0.0f;
        doc->exposureMin = -3.5f;
        doc->exposureMax = 3.5f;
    }

    // A DRIVING CAMERA SUBSTITUTES ITS OWN BLOCK, in the same unit.
    {
        camera->exposureMode = iris::CameraExposureMode::Manual;
        camera->exposure = -2.0f;
        const Resolved r = resolveThroughMirror(doc, v, mirror, camera);
        CHECK(r.fixed, "a MANUAL camera pins the grade even in an AUTO world");
        CHECK(std::fabs((r.exposure - anchor) + 2.0f * ln2) < 1e-4f,
              "...at its own stops (%.4f)", double(r.exposure - anchor));
    }
    // ...and Inherit substitutes nothing at all.
    {
        camera->exposureMode = iris::CameraExposureMode::Inherit;
        doc->exposureMode = iris::ExposureMode::Manual;
        doc->exposure = 0.75f;
        const Resolved r = resolveThroughMirror(doc, v, mirror, camera);
        CHECK(r.fixed && std::fabs((r.exposure - anchor) - 0.75f * ln2) < 1e-4f,
              "an INHERIT camera leaves the world's exposure exactly as it is (%.4f)",
              double(r.exposure - anchor));
    }

    mirror.setSource(iris::ScenePtr());
    gEngine->destroyView(v);
    gEngine->destroyScene(target);
}

// ---------------------------------------------------------------------------
// PART 3 — the defaults an absent key falls back to.
// ---------------------------------------------------------------------------

void part3_the_defaults()
{
    auto s = iris::Scene::create();
    CHECK(s->exposureMode == iris::ExposureMode::Manual,
          "A NEW SCENE IS MANUAL — the editor's default is a number, not a meter");
    CHECK(std::fabs(double(s->exposure)) < 1e-6,
          "...at zero stops, which IS the derived default grade (%.4f)", double(s->exposure));
    CHECK(std::fabs(double(s->exposureMin) + 3.5) < 1e-6 &&
              std::fabs(double(s->exposureMax) - 3.5) < 1e-6,
          "...with the same automatic window a camera is born with (%.2f..%.2f stops)",
          double(s->exposureMin), double(s->exposureMax));
    CHECK(s->exposureMin <= s->exposureMax, "...ordered, which every writer maintains");

    // THE ANCHOR IS THE DERIVATION. Zero stops is not a stored number: it is
    // what the default template's lights come out at.
    CHECK(std::fabs(double(iris::lens::exposureStopsToChain(0.0f)) -
                    double(iris::lens::defaultExposureChain())) < 1e-6,
          "zero stops IS defaultExposureChain()");
    CHECK(std::fabs(double(iris::lens::exposureChainToStops(
                        iris::lens::defaultExposureChain()))) < 1e-5,
          "...and back again (exact inverses)");

    // A camera's block is the same unit and the same window.
    auto cam = iris::CameraNode::create();
    CHECK(std::fabs(double(cam->exposureMin) - double(s->exposureMin)) < 1e-6 &&
              std::fabs(double(cam->exposureMax) - double(s->exposureMax)) < 1e-6,
          "a camera and the world are born with the SAME automatic window — one quantity, one "
          "default");

    // The mode names round-trip, which is the reader's and the writer's whole
    // contract for this field.
    bool ok = false;
    CHECK(iris::exposureModeFromName("manual", &ok) == iris::ExposureMode::Manual && ok,
          "\"manual\" reads back as Manual");
    CHECK(iris::exposureModeFromName("auto", &ok) == iris::ExposureMode::Auto && ok,
          "\"auto\" reads back as Auto");
    iris::exposureModeFromName("nonsense", &ok);
    CHECK(!ok, "...and an unknown spelling is REFUSED rather than guessed");
    CHECK(std::string(iris::exposureModeName(iris::ExposureMode::Manual)) == "manual" &&
              std::string(iris::exposureModeName(iris::ExposureMode::Auto)) == "auto",
          "and the names the writer emits are those two");
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_exposure_plane-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: Engine::create — %s\n", err.c_str()); return 1; }
    gEngine = engine.get();

    std::printf("\n-- 1  a plane in a new scene, under both meters --\n"); part1_the_plane();
    std::printf("\n-- 2  one ExposureDesc, converted once --\n");          part2_the_resolution();
    std::printf("\n-- 3  the defaults an absent key falls back to --\n");  part3_the_defaults();

    gEngine = nullptr;
    engine.reset();
    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
