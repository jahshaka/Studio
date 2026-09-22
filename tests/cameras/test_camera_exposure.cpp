// PER-CAMERA EXPOSURE AND PER-CAMERA POST OVERRIDES, in pixels
// (CAMERA_LENS_SPEC §4/§5, P3 + P4).
//
// This suite is the headline proof of the program's one new engine mechanism
// and of the two PRE-EXISTING DEFECTS it fixes. Everything here renders real
// frames through the real backend; the assertions are pixels and byte
// fingerprints, never descriptions.
//
// PART A — THE MECHANISM (engine only, no document).
//   A1 two on-screen-shaped views, one frame, TWO DIFFERENT EXPOSURES. Before
//      the per-view listener the post chain's tuning was a process-wide
//      MaterialManager write pushed from "the first enabled view whose chain
//      has effects, then break" (POST_CHAIN_SPEC §7.4), so the second view
//      rendered with the first's exposure — a defect, recorded as such. Two
//      views that ask for the same thing must still agree, which is the control
//      that keeps A1 from passing for the wrong reason.
//   A2 the SSAO PROJECTION defect: updateSsao pushed ONE camera's projection
//      process-wide, so a second view's ambient occlusion marched the first
//      view's frustum. Asserted the only way that is honest: a view's own AO
//      frame must not change when a view with a wildly different frustum is
//      enabled in front of it.
//   A3 the camera-cut re-seed (R4): resetExposureHistory() lands the new grade
//      on the NEXT frame instead of fading to it over ~1.5 s.
//
// PART B — THE RESOLUTION (document + mirror + engine).
//   B1 a camera's exposure block reaches the view it drives, in stops, and one
//      stop is exactly one doubling of the chain's exposure term.
//   B2 per-camera BLOOM, both ways round: bloom off on a camera in a bloomy
//      world, and bloom on a camera in a clean one (the owner's ask).
//   B3 inherit is inherit: a camera that overrides nothing leaves the world's
//      description bit-for-bit alone.
//   B4 THE DETERMINISM NEGATIVE. A plain offscreen view — the shape every
//      thumbnail, preview and pixel suite has — renders BYTE-IDENTICAL whether
//      or not the camera driving it overrides everything in sight. This is the
//      law the whole feature is built under and the one assertion that would
//      catch it being broken.
//
// Runs with QT_QPA_PLATFORM=offscreen and a reachable DISPLAY (Vulkan).

#include <QGuiApplication>

#include "bridge/previewmesh.h"
#include <QVariantList>

#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
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

/// Mean luminance of a frame, 0..255. Every exposure assertion is one number.
double luma(const Image &img)
{
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        sum += 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
        ++n;
    }
    return n ? sum / double(n) : 0.0;
}

/// FNV-1a over the raw RGBA — the byte fingerprint the determinism negatives
/// compare. Same shape tests/engine uses.
unsigned long long pixelHash(const Image &img)
{
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char v : img.rgba) { h ^= v; h *= 1099511628211ull; }
    return h;
}

void frames(int n) { for (int i = 0; i < n; ++i) gEngine->renderOneFrame(); }

// ---------------------------------------------------------------------------
// SETTLING, AND WHY IT IS COUNTED IN FRAMES
//
// This used to be `settle(double seconds)`: a wall-clock loop of
// renderOneFrame() + an 8 ms sleep, on the belief that "HDR adaptation is
// charged in wall clock". IT IS NOT, and has not been since the A4.2
// fixed-clock merge (2026-09-10). The adaptation step is
// HDR/DownScale03_SumLumEnd's `timeSinceLast`, declared
// `param_named_auto timeSinceLast frame_time 1`, and Ogre's frame_time comes
// from ControllerManager — which this engine puts in FRAME-DELAY mode at boot,
// at a fixed Engine::kDefaultFrameDelta = 1/60 s (OgreEngine.cpp:154). Every
// rendered frame therefore advances the exposure history by exactly 1/60 s,
// and NO rendered frame advances it by wall clock.
//
// So the old helper measured the wrong axis: in 1.5 wall-clock seconds a quiet
// box got ~150 frames (2.5 s of adaptation) and a box under a -j4 gate got a
// fraction of that. The reference shot in part B is taken first, before the
// experiments, and it was the one that suffered — the suite read an
// UNCONVERGED reference against a converged comparison and failed its own 1.5
// tolerance. Three gates saw it (2026-09-10, 2026-09-12 at 75.40 vs 78.20,
// 2026-09-13 at 78.14 vs 76.39) and every solo retry passed. Nothing was ever
// wrong with the engine; the test sampled too early.
//
// Two helpers replace it, and neither can be stretched by load:
//
//   settle(n)              render exactly n frames = exactly n/60 s of
//                          adaptation, on any box, at any load.
//   settledLuma(view, ...) render until the PICTURE STOPS MOVING and return
//                          its mean luma — "the measured value holding still",
//                          which is the only definition of converged that does
//                          not encode a guess about the adaptation rate.
//
// The tolerances are untouched. A tolerance is the assertion.

/// n frames = n/60 s of adaptation. Deterministic, and load-independent.
void settle(int n) { frames(n); }

/// A generous cap on convergence. At 1/60 s per frame this is 10 s of
/// adaptation against a rate of ~75%/s — thirty thousand times the distance
/// any of these shots has to travel. It exists so a broken chain fails on an
/// assertion rather than hanging.
constexpr int kMaxSettleFrames = 600;
/// Frames between two luma readbacks while converging.
constexpr int kSettleStep = 10;
/// Two consecutive readbacks this close (out of 255) are "still". The
/// assertions below are made at tolerances of 1.5 and 8.0 luma levels, so a
/// twentieth of a level is well inside the noise floor of every one of them.
constexpr double kStillEpsilon = 0.05;

/// Renders `view` until its mean luminance holds still, and returns it.
/// Optionally hands back the frame it settled on and that frame's hash.
double settledLuma(View *view, unsigned long long *hashOut = nullptr,
                   const char *label = nullptr)
{
    Image img;
    view->readPixels(img);
    double previous = luma(img);
    int rendered = 0;
    while (rendered < kMaxSettleFrames) {
        frames(kSettleStep);
        rendered += kSettleStep;
        view->readPixels(img);
        const double now = luma(img);
        if (std::fabs(now - previous) < kStillEpsilon) {
            if (label)
                std::printf("    [%s settled after %d frames (%.2f s of adaptation) at %.2f]\n",
                            label, rendered, rendered / 60.0, now);
            if (hashOut) *hashOut = pixelHash(img);
            return now;
        }
        previous = now;
    }
    std::printf("    [WARNING: %s did not settle in %d frames — last %.2f]\n",
                label ? label : "view", kMaxSettleFrames, previous);
    if (hashOut) *hashOut = pixelHash(img);
    return previous;
}

/// A lit scene with a bright, blooming highlight in it. Everything that follows
/// is measured on this one picture.
void populate(Scene *s)
{
    s->setAmbient(Colour(0.30f, 0.30f, 0.34f), Colour(0.22f, 0.22f, 0.26f));
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -0.8f, -0.45f), 6.0f);
    NodeId cube = enginetest::addTestCube(s, Colour(0.92f, 0.88f, 0.80f), 0.0f, 0.30f);
    enginetest::setNodeScale(s, cube, Vec3(1.15f, 1.15f, 1.15f));
}

/// A MANUALLY exposed HDR description at a given chain exposure.
PostFxDesc pinned(float chainExposure, bool bloom = false, float bloomThreshold = 3.0f)
{
    PostFxDesc fx;
    fx.allowOffscreen = true;   // the ONLY way an offscreen view gets a chain
    fx.hdr = true;
    fx.exposure = chainExposure;
    // MANUAL EXPOSURE IS THE CHAIN'S FIXED FORM (EXPOSURE-1): the luminance
    // ladder is replaced by a clear to e^(E-2)/0.18, so the grade is a number
    // rather than an average that drifts with what is on screen — and it is
    // exact on the FIRST frame instead of a second of adaptation away. The old
    // spelling (min == max, pinning the shader's clamp to a derived constant)
    // is deleted; this is what the mirror now pushes for Manual.
    fx.tonemapFixed = true;
    fx.bloom = bloom;
    fx.bloomThreshold = bloomThreshold;
    return fx;
}

/// AN AUTOMATIC HDR DESCRIPTION WHOSE METER IS CLAMPED TO ONE VALUE.
///
/// A3's subject is the adaptation HISTORY — the temporal filter a camera cut
/// re-seeds — and MANUAL exposure has no history at all: it is the chain's
/// fixed form, a clear to a constant, which lands instantly whether or not
/// anybody re-seeds it (measured: 130.96 both ways). So that case needs the
/// METER, and it needs the meter to converge somewhere PREDICTABLE, or it would
/// be measuring the fixture's content instead.
///
/// `7.5 - ln(1024 * 0.18)` is the clamp that makes the shader's multiplier
/// exactly `e^(E-2)/0.18` whatever is on screen. EXPOSURE-1 deleted this as the
/// PRODUCT's way of spelling manual exposure (it needed a derived constant to
/// avoid counting the exposure twice, and it arrived over about a second); it
/// is still the right arithmetic for pinning a meter in a fixture, and it is
/// spelled out here rather than shared, because nothing ships it any more.
PostFxDesc clampedAuto(float chainExposure)
{
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.hdr = true;
    fx.exposure = chainExposure;
    fx.exposureMin = fx.exposureMax = 7.5f - float(std::log(1024.0 * 0.18));
    return fx;
}

// ---------------------------------------------------------------------------
// PART A — the mechanism, engine only.
// ---------------------------------------------------------------------------

void a1_two_views_two_exposures()
{
    View *a = gEngine->createOffscreenView("expA", 96, 96, Colour(0.10f, 0.10f, 0.12f));
    View *b = gEngine->createOffscreenView("expB", 96, 96, Colour(0.10f, 0.10f, 0.12f));
    Scene *s = gEngine->createScene("exp-scene");
    if (!a || !b || !s) { std::printf("FAIL: fixture\n"); ++failures; return; }
    a->setScene(s); b->setScene(s);
    populate(s);
    enginetest::testCameraLookAt(a, Vec3(2.2f, 1.6f, 2.6f), Vec3(0, 0, 0));
    enginetest::testCameraLookAt(b, Vec3(2.2f, 1.6f, 2.6f), Vec3(0, 0, 0));

    // THE CONTROL. Same settings, same scene, same camera: if these two ever
    // disagree, A1's real assertion below means nothing.
    a->setPostFx(pinned(0.6f));
    b->setPostFx(pinned(0.6f));
    Image ib;
    // CONVERGED, not "1.5 seconds later": settledLuma renders until the
    // picture holds still. One engine renders both views every frame, so
    // settling A settles B — they are read at the same frame count, which is
    // the only way a control means anything.
    const double same0 = settledLuma(a, nullptr, "A1 control A");
    b->readPixels(ib);
    const double same1 = luma(ib);
    std::printf("    control: A %.2f  B %.2f\n", same0, same1);
    CHECK(std::fabs(same0 - same1) < 1.0,
          "CONTROL: two views asking for the SAME exposure agree (%.2f vs %.2f)", same0, same1);

    // THE HEADLINE. Two different exposures, one engine, one frame each. Before
    // the per-view listener the second view rendered with the first's exposure.
    a->setPostFx(pinned(-1.4f));    // two stops down
    b->setPostFx(pinned(2.0f));     // two stops up
    const double dark = settledLuma(a, nullptr, "A1 dark A");
    b->readPixels(ib);
    const double bright = luma(ib);
    std::printf("    per-view: A (E -1.4) %.2f  B (E +2.0) %.2f\n", dark, bright);
    CHECK(bright > dark + 8.0,
          "PER-VIEW EXPOSURE: two views render their OWN exposure in the same frame "
          "(%.2f vs %.2f)", dark, bright);
    // ...and the ORDER does not decide it: swapping the two descriptions swaps
    // the two pictures, which is what "each view owns its own" means and what a
    // first-view-wins global could never do.
    a->setPostFx(pinned(2.0f));
    b->setPostFx(pinned(-1.4f));
    const double swappedA = settledLuma(a, nullptr, "A1 swapped A");
    b->readPixels(ib);
    const double swappedB = luma(ib);
    std::printf("    swapped:  A (E +2.0) %.2f  B (E -1.4) %.2f\n", swappedA, swappedB);
    CHECK(swappedA > swappedB + 8.0,
          "PER-VIEW EXPOSURE: swapping the two descriptions swaps the two pictures "
          "(%.2f vs %.2f) — the FIRST view does not own the globals any more",
          swappedA, swappedB);

    a->setPostFx(PostFxDesc()); b->setPostFx(PostFxDesc());
    gEngine->destroyView(a); gEngine->destroyView(b); gEngine->destroyScene(s);
}

void a2_ssao_projection_is_per_view()
{
    Scene *s = gEngine->createScene("ao-scene");
    View *b = gEngine->createOffscreenView("aoB", 96, 96, Colour(0.10f, 0.10f, 0.12f));
    if (!s || !b) { std::printf("FAIL: fixture\n"); ++failures; return; }
    b->setScene(s);
    populate(s);
    enginetest::testCameraLookAt(b, Vec3(2.2f, 1.6f, 2.6f), Vec3(0, 0, 0));

    PostFxDesc ao;
    ao.allowOffscreen = true;
    ao.ssao = true;
    ao.ssaoPower = 2.0f;
    ao.ssaoRadius = 1.5f;
    b->setPostFx(ao);
    frames(4);
    Image alone; b->readPixels(alone);
    const unsigned long long aloneHash = pixelHash(alone);
    std::printf("    B alone: luma %.2f\n", luma(alone));

    // Now put a view with a COMPLETELY different frustum in front of it — a
    // long lens from far away, which is the projection the old global path
    // would have pushed for both. B must not move.
    View *a = gEngine->createOffscreenView("aoA", 96, 96, Colour(0.10f, 0.10f, 0.12f));
    if (!a) { std::printf("FAIL: second view\n"); ++failures; return; }
    a->setScene(s);
    // Far away down a long lens, with near and far planes nothing like B's:
    // this is precisely the projection the old process-global updateSsao would
    // have marched B's depth buffer with.
    {
        CameraDesc c;
        c.position = Vec3(0.0f, 0.0f, 60.0f);   // identity orientation: down -Z
        c.fovDegrees = 6.0f;
        c.nearClip = 5.0f;
        c.farClip = 400.0f;
        a->setCamera(c);
    }
    a->setPostFx(ao);
    frames(4);
    Image withOther; b->readPixels(withOther);
    const unsigned long long withHash = pixelHash(withOther);
    std::printf("    B with a far/narrow view enabled: luma %.2f\n", luma(withOther));
    CHECK(aloneHash == withHash,
          "SSAO PROJECTION IS PER VIEW: a second view's frustum does not reach into this "
          "view's ambient occlusion (hash %llu vs %llu)", aloneHash, withHash);

    a->setPostFx(PostFxDesc()); b->setPostFx(PostFxDesc());
    gEngine->destroyView(a); gEngine->destroyView(b); gEngine->destroyScene(s);
}

void a3_cut_reseeds_the_exposure_history()
{
    View *v = gEngine->createOffscreenView("cut", 96, 96, Colour(0.10f, 0.10f, 0.12f));
    Scene *s = gEngine->createScene("cut-scene");
    if (!v || !s) { std::printf("FAIL: fixture\n"); ++failures; return; }
    v->setScene(s);
    populate(s);
    enginetest::testCameraLookAt(v, Vec3(2.2f, 1.6f, 2.6f), Vec3(0, 0, 0));

    // CONVERGED, both times: "the same starting point" is what makes the two
    // two-frame shots below comparable, and a wall-clock settle could not
    // promise it under load.
    v->setPostFx(clampedAuto(-1.4f));
    const double darkLuma = settledLuma(v, nullptr, "A3 dark");

    // THE CUT, without the hook: a new exposure, two frames. The history still
    // holds the old grade, so almost nothing has moved yet.
    v->setPostFx(clampedAuto(2.0f));
    frames(2);
    Image fading; v->readPixels(fading);
    const double fadingLuma = luma(fading);

    // ...and the same cut WITH the hook, from the same starting point.
    v->setPostFx(clampedAuto(-1.4f));
    const double darkAgain = settledLuma(v, nullptr, "A3 dark again");
    CHECK(std::fabs(darkAgain - darkLuma) < 0.5,
          "CONTROL: the second run-up reaches the SAME dark grade as the first "
          "(%.2f vs %.2f) — the two cuts below start from one place",
          darkAgain, darkLuma);
    v->setPostFx(clampedAuto(2.0f));
    v->resetExposureHistory();
    frames(2);
    Image cut; v->readPixels(cut);
    const double cutLuma = luma(cut);

    // Where it ends up if you simply wait.
    const double settledGrade = settledLuma(v, nullptr, "A3 settled");

    std::printf("    dark %.2f -> two frames later %.2f (fade) / %.2f (re-seeded), settles at %.2f\n",
                darkLuma, fadingLuma, cutLuma, settledGrade);
    CHECK(cutLuma > fadingLuma + 4.0,
          "CAMERA CUT: re-seeding lands the new grade immediately instead of fading into it "
          "(%.2f vs %.2f two frames after the cut)", cutLuma, fadingLuma);
    CHECK(std::fabs(cutLuma - settledGrade) < std::fabs(fadingLuma - settledGrade),
          "CAMERA CUT: the re-seeded frame is nearer the settled grade than the fading one "
          "(|%.2f-%.2f| vs |%.2f-%.2f|)", cutLuma, settledGrade, fadingLuma, settledGrade);

    v->setPostFx(PostFxDesc());
    gEngine->destroyView(v); gEngine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// PART B — the resolution, through the document and the mirror.
// ---------------------------------------------------------------------------

/// The document half of the fixture: a lit cube, a sun, and a scene camera.
struct Doc {
    iris::ScenePtr scene;
    iris::CameraNodePtr camera;

    Doc()
    {
        scene = iris::Scene::create();
        auto meshNode = iris::MeshNode::create();
        meshNode->setName("cube");
        meshNode->setMesh(previewmesh::load(":assets/models/cube.obj"));
        const float r = meshNode->getMeshRadius();
        meshNode->setLocalScale(iris::Vec3(1.0f / (r > 0.0f ? r : 1.0f),
                                           1.0f / (r > 0.0f ? r : 1.0f),
                                           1.0f / (r > 0.0f ? r : 1.0f)));
        meshNode->setMaterial(iris::PbrMaterial::create());
        scene->getRootNode()->addChild(meshNode);

        // RE-BASELINED for SKY_LIGHT_SPEC.md §2 and §4, and the two halves are
        // separate numbers on purpose.
        //
        // THE BACKDROP. Most of this frame is sky, and a picked colour is
        // decoded now: the document's default grey used to reach the frame raw
        // at 0.376 of radiance and would arrive at 0.117, taking the whole
        // picture from 78 to 13 and the bloom's dynamic range with it (B2
        // measures bloom on vs off against a 2.0 threshold). 163 grey decodes to
        // 0.376 — the same backdrop radiance this fixture always had, said in
        // the colour space the renderer now speaks.
        scene->skyColor = QColor(163, 163, 163);
        // THE AMBIENT. It is a LIGHT now, and without one this fixture would
        // have none at all. 0.31 over that 0.376 sky is 0.117 of ambient
        // radiance — the level the flat 96-grey ambient it replaces carried
        // (0.376 through HlmsPbs' 1/pi split).
        auto skyLight = iris::LightNode::create();
        skyLight->setName("Sky Light");
        skyLight->setLightType(iris::LightType::Sky);
        skyLight->intensity = 0.31f;
        scene->getRootNode()->addChild(skyLight);

        auto light = iris::LightNode::create();
        light->setName("sun");
        light->intensity = 6.0f;
        light->setLocalRot(iris::Quat::fromEulerAngles(-50.0f, 30.0f, 0.0f));
        light->setLocalPos(iris::Vec3(0.0f, 0.0f, -60.0f));
        scene->getRootNode()->addChild(light);

        camera = iris::CameraNode::create();
        camera->setName("Shot A");
        camera->bodyVisible = false;          // no helper wires in the picture
        camera->setLocalPos(iris::Vec3(0.0f, 0.6f, 3.4f));
        camera->lookAt(iris::Vec3(0, 0, 0));
        scene->getRootNode()->addChild(camera);

        // A world with a graded, blooming look — the BASE every camera below
        // either inherits or overrides.
        scene->hdrEnabled = true;
        // MANUAL, so every number this suite prints is a grade and not a
        // measurement that drifts with what happens to be on screen. The world
        // and a camera in Manual reach the identical constant, which is what
        // makes the inherit assertions meaningful.
        scene->exposureMode = iris::ExposureMode::Manual;
        // STOPS (EXPOSURE-1): a bright frame, with highlights to bloom.
        scene->exposure = iris::lens::exposureChainToStops(1.5f);
        scene->bloomEnabled = true;
        scene->bloomThreshold = 0.02f;        // everything blooms
    }
};

/// Renders `view` the way a screenshot with postFx:true does: the world's
/// description, then the driving camera's substitution, then the deliberate
/// offscreen opt-in — in that order, because that IS the order the app uses.
/// `fixedFrames` = 0 (the default) settles to CONVERGENCE — every graded shot
/// in this part is compared against another graded shot, and the whole suite's
/// oldest flake was a reference sampled before it had arrived. A non-zero value
/// renders exactly that many frames instead, for the one shot that must NOT
/// converge: B4's plain view has no chain to converge, and its two hashes have
/// to be taken at an identical frame count to be compared byte for byte.
double shoot(SceneMirror &mirror, View *view, const iris::CameraNodePtr &camera,
             bool optIn, unsigned long long *hashOut = nullptr, int fixedFrames = 0,
             const char *label = nullptr)
{
    mirror.sync();
    // applySky BEFORE applyEnvironment, as every real host does. It is not
    // optional any more: the ambient is the SKY LIGHT reading the sky's own
    // integral (SKY_LIGHT_SPEC.md §2), and the integral is applySky's — a
    // fixture that skipped it used to get the flat World colour and now gets no
    // ambient at all.
    mirror.applySky(view);
    mirror.applyEnvironment(view);
    mirror.applyCamera(camera, view);
    // ...AND THE ENVIRONMENT AGAIN, ONE FRAME LATER (SKY-GPU). The sky's own
    // light is the engine's now: it captures the sky it drew into a cubemap
    // inside the next rendered frame and integrates THAT, so the ambient a host
    // reads is the sky of the frame before — exactly like the IBL convolution,
    // which has always landed a frame late. Every real host calls
    // applyEnvironment once per frame and never notices; this fixture pushes
    // once and then renders hundreds of frames, so without these two lines the
    // FIRST shot of the run is the only one taken before the sky's light
    // arrives, and every later one is 2.5x brighter than it.
    gEngine->renderOneFrame();
    mirror.applyEnvironment(view);
    if (optIn) {
        PostFxDesc fx = view->postFx();
        fx.allowOffscreen = true;
        view->setPostFx(fx);
    }
    if (fixedFrames <= 0)
        return settledLuma(view, hashOut, label);
    settle(fixedFrames);
    Image img;
    view->readPixels(img);
    if (hashOut) *hashOut = pixelHash(img);
    return luma(img);
}

void partB()
{
    // THE ENGINE SCENE FIRST, then the document. A document node is an engine
    // node (SCENEGRAPH_SPEC D2): a document built while the only engine scenes
    // in the process are being destroyed and recreated lands on a staging scene
    // that is about to go stale, and iris::Scene says so, loudly, at bind time.
    Scene *target = gEngine->createScene("doc-scene");
    View *view = gEngine->createOffscreenView("docView", 96, 96, Colour(0.10f, 0.10f, 0.12f));
    if (!target || !view) { std::printf("FAIL: fixture\n"); ++failures; return; }
    view->setScene(target);
    Doc doc;
    SceneMirror mirror(target);
    mirror.setSource(doc.scene);

    // ---- B3 first: INHERIT changes nothing --------------------------------
    unsigned long long inheritHash = 0;
    const double inherited = shoot(mirror, view, doc.camera, true, &inheritHash, 0,
                                   "B3 reference (inherit)");
    std::printf("    world grade through an inheriting camera: %.2f\n", inherited);

    // ---- B1: the camera's own exposure, in STOPS --------------------------
    doc.camera->exposureMode = iris::CameraExposureMode::Manual;
    doc.camera->exposure = -2.0f;                       // two stops under
    const double under = shoot(mirror, view, doc.camera, true);
    doc.camera->exposure = 2.0f;                        // two stops over
    const double over = shoot(mirror, view, doc.camera, true);
    std::printf("    camera exposure: -2 stops %.2f  +2 stops %.2f\n", under, over);
    CHECK(over > under + 8.0,
          "PER-CAMERA EXPOSURE: the driving camera's stops reach the view (%.2f vs %.2f)",
          under, over);

    // The conversion itself, asserted against the arithmetic rather than the
    // picture: one stop IS ln 2 on the chain's axis, and the manual pin does
    // not smuggle the exposure into the clamp as well.
    {
        const float zero = iris::lens::exposureStopsToChain(0.0f);
        const float one  = iris::lens::exposureStopsToChain(1.0f);
        CHECK(std::fabs((one - zero) - 0.6931472f) < 1e-5f,
              "one stop is ln 2 on the chain's exposure axis (%.7f)", double(one - zero));
        // ZERO STOPS IS THE DERIVED DEFAULT GRADE (EXPOSURE-1), not a tuned
        // number. THE WHOLE DERIVATION, recomputed here by hand from the two
        // things it depends on — the default template's lights and the SHIPPED
        // film curve — so that changing either without re-deriving fails here.
        {
            const double pi = 3.14159265358979323846;
            // (1) the film curve, inverted: the tonemapper input that makes the
            // display emit 18 %. FinalToneMapping_ps.glsl's second constant set
            // plus its hand grade tail, spelled out again rather than shared.
            const double A = 0.22, B = 0.30, C = 0.10, D = 0.20, Ee = 0.01, Ff = 0.30, W = 11.2;
            auto hable = [&](double x) {
                return ((x * (A * x + C * B) + D * Ee) / (x * (A * x + B) + D * Ff)) - Ee / Ff;
            };
            const double hw = hable(W);
            const double h = ((0.18 - 0.61) / 1.25 + 0.5) * hw;
            const double k = h + Ee / Ff;
            const double qa = A * (1.0 - k), qb = B * (C - k), qc = D * (Ee - k * Ff);
            const double xStar = (-qb + std::sqrt(qb * qb - 4.0 * qa * qc)) / (2.0 * qa);
            CHECK(std::fabs(double(iris::lens::greyCardFilmInput()) - xStar) < 1e-6,
                  "the film curve inverts to x* = %.6f (the tonemapper input that displays as "
                  "18%% grey); the shipped constant is %.6f", xStar,
                  double(iris::lens::greyCardFilmInput()));
            // ...and the inversion is really an inversion: put x* through the
            // curve and the display gets 0.18.
            const double back = (hable(xStar) / hw - 0.5) * 1.25 + 0.61;
            CHECK(std::fabs(back - 0.18) < 1e-6, "...and x* develops to 0.18 (%.6f)", back);

            // (2) the default template's lights: a sun and a Sky Light at
            // intensity 1 over a 96-grey sky.
            const double skySrgb = 96.0 / 255.0;
            const double skyLin = std::pow((skySrgb + 0.055) / 1.055, 2.4);
            const double eKey = pi * (1.0 + skyLin);
            CHECK(std::fabs(double(iris::lens::keyIrradiance(1.0f, 1.0f, float(skyLin))) - eKey)
                      < 1e-4,
                  "keyIrradiance(sun 1, sky light 1, sky %.5f) = %.5f", skyLin, eKey);

            // (3) the exposure that develops an 18 % grey card under it at x*.
            const double byHand = 2.0 + std::log(xStar * pi / eKey);
            CHECK(std::fabs(double(zero) - byHand) < 1e-5,
                  "zero stops is the DERIVED default grade (%.6f vs the hand computation %.6f)",
                  double(zero), byHand);
            CHECK(std::fabs(double(iris::lens::exposureForKeyIrradiance(float(eKey))) - byHand)
                      < 1e-5,
                  "the derivation and the default agree");
            // AND IT IS A METER: doubling the light is exactly one stop down.
            const double half = double(iris::lens::exposureForKeyIrradiance(float(2.0 * eKey)));
            CHECK(std::fabs((byHand - half) - 0.6931472) < 1e-5,
                  "twice the light is one stop down (%.6f)", byHand - half);
            // THE END-TO-END CLAIM, in one line: a grey card under the default
            // scene's own lights displays at 18 %.
            const double greyRadiance = 0.18 * eKey / pi;
            const double displayed =
                (hable(greyRadiance * double(iris::lens::exposureMultiplier(zero))) / hw - 0.5) *
                    1.25 + 0.61;
            CHECK(std::fabs(displayed - 0.18) < 1e-4,
                  "an 18%% grey card under the default lights displays at 18%% (%.5f)", displayed);
        }
        // MANUAL IS THE FIXED-TONEMAP CONSTANT — not "agrees with it", IS it, so
        // a manually exposed viewport and a thumbnail of the same world grade
        // identically by construction rather than through a derived pin.
        {
            iris::ExposureDesc d;
            d.mode = iris::ExposureMode::Manual;
            d.stops = 0.0f;
            float e = 0.0f, lo = 0.0f, hi = 0.0f;
            bool fixed = false;
            iris::lens::toChain(d, e, lo, hi, fixed);
            CHECK(fixed, "Manual resolves to the chain's FIXED-exposure form");
            CHECK(std::fabs(double(e) - double(zero)) < 1e-6, "...at zero stops' exposure");
            d.mode = iris::ExposureMode::Auto;
            d.minStops = -1.0f; d.maxStops = 1.0f;
            iris::lens::toChain(d, e, lo, hi, fixed);
            CHECK(!fixed, "Auto does not");
            CHECK(std::fabs(double(hi - lo) - 2.0 * 0.6931472) < 1e-5,
                  "...and its window is two stops wide on the chain's axis (%.6f)",
                  double(hi - lo));
        }
    }

    // ---- B2: per-camera BLOOM, both ways round ----------------------------
    // A camera pinned at the SAME grade the world is, so the only thing that
    // moves between the two measurements below is the bloom.
    doc.camera->exposureMode = iris::CameraExposureMode::Manual;
    doc.camera->exposure = doc.scene->exposure;   // STOPS, both of them now
    doc.camera->postOverrides = QJsonObject();
    const double bloomyWorld = shoot(mirror, view, doc.camera, true);
    CHECK(doc.camera->setPostOverride(QStringLiteral("bloom"), false),
          "bloom is an override key the document accepts");
    const double bloomOffCamera = shoot(mirror, view, doc.camera, true);
    std::printf("    bloomy world %.2f -> camera with bloom OFF %.2f\n",
                bloomyWorld, bloomOffCamera);
    CHECK(bloomyWorld > bloomOffCamera + 2.0,
          "PER-CAMERA BLOOM: a camera can switch the world's bloom OFF (%.2f -> %.2f)",
          bloomyWorld, bloomOffCamera);

    // ...and the other way: a clean world, one blooming camera.
    doc.scene->bloomEnabled = false;
    doc.camera->clearPostOverride(QStringLiteral("bloom"));
    const double cleanWorld = shoot(mirror, view, doc.camera, true);
    doc.camera->setPostOverride(QStringLiteral("bloom"), true);
    doc.camera->setPostOverride(QStringLiteral("bloomThreshold"), 0.02);
    const double bloomOnCamera = shoot(mirror, view, doc.camera, true);
    std::printf("    clean world %.2f -> camera with bloom ON %.2f\n",
                cleanWorld, bloomOnCamera);
    CHECK(bloomOnCamera > cleanWorld + 2.0,
          "PER-CAMERA BLOOM: a camera can switch bloom ON in a world without it "
          "(%.2f -> %.2f)", cleanWorld, bloomOnCamera);

    // ---- B2b: AN OVERRIDE MUST NOT CHURN THE COMPOSITOR -------------------
    // An enable-flag difference IS a workspace rebuild, and hosts call
    // applyEnvironment and then applyCamera EVERY FRAME. If only one of the two
    // substituted the camera's flags, the chain would be rebuilt twice a frame
    // for as long as an overriding camera was driving — invisible in a pixel
    // assertion and ruinous in the editor. Found while building this phase;
    // this is the assertion that keeps it fixed.
    {
        doc.camera->setPostOverride(QStringLiteral("bloom"), false);
        // Settle first: turning the override ON is legitimately one rebuild.
        for (int i = 0; i < 3; ++i) {
            mirror.sync();
            mirror.applyEnvironment(view);
            mirror.applyCamera(doc.camera, view);
            gEngine->renderOneFrame();
        }
        const unsigned before = view->workspaceGeneration();
        for (int i = 0; i < 10; ++i) {
            mirror.sync();
            mirror.applyEnvironment(view);
            mirror.applyCamera(doc.camera, view);
            gEngine->renderOneFrame();
        }
        const unsigned after = view->workspaceGeneration();
        std::printf("    workspace generation over 10 steady frames: %u -> %u\n", before, after);
        CHECK(before == after,
              "STEADY STATE: a camera whose override changes an ENABLE FLAG rebuilds the "
              "compositor ZERO times per frame (%u -> %u)", before, after);
        doc.camera->clearPostOverride(QStringLiteral("bloom"));
    }

    // ---- B3 again, now that overrides exist: clearing goes back -----------
    doc.scene->bloomEnabled = true;
    doc.camera->exposureMode = iris::CameraExposureMode::Inherit;
    doc.camera->postOverrides = QJsonObject();
    unsigned long long backHash = 0;
    const double back = shoot(mirror, view, doc.camera, true, &backHash, 0,
                              "B3 back to inherit");
    std::printf("    back to inherit: %.2f (was %.2f)\n", back, inherited);
    CHECK(std::fabs(back - inherited) < 1.5,
          "INHERIT: clearing every override puts the camera back on the world's grade "
          "(%.2f vs %.2f)", back, inherited);

    // ---- B4: THE DETERMINISM NEGATIVE -------------------------------------
    // The shape every thumbnail, preview and pixel suite has: an offscreen view
    // that never opted in. It must be byte-identical whatever the camera says.
    View *plain = gEngine->createOffscreenView("plainView", 96, 96, Colour(0.10f, 0.10f, 0.12f));
    if (!plain) { std::printf("FAIL: plain view\n"); ++failures; return; }
    plain->setScene(target);
    unsigned long long neutral = 0;
    shoot(mirror, plain, doc.camera, false, &neutral, 18);

    doc.camera->exposureMode = iris::CameraExposureMode::Manual;
    doc.camera->exposure = 4.0f;
    doc.camera->setPostOverride(QStringLiteral("hdr"), true);
    doc.camera->setPostOverride(QStringLiteral("bloom"), true);
    doc.camera->setPostOverride(QStringLiteral("bloomThreshold"), 0.01);
    doc.camera->setPostOverride(QStringLiteral("ssao"), true);
    unsigned long long loud = 0;
    shoot(mirror, plain, doc.camera, false, &loud, 18);
    CHECK(neutral == loud,
          "DETERMINISM: an offscreen view that did not opt in is BYTE-IDENTICAL whatever the "
          "driving camera overrides (hash %llu vs %llu)", neutral, loud);

    // The document still holds every override — the guarantee is the engine's
    // one place, not a mirror that quietly declined to push.
    CHECK(doc.camera->hasPostOverride(QStringLiteral("ssao")),
          "…and the camera still carries its overrides (the guard is the engine's, not a "
          "silent refusal to store)");

    gEngine->destroyView(plain);
    gEngine->destroyView(view);
    // UNBIND BEFORE DESTROYING: an engine scene destroyed under a bound
    // document leaves every node handle in it stale (iris::Scene::setGraphScene
    // warns about exactly this).
    mirror.setSource(iris::ScenePtr());
    gEngine->destroyScene(target);
}

// ---------------------------------------------------------------------------
// PART C — the document contract (no rendering).
// ---------------------------------------------------------------------------

void partC()
{
    auto cam = iris::CameraNode::create();

    // Defaults are "as if this block did not exist".
    CHECK(cam->exposureMode == iris::CameraExposureMode::Inherit,
          "a new camera inherits the world's exposure");
    CHECK(cam->postOverrides.isEmpty(), "a new camera overrides nothing");

    // Every key the document declares is reachable through the reflection
    // layer, and a null clears it — which is how "inherit" is expressible.
    int count = 0;
    const iris::CameraPostKey *table = iris::cameraPostKeys(count);
    CHECK(count > 0, "the override key table is not empty (%d keys)", count);
    bool allRoundTrip = true;
    for (int i = 0; i < count; ++i) {
        const QString key = QString::fromLatin1(table[i].id);
        // A Stack key (POST_LOOKS_SPEC.md §4.1 — `looks`) holds a whole array,
        // so the value that round-trips it is a list. An EMPTY one is a real
        // override and is exactly the interesting case: it means "this camera
        // has no looks" over a world that has some, which is a third state a
        // boolean could not carry.
        const QVariant value =
            table[i].type == iris::CameraPostKeyType::Stack
                ? QVariant(QVariantList())
                : (table[i].type == iris::CameraPostKeyType::Number
                       ? QVariant(1.25)
                       : (key == QLatin1String("smaa") ? QVariant(-1) : QVariant(1)));
        if (!cam->setPropertyValue(QStringLiteral("postFx.") + key, value)) allRoundTrip = false;
        if (!cam->getPropertyValue(QStringLiteral("postFx.") + key).isValid()) allRoundTrip = false;
        if (!cam->setPropertyValue(QStringLiteral("postFx.") + key, QVariant())) allRoundTrip = false;
        if (cam->getPropertyValue(QStringLiteral("postFx.") + key).isValid()) allRoundTrip = false;
    }
    CHECK(allRoundTrip,
          "every override key sets, reads back and CLEARS through node.setProperty");

    // The two refusals the table cannot express.
    CHECK(!cam->setPostOverride(QStringLiteral("exposure"), 1.0),
          "a camera's exposure is its own block, not an override slot");
    CHECK(!cam->setPostOverride(QStringLiteral("smaa"), 2),
          "a camera may not pick an SMAA PRESET (that is a shader recompile)");
    CHECK(cam->setPostOverride(QStringLiteral("smaa"), -1),
          "…but it may switch SMAA off, which is compositor shape and free");

    // The exposure window stays ordered, whichever end is written first.
    cam->setPropertyValue(QStringLiteral("exposureMin"), 2.0f);
    cam->setPropertyValue(QStringLiteral("exposureMax"), -2.0f);
    CHECK(cam->exposureMin <= cam->exposureMax,
          "the auto-exposure window stays ordered (%.2f..%.2f)",
          double(cam->exposureMin), double(cam->exposureMax));

    // A duplicate is the SAME camera, overrides and all.
    cam->exposureMode = iris::CameraExposureMode::Manual;
    cam->exposure = -1.5f;
    auto copy = cam->createDuplicate().staticCast<iris::CameraNode>();
    CHECK(copy->exposureMode == iris::CameraExposureMode::Manual &&
              std::fabs(copy->exposure - (-1.5f)) < 1e-6f &&
              copy->postOverrides == cam->postOverrides,
          "createDuplicate copies the exposure block and the whole override map");
}

// ---------------------------------------------------------------------------
// PART D — THE METER, THROUGH THE DOCUMENT (EXPOSURE-2).
//
// hdr.meter asserts the histogram meter's ARITHMETIC against numbers computed
// on paper. This part asserts the other half: that the scene's choice of
// METERING PATTERN reaches the view that renders it, that a camera override
// does not silently reset it, and that it MOVES THE PICTURE in the direction
// the pattern promises.
//
// THE FIXTURE IS THE CLASSIC PHOTOGRAPHIC CASE, and it is what this part
// exists to demonstrate: a SUBJECT DARKER THAN THE SKY BEHIND IT — the backlit
// portrait. An averaging meter reads mostly sky and stops down, leaving the
// subject in shadow; a SPOT meter reads the subject and opens up. That is the
// whole reason a metering pattern exists, and it is a picture, not a number.
//
// THE PREMISE IS MEASURED, not assumed: part D first reads the centre ninth of
// the frame against its surround under a MANUAL grade, where no meter is
// involved at all, and only then asserts what the patterns do about it.
// ---------------------------------------------------------------------------

/// Mean luminance of the middle ninth of a frame, and of everything outside it.
void centreAndSurround(const Image &img, double &centre, double &surround)
{
    double cs = 0.0, ss = 0.0;
    long cn = 0, sn = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const size_t i = (size_t(y) * img.width + x) * 4;
            if (i + 3 >= img.rgba.size()) continue;
            const double l = 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] +
                             0.0722 * img.rgba[i + 2];
            const bool inCentre = x >= img.width / 3 && x < img.width * 2 / 3 &&
                                  y >= img.height / 3 && y < img.height * 2 / 3;
            if (inCentre) { cs += l; ++cn; } else { ss += l; ++sn; }
        }
    centre = cn ? cs / double(cn) : 0.0;
    surround = sn ? ss / double(sn) : 0.0;
}

void partD()
{
    Doc doc;
    Scene *target = gEngine->createScene("meterDoc");
    View *view = gEngine->createOffscreenView("meterDocView", 192, 108,
                                              Colour(0.10f, 0.10f, 0.12f));
    if (!target || !view) { std::printf("FAIL: part D scene/view\n"); ++failures; return; }
    SceneMirror mirror(target);
    mirror.setSource(doc.scene);
    view->setScene(target);

    // ---- D1: the document's defaults -----------------------------------
    CHECK(doc.scene->exposureMetering == iris::ExposureMetering::CentreWeighted,
          "a new scene meters CENTRE WEIGHTED");
    CHECK(std::fabs(doc.scene->exposureMeterLowPercent - 10.0f) < 1e-6f &&
              std::fabs(doc.scene->exposureMeterHighPercent - 90.0f) < 1e-6f,
          "…and clips the darkest and brightest tenth (%.1f/%.1f)",
          double(doc.scene->exposureMeterLowPercent),
          double(doc.scene->exposureMeterHighPercent));
    CHECK(std::string(iris::exposureMeteringName(iris::ExposureMetering::Spot)) == "spot" &&
              iris::exposureMeteringFromName("average") == iris::ExposureMetering::Average,
          "the pattern's names round-trip");
    bool badOk = true;
    iris::exposureMeteringFromName("sideways", &badOk);
    CHECK(!badOk, "…and an unknown name is REFUSED rather than silently defaulted");

    // ---- D2: it reaches the view that renders it ------------------------
    doc.scene->exposureMode = iris::ExposureMode::Auto;
    doc.scene->exposureMin = -6.0f;
    doc.scene->exposureMax = 6.0f;                 // wide: the meter decides
    doc.scene->exposureMetering = iris::ExposureMetering::Spot;
    doc.scene->exposureMeterLowPercent = 2.0f;
    doc.scene->exposureMeterHighPercent = 98.0f;
    shoot(mirror, view, doc.camera, true, nullptr, 3);
    CHECK(view->postFx().meterPattern == ExposureMeterPattern::Spot &&
              std::fabs(view->postFx().meterLowPercent - 2.0f) < 1e-6f &&
              std::fabs(view->postFx().meterHighPercent - 98.0f) < 1e-6f,
          "the scene's metering pattern and clips reach the view's description");

    // ---- D3: A CAMERA OVERRIDE DOES NOT RESET THE WORLD'S METER ---------
    // The meter is the WORLD's: a CameraNode has an exposure block and no
    // metering block (per camera comes later), so a camera that overrides the
    // MODE must leave the pattern the world chose alone. It is one line in the
    // mirror (applyExposure and applyMeter are separate calls, and only the
    // world makes the second) and it would fail silently otherwise — a camera
    // would quietly meter centre-weighted.
    doc.camera->exposureMode = iris::CameraExposureMode::Auto;
    doc.camera->exposure = 0.0f;
    doc.camera->exposureMin = -6.0f;
    doc.camera->exposureMax = 6.0f;
    shoot(mirror, view, doc.camera, true, nullptr, 3);
    CHECK(view->postFx().meterPattern == ExposureMeterPattern::Spot &&
              std::fabs(view->postFx().meterLowPercent - 2.0f) < 1e-6f,
          "a camera in Auto over the world's Spot meter still meters SPOT");
    doc.camera->exposureMode = iris::CameraExposureMode::Inherit;

    // ---- D4: THE FIXTURE'S PREMISE, under a grade with no meter in it ----
    doc.scene->exposureMode = iris::ExposureMode::Manual;
    doc.scene->exposureMetering = iris::ExposureMetering::Average;
    doc.scene->exposureMeterLowPercent = 10.0f;
    doc.scene->exposureMeterHighPercent = 90.0f;
    shoot(mirror, view, doc.camera, true, nullptr, 8);
    Image manual;
    double centre = 0.0, surround = 0.0;
    if (view->readPixels(manual)) centreAndSurround(manual, centre, surround);
    std::printf("    manual grade: centre ninth %.2f, surround %.2f\n", centre, surround);
    CHECK(surround > centre + 5.0,
          "PREMISE: the SUBJECT in the middle of this frame is DARKER than the sky around "
          "it (%.2f against %.2f) — the backlit case, and without it nothing below would "
          "mean anything", centre, surround);

    // ---- D5: AVERAGE vs SPOT, in the picture ----------------------------
    doc.scene->exposureMode = iris::ExposureMode::Auto;
    const double avgLuma = shoot(mirror, view, doc.camera, true);
    const double avgScale = double(view->measuredExposureScale());
    doc.scene->exposureMetering = iris::ExposureMetering::Spot;
    const double spotLuma = shoot(mirror, view, doc.camera, true);
    const double spotScale = double(view->measuredExposureScale());
    std::printf("    average: multiplier %.4f, frame %.2f | spot: multiplier %.4f, frame %.2f\n",
                avgScale, avgLuma, spotScale, spotLuma);
    CHECK(avgScale > 0.0 && spotScale > 0.0,
          "both patterns produced a measurement (%.4f, %.4f)", avgScale, spotScale);
    CHECK(spotScale > avgScale * 1.02,
          "SPOT exposes for the SUBJECT and not for the sky behind it, so it OPENS UP "
          "(multiplier %.4f against average's %.4f)", spotScale, avgScale);
    CHECK(spotLuma > avgLuma + 1.0,
          "…and the picture is brighter for it (%.2f vs %.2f) — the backlit subject comes "
          "out of shadow, which is the whole point of the dial", spotLuma, avgLuma);
    // The SUBJECT itself is what moved, not just the frame's average.
    Image spotImg;
    double spotCentre = 0.0, spotSurround = 0.0;
    if (view->readPixels(spotImg)) centreAndSurround(spotImg, spotCentre, spotSurround);
    std::printf("    spot: centre ninth %.2f, surround %.2f\n", spotCentre, spotSurround);
    CHECK(spotCentre > centre + 5.0,
          "…and it is the SUBJECT that came up (centre ninth %.2f against the manual "
          "grade's %.2f)", spotCentre, centre);

    // ---- D6: the clips move the grade, in the direction they promise -----
    // A window of 0..100 keeps the brightest pixels the 90 clip threw away, so
    // the measurement rises and the grade comes DOWN.
    doc.scene->exposureMetering = iris::ExposureMetering::Average;
    doc.scene->exposureMeterLowPercent = 0.0f;
    doc.scene->exposureMeterHighPercent = 100.0f;
    shoot(mirror, view, doc.camera, true);
    const double keepAll = double(view->measuredExposureScale());
    doc.scene->exposureMeterHighPercent = 60.0f;   // cut the brightest 40 %
    shoot(mirror, view, doc.camera, true);
    const double cutBright = double(view->measuredExposureScale());
    std::printf("    keep everything %.4f -> cut the brightest 40 %% %.4f\n",
                keepAll, cutBright);
    CHECK(cutBright > keepAll * 1.02,
          "CUTTING THE BRIGHT TAIL raises the grade (%.4f -> %.4f): the pixels a mean "
          "could not resist are out of the measurement", keepAll, cutBright);

    gEngine->destroyView(view);
    mirror.setSource(iris::ScenePtr());
    gEngine->destroyScene(target);
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_camera_exposure-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: Engine::create — %s\n", err.c_str()); return 1; }
    gEngine = engine.get();

    std::printf("\n-- A1 two views, two exposures, one frame --\n");        a1_two_views_two_exposures();
    std::printf("\n-- A2 the SSAO projection is per view --\n");            a2_ssao_projection_is_per_view();
    std::printf("\n-- A3 a cut re-seeds the exposure history --\n");        a3_cut_reseeds_the_exposure_history();
    std::printf("\n-- B  camera exposure and overrides through the mirror --\n"); partB();
    std::printf("\n-- C  the document contract --\n");                      partC();
    std::printf("\n-- D  the METER, through the document --\n");            partD();

    gEngine = nullptr;
    engine.reset();
    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
