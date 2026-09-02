// Avatar Part 0, the PIXEL half: the two toggles, at the pixel level.
//
// AvatarPreviewScene (its own engine Scene + SceneMirror + BoneOverlay) driven
// headless into an offscreen View. The owner named the shape as two INDEPENDENT
// booleans, and all four combinations are pixel-assertable
// (AVATAR_MODULE_SPEC §0.7):
//
//   mesh on / skeleton off   red at the subject centre, ZERO overlay pixels
//   mesh on / skeleton on    both colours present
//   mesh off / skeleton on   ZERO mesh pixels, > 10 overlay pixels   <- the ask
//   mesh off / skeleton off  background only
//
// Plus S7: the pose reaches the overlay — the bones are somewhere else at
// t = 0.5 than at t = 0.
//
// The overlay draws 3D BONES (octahedra parent joint -> child joint, a marker
// at each joint, a stub past each leaf), not lines, so this suite also pins:
//   * the counts are re-baselined, in a band, not loosened to "nonzero" —
//     captured on this fixture at 160x160 on both an NVIDIA GPU and lavapipe,
//     and verified to FAIL against the old line overlay;
//   * a bone is a solid tapered SHAPE: a cross-section through the bone body
//     (away from the joint markers) is 6 px, where lines gave exactly 1;
//   * the structural counts (bones / leaf stubs / joint markers) for a rig
//     whose shape the fixture fixes: 1 bone, 1 stub, 2 joints.
//
// DEPTH: bones are depth-tested now (the old line overlay had depth test OFF
// and always drew on top). The fixture's mesh is a FLAT strip in the z = 0
// plane and the bone octahedron straddles it, so the front half still shows
// with the mesh on — on a real character the skeleton is inside the mesh and
// is hidden, which is the accepted behaviour until an X-ray mode exists.
#include <QApplication>
#include <QColor>
#include <cmath>
#include <cstdio>
#include <string>

#include "bridge/avatarpreviewscene.h"
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"
#include "modules/avatar/avatarpreviewmodel.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const char *kRig = JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2.glb";

// The fixture's material is baseColorFactor (0.9, 0.15, 0.15); the overlay is
// the BoneOverlay default green; the sky is (28, 30, 36). Classify by
// dominance, not absolute brightness — the exact shade is the engine's.
static bool isMesh(const Colour &c)    { return c.r > 0.10f && c.r > c.g * 1.8f && c.r > c.b * 1.8f; }
static bool isOverlay(const Colour &c) { return c.g > 0.45f && c.g > c.r * 1.8f && c.g > c.b * 1.8f; }

template <typename Pred>
static int count(const Image &img, Pred pred)
{
    int n = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x)
            if (pred(img.at(x, y))) ++n;
    return n;
}

// The widest unbroken horizontal run of overlay pixels — how THICK the drawn
// skeleton is. A 1-pixel line overlay can only ever reach 1 or 2 (2 where two
// lines cross); a bone octahedron is as wide as its girth.
static int widestOverlayRun(const Image &img)
{
    int widest = 0;
    for (unsigned y = 0; y < img.height; ++y) {
        int run = 0;
        for (unsigned x = 0; x < img.width; ++x) {
            if (isOverlay(img.at(x, y))) { ++run; if (run > widest) widest = run; }
            else run = 0;
        }
    }
    return widest;
}

// The widest overlay run in the row `f` of the way down the overlay's own
// vertical extent — a cross-section of the drawn skeleton AWAY from the joint
// markers, i.e. the thickness of the bone body itself.
static int runAtFraction(const Image &img, float f)
{
    int top = -1, bottom = -1;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x)
            if (isOverlay(img.at(x, y))) { if (top < 0) top = int(y); bottom = int(y); break; }
    if (top < 0) return 0;
    const unsigned row = unsigned(top + int(f * float(bottom - top)));
    int run = 0, widest = 0;
    for (unsigned x = 0; x < img.width; ++x) {
        if (isOverlay(img.at(x, row))) { ++run; if (run > widest) widest = run; }
        else run = 0;
    }
    return widest;
}

// Vertical centroid of the overlay pixels (S7's assertion surface).
static float overlayCentroidY(const Image &img)
{
    float sum = 0; int n = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x)
            if (isOverlay(img.at(x, y))) { sum += float(y); ++n; }
    return n ? sum / float(n) : -1.0f;
}

static Image render(AvatarPreviewScene &scene, Engine &engine, View *view, int frames = 3)
{
    for (int i = 0; i < frames; ++i) {
        scene.step(0.0f, int(view->width()), int(view->height()));   // dt 0: paused, deterministic
        engine.renderOneFrame();
    }
    Image img;
    view->readPixels(img);
    return img;
}

static void show(const char *tag, const Image &img)
{
    const Colour c = img.at(img.width / 2, img.height / 2);
    std::printf("    %-38s centre %3.0f %3.0f %3.0f   mesh px %4d   overlay px %4d   widest run %2d"
                "   cross-section .35/.5/.65 %2d %2d %2d\n",
                tag, c.r * 255, c.g * 255, c.b * 255, count(img, isMesh), count(img, isOverlay),
                widestOverlayRun(img), runAtFraction(img, 0.35f), runAtFraction(img, 0.5f),
                runAtFraction(img, 0.65f));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_avatar_preview-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    const int W = 160, H = 160;
    View *view = engine->createOffscreenView("avatar", unsigned(W), unsigned(H), Colour(28/255.f, 30/255.f, 36/255.f));
    CHECK(view != nullptr, "offscreen view");
    if (!view) return 1;

    avatar::AvatarPreviewModel model;
    CHECK(model.load(QString::fromUtf8(kRig)), "the rigged fixture loads into the preview model");
    if (!model.isLoaded()) return 1;
    model.setClip("Idle");
    model.setTime(0.0f);

    AvatarPreviewScene scene(engine);
    CHECK(scene.attach(view), "the preview scene attaches to the view");
    scene.setModel(&model);

    // ---- S4: mesh on, skeleton off ----
    model.setMeshVisible(true);
    model.setSkeletonVisible(false);
    Image img = render(scene, *engine, view);
    show("S4 mesh on / skeleton off", img);
    CHECK(isMesh(img.at(unsigned(W / 2), unsigned(H / 2))), "S4: the mesh covers the subject centre");
    CHECK(count(img, isOverlay) == 0, "S4: NO overlay pixels while the skeleton is off");
    CHECK(scene.overlaySegments() == 0, "S4: the overlay draws no segments");

    // ---- S6a: both on ----
    model.setSkeletonVisible(true);
    img = render(scene, *engine, view);
    show("S6 mesh on / skeleton on", img);
    const int bothMesh = count(img, isMesh), bothOverlay = count(img, isOverlay);
    CHECK(bothMesh > 10, "S6: the mesh is still there with the skeleton on");
    // Depth-tested bones: the fixture's mesh is a flat strip in z = 0 and the
    // bone octahedron straddles it, so its front half survives. What is NOT
    // claimed any more is that the skeleton draws on top of everything.
    CHECK(bothOverlay > 10, "S6: the part of the skeleton in FRONT of the mesh still draws");
    CHECK(scene.overlaySegments() == 1, "S6: one bone for a two-bone rig (bones - roots)");
    CHECK(scene.overlayStubs() == 1, "S6: the tip bone is a leaf and gets one stub");
    CHECK(scene.overlayJoints() == 2, "S6: one joint marker per distinct joint");

    // ---- S5: THE owner's case — mesh off, skeleton on ----
    model.setMeshVisible(false);
    img = render(scene, *engine, view);
    show("S5 mesh off / skeleton on", img);
    CHECK(count(img, isMesh) == 0, "S5: ZERO mesh pixels with the mesh hidden");
    const int skeletonOnly = count(img, isOverlay);
    CHECK(skeletonOnly > 10, "S5: the bone overlay is on screen by itself");
    // Re-baselined for 3D bones by CAPTURE, not by loosening: 371 px at
    // 160x160 with this fixture, the SAME number on an RTX 4080 SUPER and on
    // lavapipe. The line overlay this replaced drew 178 here, and it FAILS this
    // band (measured, by running this suite against the old overlay) — so the
    // band is a real discriminator, not a rubber stamp. +-20% leaves room for a
    // proportion tweak or a driver's edge rules.
    CHECK(skeletonOnly > 297 && skeletonOnly < 445,
          "S5: the skeleton-only pixel count is in the 3D-bone band (297 < n < 445)");
    // Shape, not just quantity: a cross-section taken 65% of the way down the
    // skeleton — between the joint markers, through the bone BODY — is 6 px of
    // solid bone here and was exactly 1 px with the line overlay (0.35/0.5/0.65
    // measured 2/4/6 for bones against 1/1/1 for lines). The widest run overall
    // is NOT used for this: the old overlay's cube joint markers were as wide
    // as a bone is.
    const int bodyWidth = runAtFraction(img, 0.65f);
    CHECK(bodyWidth >= 3 && bodyWidth <= 20,
          "S5: a bone is a SOLID tapered shape (bone-body cross-section 3-20 px; lines gave 1)");
    CHECK(bothOverlay <= skeletonOnly,
          "S5/S6: with the mesh on, the mesh can only ever HIDE overlay pixels, never add any");
    const float centroid0 = overlayCentroidY(img);

    // ---- S7: the pose reaches the overlay ----
    model.setTime(0.5f);
    img = render(scene, *engine, view);
    show("S7 skeleton only, t = 0.5", img);
    const float centroidHalf = overlayCentroidY(img);
    std::printf("    overlay centroid y: t=0 %.1f   t=0.5 %.1f\n", centroid0, centroidHalf);
    CHECK(count(img, isOverlay) > 10, "S7: still drawn at t = 0.5");
    CHECK(std::fabs(centroidHalf - centroid0) > 1.5f,
          "S7: the bone lines MOVED with the clip (the pose reaches pixels)");
    model.setTime(0.0f);

    // ---- S6b: neither ----
    model.setSkeletonVisible(false);
    img = render(scene, *engine, view);
    show("S6 mesh off / skeleton off", img);
    CHECK(count(img, isMesh) == 0 && count(img, isOverlay) == 0,
          "S6: neither toggle -> background only");

    // ---- avatar.snapshot's engine half ----
    model.setMeshVisible(true);
    model.setSkeletonVisible(false);
    model.setClip("Idle");
    model.setTime(0.0f);
    const QImage shot = scene.renderImage(96, 96);
    CHECK(!shot.isNull() && shot.width() == 96 && shot.height() == 96,
          "snapshot: an offscreen render of the preview scene comes back at the asked size");

    // ---- S9: a SECOND snapshot shows the SECOND pose ----
    // renderImage clears the current view when it is done with its shot view,
    // and step() only mirrors when it has one — so this used to return the
    // first snapshot's pixels forever, whatever the document did in between
    // (the document advances, the engine never hears about it). A page that is
    // not on screen has no other view, which is exactly the scripted /
    // MCP-driven case.
    model.setTime(0.5f);
    const QImage moved = scene.renderImage(96, 96);
    CHECK(!moved.isNull() && moved != shot,
          "S9: a second snapshot at a different time renders a DIFFERENT pose");
    model.setClip("rig2");           // the fixture's second clip, junk-named
    model.setTime(0.25f);
    const QImage switched = scene.renderImage(96, 96);
    CHECK(!switched.isNull() && switched != moved && switched != shot,
          "S9: a snapshot after a CLIP SWITCH renders the new clip's pose");

    // ---- teardown in the documented order ----
    scene.release();
    engine->destroyView(view);

    std::printf(failures ? "\n%d FAILURES\n" : "\nall preview checks passed\n", failures);
    return failures ? 1 : 0;
}
