// THE PiP TONEMAP — CAMERAS_SPEC §7.2 "Route C", in pixels
// (POST_CHAIN_SPEC §14's fourth consumer, the one item 6 left open).
//
// THE DEFECT THIS SUITE EXISTS FOR. The inset used to render its scene pass
// straight into the window — after the main chain had already tonemapped it. A
// world the viewport grades filmically therefore showed a RAW LINEAR inset
// beside a graded main image: everything above 1.0 clipped to flat white two
// centimetres from the same highlight rolling off correctly. And because the
// inset had no post chain at all, a camera's own exposure and post overrides
// (CAMERA_LENS_SPEC §4/§5, shipped) could not appear in its own preview — the
// camera panel carried a hint saying so.
//
// WHAT IS ASSERTED, all of it from real frames of the real backend, all of it
// through the HOST ROUTE (SceneMirror::applyPip), because that is the seam the
// editor uses:
//
//   1. THE GRADE. A surface at TWICE WHITE saturates 0% of the inset, exactly
//      as it saturates 0% of the main view — and the inset's centre pixel is
//      the main view's centre pixel, because both went through the same
//      HDR/FinalToneMapping quad at the same fixed exposure. The CONTROL that
//      keeps this from passing for the wrong reason: the same frame with the
//      inset's tonemap switched off is the old defect, blown out, measured.
//   2. THE PER-CAMERA LOOK. The PIPPED camera's own exposure changes the inset
//      and NOTHING ELSE: the frame outside the rect is byte-identical across
//      the change, and putting the camera back on Inherit restores the inset
//      byte-for-byte.
//   3. DETERMINISM. Switching the inset off leaves the main view byte-identical
//      to a frame that never had one.
//   4. NO STRETCHING. A world-space SQUARE stays square in the inset at two
//      rectangles of very different shape — the assertion that pins Route C's
//      explicit camera aspect and its rect-shaped local texture. (An inset that
//      kept setAutoAspectRatio, or a texture whose shape did not follow the
//      rect, fails here and nowhere else.)
//   5. HAZARD 3 (POST_CHAIN_SPEC §14): applyPip moves the rects LIVE while a
//      fraction-sized local texture does not follow. So: a steady inset, a
//      MOVING inset and a moving CAMERA must not rebuild anything
//      (View::pipGeneration is flat over N frames), while a RESIZE rebuilds
//      exactly once — and a main-workspace rebuild re-appends the inset, which
//      is hazard 2 (the inset must stay LAST; there is no reorder API).
//
// Hazards 1 (kMultiWorkspaceStore's MSAA store action) and 2 are additionally
// pinned engine-side by tests/engine's pip_over_msaa_keeps_the_main_frame and
// pip_survives_a_main_workspace_rebuild, which Route C leaves passing.
//
// Runs with QT_QPA_PLATFORM=offscreen and a reachable DISPLAY (Vulkan).

#include <QGuiApplication>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
} while (0)

namespace {

/// The view is small enough to be quick and big enough that an inset a third of
/// its width is still tens of pixels across — the aspect assertion measures a
/// bounding box, and a bounding box needs pixels.
constexpr unsigned kW = 240, kH = 180;
/// How far the cameras sit from the subject. Chosen so the cube's silhouette is
/// about a quarter of the frame HEIGHT: the aspect assertion measures a
/// bounding box, and a box clipped by the inset's edge is not a measurement of
/// anything. The tall test rect is 48x100 px, so the subject must be under 48
/// px on a 100 px height — a quarter leaves room to spare.
constexpr float kSubjectDistance = 9.0f;

struct Rect { float l, t, w, h; };

size_t diffAll(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return size_t(-1);
    size_t n = 0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4)
        if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] ||
            a.rgba[i + 2] != b.rgba[i + 2] || a.rgba[i + 3] != b.rgba[i + 3]) ++n;
    return n;
}

/// Differing pixels inside (or outside) a normalised rect. The rect's own
/// boundary row/column is skipped on the OUTSIDE count: a normalised edge only
/// lands on a pixel boundary when the rect divides the size exactly.
size_t diffInRect(const Image &a, const Image &b, const Rect &r, bool inside)
{
    const int x0 = int(r.l * a.width), x1 = int((r.l + r.w) * a.width);
    const int y0 = int(r.t * a.height), y1 = int((r.t + r.h) * a.height);
    size_t n = 0;
    for (int y = 0; y < int(a.height); ++y)
        for (int x = 0; x < int(a.width); ++x) {
            const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (in != inside) continue;
            if (!inside && x >= x0 - 1 && x <= x1 && y >= y0 - 1 && y <= y1) continue;
            const size_t i = (size_t(y) * a.width + x) * 4u;
            if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] ||
                a.rgba[i + 2] != b.rgba[i + 2] || a.rgba[i + 3] != b.rgba[i + 3]) ++n;
        }
    return n;
}

/// Lit pixels, saturated pixels and the brightest channel inside a rect — the
/// §14 histogram, restricted to a region so the same function measures the
/// inset and the main view.
struct Histogram { int lit = 0, saturated = 0, brightest = 0; double meanLuma = 0.0; };

Histogram histogram(const Image &img, const Rect &r)
{
    Histogram h;
    const int x0 = int(r.l * img.width), x1 = int((r.l + r.w) * img.width);
    const int y0 = int(r.t * img.height), y1 = int((r.t + r.h) * img.height);
    double sum = 0.0;
    int n = 0;
    for (int y = std::max(0, y0); y < std::min(int(img.height), y1); ++y)
        for (int x = std::max(0, x0); x < std::min(int(img.width), x1); ++x) {
            const size_t i = (size_t(y) * img.width + x) * 4u;
            const int rr = img.rgba[i], gg = img.rgba[i + 1], bb = img.rgba[i + 2];
            sum += 0.2126 * rr + 0.7152 * gg + 0.0722 * bb;
            ++n;
            if (rr < 40 && gg < 40 && bb < 40) continue;     // the inset's background
            ++h.lit;
            h.brightest = std::max(h.brightest, std::max({ rr, gg, bb }));
            if (rr >= 254 && gg >= 254 && bb >= 254) ++h.saturated;
        }
    h.meanLuma = n ? sum / n : 0.0;
    return h;
}

/// The bounding box of everything BRIGHT inside a rect, in pixels. The subject
/// is an emissive white cube on a near-black inset background, so "bright" is
/// unambiguous and the box is the cube's silhouette — a SQUARE, if the inset
/// is not stretching it.
struct Box { int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
             int w() const { return x1 - x0 + 1; }
             int h() const { return y1 - y0 + 1; }
             bool valid() const { return x1 >= x0 && y1 >= y0; } };

Box brightBox(const Image &img, const Rect &r)
{
    Box b;
    b.x0 = int(img.width); b.y0 = int(img.height); b.x1 = -1; b.y1 = -1;
    const int x0 = int(r.l * img.width), x1 = int((r.l + r.w) * img.width);
    const int y0 = int(r.t * img.height), y1 = int((r.t + r.h) * img.height);
    for (int y = std::max(0, y0); y < std::min(int(img.height), y1); ++y)
        for (int x = std::max(0, x0); x < std::min(int(img.width), x1); ++x) {
            const size_t i = (size_t(y) * img.width + x) * 4u;
            const int lum = (img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2]) / 3;
            if (lum < 96) continue;
            b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x);
            b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
        }
    return b;
}

Colour centreOf(const Image &img, const Rect &r)
{
    return img.at(unsigned((r.l + r.w * 0.5f) * img.width),
                  unsigned((r.t + r.h * 0.5f) * img.height));
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_camera_pip-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // The VIEW first (createScene refuses before the first view exists), then
    // the engine scene, and only then the document (SCENEGRAPH_SPEC D2).
    View *view = engine->createOffscreenView("pipView", kW, kH, Colour(0.02f, 0.02f, 0.03f));
    Scene *target = engine->createScene("pip-scene");
    CHECK(view && target, "offscreen view + engine scene");
    if (!view || !target) return 1;
    view->setScene(target);
    target->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    auto doc = iris::Scene::create();
    SceneMirror mirror(target);
    mirror.setSource(doc);

    // THE SUBJECT: a cube whose light is its own, at TWICE WHITE. §14's window
    // is narrow and deliberate — the raw path saturates at 1.0 and the filmic
    // curve's grade tail saturates at about 2.5 at this exposure, so 2.0 is
    // over-range for raw and inside the grade. That is the whole band this
    // feature recovers, and a value of 8 would clip in both.
    auto cube = iris::MeshNode::create();
    cube->setName("hot cube");
    cube->setMesh(":assets/models/cube.obj");
    CHECK(!!cube->getMesh(), "cube.obj loaded into the document");
    {
        const float r = cube->getMeshRadius();
        const float s = r > 0.0f ? 1.0f / r : 1.0f;
        cube->setLocalScale(iris::Vec3(s, s, s));
    }
    {
        auto mat = iris::PbrMaterial::create();
        mat->setBaseColor(QColor(0, 0, 0));
        mat->setEmissiveColor(QColor(255, 255, 255));
        mat->setEmissiveIntensity(2.0f);        // TWICE WHITE
        cube->setMaterial(mat);
    }
    doc->getRootNode()->addChild(cube);

    // THE WORLD GRADES. hdrEnabled is what makes the main view's chain tonemap,
    // and it is what a host reads into ViewPipDesc::tonemap for the inset.
    doc->hdrEnabled = true;
    doc->exposure = 0.6f;                       // the scene default
    doc->exposureMin = iris::lens::manualExposureClamp();
    doc->exposureMax = doc->exposureMin;

    // Two cameras at the SAME pose: the explorer that drives the view and the
    // scene camera the inset previews. Identical shots make the grade
    // comparable pixel for pixel — the inset is the main view, in miniature.
    auto editorCam = iris::CameraNode::create();
    editorCam->setLocalPos(iris::Vec3(0.0f, 0.0f, kSubjectDistance));
    editorCam->lookAt(iris::Vec3(0, 0, 0));

    auto shotCam = iris::CameraNode::create();
    shotCam->setName("Shot A");
    shotCam->bodyVisible = false;               // no helper wires in the picture
    shotCam->setLocalPos(iris::Vec3(0.0f, 0.0f, kSubjectDistance));
    shotCam->lookAt(iris::Vec3(0, 0, 0));
    doc->getRootNode()->addChild(shotCam);

    Rect rect { 0.60f, 0.58f, 0.36f, 0.38f };
    bool pipOn = false;
    bool forceRaw = false;      // the control: the inset WITHOUT its tonemap

    // ONE FRAME, the way the editor takes one: sync, world, camera, inset —
    // in that order, because applyPip layers the pipped camera over the world
    // description applyEnvironment just recorded.
    //
    // The main view is pushed into the DETERMINISTIC form of the same grade
    // (tonemapFixed, §14) for the same reason the thumbnails are: the automatic
    // exposure adapts against wall-clock time, and a number that depends on how
    // long the test took is not an assertion. It is also what makes the main
    // view and the inset directly comparable — same curve, same exposure.
    bool worldDirty = true;
    auto frame = [&](Image &out) {
        mirror.sync();
        // THE WORLD, ONCE. applyEnvironment rebuilds the view's description from
        // the document every frame, and the deterministic form this suite wants
        // (tonemapFixed) is a chain SHAPE the document has no field for — so
        // pushing it after every applyEnvironment would flip the main
        // workspace's shape twice per frame and measure the test's own hack
        // rather than the product. The document does not change between frames
        // here, so the world is pushed when it is dirty and not otherwise;
        // that is exactly what a stable document costs the editor.
        if (worldDirty) {
            mirror.applyEnvironment(view);
            PostFxDesc fx = view->postFx();
            fx.allowOffscreen = true;
            fx.tonemapFixed = true;
            view->setPostFx(fx);
            worldDirty = false;
        }
        mirror.applyCamera(editorCam, view);
        ViewPipDesc pip;
        pip.enabled = pipOn;
        pip.allowOffscreen = true;              // offscreen is the only readable kind
        pip.left = rect.l; pip.top = rect.t; pip.width = rect.w; pip.height = rect.h;
        mirror.applyPip(pipOn ? shotCam : iris::CameraNodePtr(), view, pip);
        if (forceRaw) {                         // the control, engine-side
            ViewPipDesc raw = view->pip();
            raw.tonemap = false;
            view->setPip(raw);
        }
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(out);
    };

    // ---- 1. THE GRADE -----------------------------------------------------
    Image noPip; frame(noPip);
    const Rect whole { 0.0f, 0.0f, 1.0f, 1.0f };
    const Histogram mainH = histogram(noPip, whole);
    std::printf("    main view:   %d lit, %d saturated (%.0f%%), brightest %d\n",
                mainH.lit, mainH.saturated,
                mainH.lit ? 100.0 * mainH.saturated / mainH.lit : 0.0, mainH.brightest);
    {
        const Box mb = brightBox(noPip, whole);
        std::printf("    subject in the main view: %dx%d px of %ux%u\n",
                    mb.w(), mb.h(), kW, kH);
    }
    CHECK(mainH.lit > 500, "the hot cube renders in the main view (%d lit px)", mainH.lit);
    CHECK(mainH.saturated == 0,
          "THE MAIN VIEW IS GRADED: nothing clips (%d of %d lit px saturated, brightest %d)",
          mainH.saturated, mainH.lit, mainH.brightest);

    pipOn = true;
    Image graded; frame(graded);
    const Histogram insetH = histogram(graded, rect);
    std::printf("    inset:       %d lit, %d saturated (%.0f%%), brightest %d\n",
                insetH.lit, insetH.saturated,
                insetH.lit ? 100.0 * insetH.saturated / insetH.lit : 0.0, insetH.brightest);
    CHECK(insetH.lit > 100, "the inset renders the camera's shot (%d lit px)", insetH.lit);
    CHECK(insetH.saturated == 0,
          "THE INSET IS GRADED TOO: the twice-white surface saturates 0%% of the inset "
          "(%d of %d lit px, brightest %d) — the same class as the main view",
          insetH.saturated, insetH.lit, insetH.brightest);
    CHECK(diffInRect(graded, noPip, rect, /*inside*/ false) == 0,
          "the inset changes NOTHING outside its rect");

    // The two pictures are the SAME shot at the same exposure through the same
    // tonemapper, so their centres must agree — the strongest form of "matches
    // the main view's tonemapped class" available without resampling the inset.
    {
        const Colour mid = centreOf(graded, rect);
        const Colour main = centreOf(noPip, whole);
        const int dr = int(std::lround(std::fabs(mid.r - main.r) * 255.0f));
        const int dg = int(std::lround(std::fabs(mid.g - main.g) * 255.0f));
        const int db = int(std::lround(std::fabs(mid.b - main.b) * 255.0f));
        std::printf("    centre:      main %3.0f %3.0f %3.0f   inset %3.0f %3.0f %3.0f\n",
                    main.r * 255, main.g * 255, main.b * 255,
                    mid.r * 255, mid.g * 255, mid.b * 255);
        CHECK(std::max({ dr, dg, db }) <= 2,
              "the inset's centre pixel IS the main view's, within %d/255",
              std::max({ dr, dg, db }));
    }

    // THE CONTROL — the defect, measured. Same frame, same scene, the inset's
    // tonemap switched off: this is exactly what the inset did before Route C.
    forceRaw = true;
    Image rawInset; frame(rawInset);
    const Histogram rawH = histogram(rawInset, rect);
    std::printf("    inset RAW:   %d lit, %d saturated (%.0f%%), brightest %d\n",
                rawH.lit, rawH.saturated,
                rawH.lit ? 100.0 * rawH.saturated / rawH.lit : 0.0, rawH.brightest);
    CHECK(rawH.saturated * 2 > rawH.lit,
          "CONTROL: an UNGRADED inset blows out (%d of %d lit px flat white) — the "
          "assertion above measures the tonemap and not the scene",
          rawH.saturated, rawH.lit);
    forceRaw = false;
    Image regraded; frame(regraded);
    CHECK(diffAll(regraded, graded) == 0,
          "…and switching the grade back on restores the inset byte-for-byte");

    // ---- 2. THE PER-CAMERA LOOK -------------------------------------------
    // The PIPPED camera's own exposure, resolved at the same one function the
    // lens program built (applyCameraPostFx), into the inset and nowhere else.
    shotCam->exposureMode = iris::CameraExposureMode::Manual;
    shotCam->exposure = -3.0f;                  // three stops under
    Image dark; frame(dark);
    const Histogram darkH = histogram(dark, rect);
    std::printf("    inset at -3 stops: mean luma %.2f (was %.2f)\n",
                darkH.meanLuma, insetH.meanLuma);
    CHECK(darkH.meanLuma < insetH.meanLuma - 8.0,
          "PER-CAMERA EXPOSURE: the pipped camera's own stops darken the INSET "
          "(%.2f vs %.2f)", darkH.meanLuma, insetH.meanLuma);
    CHECK(diffInRect(dark, graded, rect, /*inside*/ false) == 0,
          "…and the MAIN VIEW does not move by one byte while it does");

    shotCam->exposure = 3.0f;                   // three stops over
    Image bright; frame(bright);
    const Histogram brightH = histogram(bright, rect);
    std::printf("    inset at +3 stops: mean luma %.2f\n", brightH.meanLuma);
    CHECK(brightH.meanLuma > darkH.meanLuma + 8.0,
          "…and the exposure runs both ways (%.2f vs %.2f)",
          brightH.meanLuma, darkH.meanLuma);
    CHECK(diffInRect(bright, graded, rect, /*inside*/ false) == 0,
          "…still without touching the main view");

    // A per-camera POST OVERRIDE reaches the inset through the same map: `hdr`
    // off on this camera means its preview is the ungraded shot, and only its.
    shotCam->exposureMode = iris::CameraExposureMode::Inherit;
    shotCam->setPostOverride(QStringLiteral("hdr"), 0);
    Image overridden; frame(overridden);
    const Histogram overH = histogram(overridden, rect);
    std::printf("    inset with hdr override off: %d saturated of %d lit\n",
                overH.saturated, overH.lit);
    CHECK(overH.saturated * 2 > overH.lit,
          "PER-CAMERA OVERRIDE: `hdr:off` on the pipped camera ungrades ITS inset");
    CHECK(diffInRect(overridden, graded, rect, /*inside*/ false) == 0,
          "…and still nothing outside the rect");
    shotCam->clearPostOverride(QStringLiteral("hdr"));

    Image restored; frame(restored);
    CHECK(diffAll(restored, graded) == 0,
          "INHERIT IS INHERIT: a camera that overrides nothing restores the frame "
          "byte-for-byte");

    // ---- 3. DETERMINISM ---------------------------------------------------
    pipOn = false;
    Image pipOff; frame(pipOff);
    CHECK(diffAll(pipOff, noPip) == 0,
          "switching the inset off leaves the main view byte-identical to a frame that "
          "never had one");
    pipOn = true;

    // ---- 4. NO STRETCHING -------------------------------------------------
    // A world-space square, in two rectangles of very different shape. The
    // camera does not constrain its aspect, so it takes the RECT's — and the
    // cube's silhouette must stay square in PIXELS both times. This is the
    // assertion that fails if the inset keeps setAutoAspectRatio (the camera
    // would take the local texture's aspect) or if the local texture's shape
    // stops following the rect.
    {
        const Rect wide { 0.58f, 0.62f, 0.40f, 0.20f };     // 96 x 36 px, aspect 2.7
        const Rect tall { 0.68f, 0.40f, 0.20f, 0.56f };     // 48 x 100 px, aspect 0.5
        double ratios[2] = { 0.0, 0.0 };
        int i = 0;
        for (const Rect &r : { wide, tall }) {
            rect = r;
            Image img; frame(img);
            const Box b = brightBox(img, r);
            CHECK(b.valid() && b.w() > 6 && b.h() > 6,
                  "the subject is measurable in the %s rect (%dx%d px)",
                  i == 0 ? "wide" : "tall", b.w(), b.h());
            ratios[i] = b.h() ? double(b.w()) / double(b.h()) : 0.0;
            std::printf("    %s rect (%.0fx%.0f px): subject %dx%d px, aspect %.3f\n",
                        i == 0 ? "wide" : "tall", r.w * kW, r.h * kH, b.w(), b.h(), ratios[i]);
            ++i;
        }
        // One pixel of tolerance on a ~20 px box is 5%, and the two rects
        // disagree by a factor of five — a stretch would be unmissable.
        CHECK(std::fabs(ratios[0] - 1.0) < 0.12 && std::fabs(ratios[1] - 1.0) < 0.12,
              "NO STRETCHING: a world square is square in the inset at both rect shapes "
              "(%.3f and %.3f)", ratios[0], ratios[1]);
    }

    // ---- 5. HAZARD 3: the rects move live, the texture does not ------------
    {
        rect = Rect { 0.60f, 0.58f, 0.36f, 0.38f };
        Image img; frame(img);                      // settle on this rect
        const unsigned builtPip = view->pipGeneration();
        const unsigned builtWs  = view->workspaceGeneration();
        CHECK(builtPip > 0, "the inset's workspace exists (pipGeneration %u)", builtPip);

        // STEADY: the host pushes the whole description every frame, exactly as
        // the editor does. Nothing may rebuild.
        for (int i = 0; i < 30; ++i) frame(img);
        std::printf("    30 steady frames: pipGeneration %u -> %u, workspace %u -> %u\n",
                    builtPip, view->pipGeneration(), builtWs, view->workspaceGeneration());
        CHECK(view->pipGeneration() == builtPip && view->workspaceGeneration() == builtWs,
              "STEADY: 30 frames with the inset shown rebuild NOTHING");

        // MOVING the inset, and moving the CAMERA: both live, both free.
        for (int i = 0; i < 10; ++i) {
            rect.l = 0.50f + 0.004f * i;
            rect.t = 0.50f + 0.004f * i;
            shotCam->setLocalPos(iris::Vec3(0.02f * i, 0.0f, kSubjectDistance));
            frame(img);
        }
        std::printf("    10 moving frames: pipGeneration %u\n", view->pipGeneration());
        CHECK(view->pipGeneration() == builtPip,
              "MOVING the inset and the previewed camera rebuilds NOTHING either");
        shotCam->setLocalPos(iris::Vec3(0.0f, 0.0f, kSubjectDistance));

        // RESIZING it is the one thing that must: the local texture is sized
        // from the rect. Once, not once per frame.
        rect.w = 0.24f; rect.h = 0.26f;
        frame(img);
        const unsigned afterResize = view->pipGeneration();
        std::printf("    after a resize: pipGeneration %u\n", afterResize);
        CHECK(afterResize == builtPip + 1,
              "a RESIZE re-creates the inset's texture exactly once (%u -> %u)",
              builtPip, afterResize);
        for (int i = 0; i < 10; ++i) frame(img);
        CHECK(view->pipGeneration() == afterResize,
              "…and holding the new size rebuilds nothing further");

        // HAZARD 2, host-side: a MAIN-workspace rebuild must re-append the
        // inset (there is no reorder API — an inset added before the main
        // workspace is silently painted over).
        const unsigned beforeRebuild = view->pipGeneration();
        view->setShadows(!view->shadows());
        Image afterShadowFlip; frame(afterShadowFlip);
        std::printf("    main-workspace rebuild: workspace %u, pipGeneration %u -> %u\n",
                    view->workspaceGeneration(), beforeRebuild, view->pipGeneration());
        CHECK(view->pipGeneration() > beforeRebuild,
              "a MAIN workspace rebuild re-appends the inset (it must stay LAST)");
        CHECK(histogram(afterShadowFlip, rect).lit > 100,
              "…and the inset is still on screen after it");
        view->setShadows(!view->shadows());
    }

    // ---- 6. THE CONSISTENCY RULE ------------------------------------------
    // A host may ask for a graded inset on a surface that is not graded itself
    // (an offscreen view that never opted into the post chain, a world with HDR
    // off). It must not get one: a graded inset on a raw frame is the SAME
    // mismatch Route C exists to remove, with the halves swapped. The engine
    // ANDs the request with its own chain (OgreView::pipTonemapEffective), and
    // this is that AND, in pixels.
    {
        rect = Rect { 0.60f, 0.58f, 0.36f, 0.38f };
        doc->hdrEnabled = false;                // the world stops grading...
        worldDirty = true;
        Image plain; frame(plain);
        ViewPipDesc ask = view->pip();
        ask.tonemap = true;                     // ...and the host asks anyway
        view->setPip(ask);
        Image asked; frame(asked);
        const Histogram mainRaw = histogram(asked, whole);
        const Histogram insetRaw = histogram(asked, rect);
        std::printf("    ungraded view: main %d/%d saturated, inset %d/%d saturated\n",
                    mainRaw.saturated, mainRaw.lit, insetRaw.saturated, insetRaw.lit);
        CHECK(mainRaw.saturated * 2 > mainRaw.lit && insetRaw.saturated * 2 > insetRaw.lit,
              "CONSISTENCY: an inset cannot grade on a surface that does not — both "
              "halves of the frame are raw together");
        CHECK(diffAll(asked, plain) == 0,
              "…and asking for it changed nothing at all (byte-identical frames)");
        doc->hdrEnabled = true;
        worldDirty = true;
    }

    mirror.setSource(nullptr);
    engine->destroyScene(target);

    std::printf(failures == 0 ? "\nALL PiP TONEMAP CHECKS PASSED\n"
                              : "\n%d PiP TONEMAP CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
