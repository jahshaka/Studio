// gi.hidden_volume — TWO statements about what the automatic lit volume is
// allowed to contain, both of them owner-reported smoke defects.
//
// 1. S12: A HIDDEN OBJECT STOPS LIGHTING. The owner hid a cube in the outliner
//    and its "massive grey lighting effect" stayed on the floor. `setNodeVisible`
//    only toggled Ogre's LAYER_VISIBILITY bit, which `MovableObject::
//    getVisibilityFlags()` masks off before returning — so every GI gather's
//    `flags & kGiGeometryBit` test was blind to it, the hidden cube stayed
//    voxelised, it went on defining the automatic volume, and nothing
//    invalidated the caches. Measured before the fix: hiding a cube at
//    (0, 20, 40) left the lit volume at 4.0 x 22.0 x 63.2, unchanged.
//
// 2. S14: THE AUTOMATIC VOLUME HAS A CEILING, expressed in METRES PER VOXEL.
//    The default project is one 1024 m ground plane at Epic, and `giItemBounds`
//    cannot trim a single item (the geometric mean of one extent is that
//    extent, so its ramp is a no-op by construction) — so the engine voxelised
//    a square kilometre at 128^3: 8.1 m voxels, a DDGI field two probes tall,
//    eighteen reflection probes 346 m apart. `GiParams::autoBoundsMax` caps the
//    largest axis at 64 m, centred on the scene's content.
//
// 1b. (RENDER_PIPELINE_AUDIT 1.1/1.2) The same for a hidden PARENT: its
//    children leave GI with it, and showing it restores each child to its own
//    flag — a child the user hid stays hidden.
//
// Its own binary like every GI suite: GI binds process-wide HlmsPbs state.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void showBox(const char *what, const GiStatus &st)
{
    std::printf("   %-34s %7.2f x %7.2f x %7.2f   (voxel %.3f m)\n", what,
                st.boundsMax.x - st.boundsMin.x, st.boundsMax.y - st.boundsMin.y,
                st.boundsMax.z - st.boundsMin.z, st.voxelMetres);
}

// ---------------------------------------------------------------------------
// 1. S12 — hiding lit geometry takes its bounce AND its extent away.
// ---------------------------------------------------------------------------
static void hiddenGeometry(Engine *engine, View *view)
{
    std::printf("-- S12: a hidden object stops bouncing light\n");
    Scene *s = engine->createScene("gi_hidden");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // A white floor, and a strongly red panel standing over one end of it. The
    // light grazes the floor and hits the panel square on, so the only thing
    // that can tint the floor red is the panel's bounce — the same geometry
    // gi.modes uses, with the panel made hideable.
    const NodeId floor = enginetest::addTestCube(s, Colour(1.0f, 1.0f, 1.0f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(s, floor, Vec3(14.0f, 0.1f, 14.0f));

    const NodeId panel = enginetest::addTestCube(s, Colour(1.0f, 0.05f, 0.05f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, panel, Vec3(0.0f, 3.0f, -3.0f));
    enginetest::setNodeScale(s, panel, Vec3(12.0f, 6.0f, 0.9f));

    const NodeId lightNode = s->createNode();
    const float half = 40.0f * 3.14159265f / 180.0f;
    const Quat atPanel(std::sin(half), 0.0f, 0.0f, std::cos(half));
    s->setNodeTransform(lightNode, Vec3(0, 6, 6), atPanel, Vec3(1, 1, 1));
    LightDesc light;
    light.type = LightType::Directional;
    light.colour = Colour(1, 1, 1);
    light.intensity = 2.0f;
    light.castShadows = false;
    s->setLight(lightNode, light);

    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 6.0f), Vec3(0.0f, 0.0f, -0.5f));

    // THE REFERENCE IS THE SCENE WITHOUT THE PANEL, taken before GI exists at
    // all, so "the ground returns to its no-panel lighting" is measured against
    // the real thing rather than against an argument.
    s->setNodeVisible(panel, false);
    render(engine);
    Image img;
    view->readPixels(img);
    const unsigned fx = 64, fy = 96;
    const Colour noPanel = img.at(fx, fy);
    s->setNodeVisible(panel, true);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    CHECK(s->setGlobalIllumination(gi), "VCT arms");
    render(engine, 4);
    view->readPixels(img);
    const Colour withPanel = img.at(fx, fy);
    const GiStatus visibleSt = s->giStatus();
    showBox("volume, panel visible", visibleSt);
    std::printf("   floor no-panel  r=%.3f g=%.3f | panel visible r=%.3f g=%.3f\n",
                noPanel.r, noPanel.g, withPanel.r, withPanel.g);
    const float bounceOn = (withPanel.r - withPanel.g) - (noPanel.r - noPanel.g);
    CHECK(bounceOn > 0.02f, "the visible panel bounces red onto the floor");

    // HIDE IT. The engine must drop kGiGeometryBit and invalidate; the mirror's
    // stability window is a host concern, so the suite asks for the re-solve
    // the same way world.refreshGi() does.
    s->setNodeVisible(panel, false);
    s->refreshGlobalIllumination();
    render(engine, 4);
    view->readPixels(img);
    const Colour hidden = img.at(fx, fy);
    const GiStatus hiddenSt = s->giStatus();
    showBox("volume, panel hidden", hiddenSt);
    std::printf("   floor panel hidden r=%.3f g=%.3f\n", hidden.r, hidden.g);
    const float bounceOff = (hidden.r - hidden.g) - (noPanel.r - noPanel.g);
    CHECK(std::fabs(bounceOff) < 0.01f,
          "hiding the panel returns the floor's red bounce to its no-panel value");
    // ...and the volume no longer stretches to it. The panel spans y 0..6 and
    // z -3.45..-2.55; with it hidden the floor alone is 0.1 m thick.
    CHECK(hiddenSt.boundsMax.y - hiddenSt.boundsMin.y <
              (visibleSt.boundsMax.y - visibleSt.boundsMin.y) * 0.5f,
          "the automatic volume no longer contains the hidden panel");

    // SHOWING IT AGAIN PUTS IT BACK — the edge is symmetric, and this is what
    // proves the fix is a mask rather than a one-way delete.
    s->setNodeVisible(panel, true);
    s->refreshGlobalIllumination();
    render(engine, 4);
    view->readPixels(img);
    const Colour shown = img.at(fx, fy);
    std::printf("   floor panel shown again r=%.3f g=%.3f\n", shown.r, shown.g);
    CHECK(std::fabs((shown.r - shown.g) - (withPanel.r - withPanel.g)) < 0.02f,
          "showing the panel again restores its bounce");

    GiParams off;
    s->setGlobalIllumination(off);
    render(engine, 2);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// 1b. RENDER_PIPELINE_AUDIT 1.1/1.2 (lane L12) — a hidden PARENT hides its
//     children from GI too, and showing it again restores each child to its
//     OWN state.
//
// The S12 fix above dropped the GI bit of the node hidden DIRECTLY and nothing
// else: setNodeVisible used Ogre's setVisible cascade for the picture but
// recomputed kGiGeometryBit for that one node, so hiding an imported model by
// its root — the common case, a model roots at an Empty — hid it on screen and
// left every part voxelised, bouncing light and defining the volume (measured:
// bounds unchanged). The same cascade on the way back set EVERY descendant
// visible, re-drawing a child the user had hidden himself.
//
// Run at the Epic tier's GI (hybrid, High, three bounces, the irradiance field
// on, two dynamic probes a frame — worldmodes.cpp kRayonTable), because that is
// what a new project renders with.
// ---------------------------------------------------------------------------
static NodeId addCubeUnder(Scene *s, NodeId parent, const Colour &albedo)
{
    const NodeId node = s->createNode(parent);
    if (!node) return 0;
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.9f;
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    return node;
}

static bool isGreen(const Colour &c) { return c.g > 0.15f && c.g > c.r * 2.0f && c.g > c.b * 2.0f; }

static void hiddenParent(Engine *engine, View *view)
{
    std::printf("-- 1.1/1.2: a hidden PARENT takes its children out of GI; showing it keeps a hidden child hidden\n");
    Scene *s = engine->createScene("gi_hidden_parent");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const NodeId floor = enginetest::addTestCube(s, Colour(1.0f, 1.0f, 1.0f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(s, floor, Vec3(14.0f, 0.1f, 14.0f));

    // THE MODEL: an empty root with two parts. The red panel is section 1's
    // bouncer; the green pillar is TALL (y 0..10, the panel stops at 6), so
    // whether it is in the lit volume reads straight off the bounds, and it
    // stands in front of the panel where the camera sees it.
    const NodeId root = s->createNode();
    const NodeId panel = addCubeUnder(s, root, Colour(1.0f, 0.05f, 0.05f));
    enginetest::setNodePosition(s, panel, Vec3(0.0f, 3.0f, -3.0f));
    enginetest::setNodeScale(s, panel, Vec3(12.0f, 6.0f, 0.9f));
    const NodeId pillar = addCubeUnder(s, root, Colour(0.05f, 1.0f, 0.05f));
    enginetest::setNodePosition(s, pillar, Vec3(2.5f, 5.0f, -1.5f));
    enginetest::setNodeScale(s, pillar, Vec3(1.0f, 10.0f, 1.0f));
    CHECK(root && panel && pillar, "an empty root with two lit parts");

    const NodeId lightNode = s->createNode();
    const float half = 40.0f * 3.14159265f / 180.0f;
    s->setNodeTransform(lightNode, Vec3(0, 6, 6),
                        Quat(std::sin(half), 0.0f, 0.0f, std::cos(half)), Vec3(1, 1, 1));
    LightDesc light;
    light.type = LightType::Directional;
    light.colour = Colour(1, 1, 1);
    light.intensity = 2.0f;
    light.castShadows = false;
    s->setLight(lightNode, light);

    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 6.0f), Vec3(0.0f, 0.0f, -0.5f));
    const unsigned fx = 64, fy = 96;      // the floor, between the camera and the panel
    const unsigned px = 114, py = 28;     // the pillar's lit front face, low down

    render(engine);
    Image img;
    view->readPixels(img);
    std::printf("   pillar probe, everything shown  r=%.3f g=%.3f b=%.3f\n",
                img.at(px, py).r, img.at(px, py).g, img.at(px, py).b);
    CHECK(isGreen(img.at(px, py)), "the pillar probe pixel is ON the pillar");

    // The user hides the PILLAR ITSELF — its own flag, the state that must
    // survive everything its parent does below.
    s->setNodeVisible(pillar, false);

    // THE REFERENCE: the floor with the whole model gone, before GI exists.
    s->setNodeVisible(root, false);
    render(engine);
    view->readPixels(img);
    const Colour noModel = img.at(fx, fy);
    s->setNodeVisible(root, true);
    render(engine);
    view->readPixels(img);
    std::printf("   pillar probe after root hide/show r=%.3f g=%.3f b=%.3f\n",
                img.at(px, py).r, img.at(px, py).g, img.at(px, py).b);
    CHECK(!isGreen(img.at(px, py)),
          "showing the root does NOT re-draw the pillar the user hid itself");

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::High;
    gi.numBounces = 3;
    gi.ddgi = GiToggle::On;
    CHECK(s->setGlobalIllumination(gi), "Epic's GI arms (hybrid, High, 3 bounces, field on)");
    render(engine, 6);
    view->readPixels(img);
    const Colour withModel = img.at(fx, fy);
    const GiStatus shownSt = s->giStatus();
    showBox("volume, root shown", shownSt);
    std::printf("   floor no-model r=%.3f g=%.3f | root shown r=%.3f g=%.3f (ifd converged %d)\n",
                noModel.r, noModel.g, withModel.r, withModel.g, int(shownSt.ifdConverged));
    const float bounceOn = (withModel.r - withModel.g) - (noModel.r - noModel.g);
    // THRESHOLD MOVED 0.02 -> 0.008 WITH A VERDICT (2026-09-13 reflection-probe
    // lane, owner decision Q3). This case cannot pin its lit volume — the
    // assertion four lines below is that the AUTO volume excludes the hidden
    // pillar — and the hybrid now declines to build a probe grid in a scene it
    // measures as OPEN, which this floor-and-panel scene is. So the red this
    // floor gains from the shown model is the DIFFUSE half only (cone tracing
    // and the irradiance field) where it used to be diffuse plus the probes'
    // specular. Measured here, same scene, same frame count: 0.015 (r-g gain 0.439-0.424 against a 0.353/0.353 floor),
    // where the pre-decision reading with the probes alive was 0.180. The
    // contract is unchanged — showing a model must add its bounce and hiding it
    // must take it away, which the two assertions after this still pin at the
    // same strength — only the magnitude the owner's decision left behind is.
    CHECK(bounceOn > 0.008f, "the shown model's panel bounces red onto the floor");
    CHECK(shownSt.boundsMax.y - shownSt.boundsMin.y < 8.5f,
          "the pillar the user hid is not in the lit volume (y 0..10 would be)");

    // HIDE THE ROOT. Every part must leave GI with it, not just the root.
    s->setNodeVisible(root, false);
    s->refreshGlobalIllumination();
    render(engine, 6);
    view->readPixels(img);
    const Colour hidden = img.at(fx, fy);
    const GiStatus hiddenSt = s->giStatus();
    showBox("volume, root hidden", hiddenSt);
    std::printf("   floor root hidden r=%.3f g=%.3f\n", hidden.r, hidden.g);
    const float bounceOff = (hidden.r - hidden.g) - (noModel.r - noModel.g);
    CHECK(std::fabs(bounceOff) < 0.01f,
          "hiding the ROOT returns the floor's red bounce to its no-model value");
    CHECK(hiddenSt.boundsMax.y - hiddenSt.boundsMin.y <
              (shownSt.boundsMax.y - shownSt.boundsMin.y) * 0.5f,
          "the automatic volume no longer contains the hidden root's parts");

    // SHOW IT AGAIN: the panel comes back, the pillar stays the user's hidden.
    s->setNodeVisible(root, true);
    s->refreshGlobalIllumination();
    render(engine, 6);
    view->readPixels(img);
    const Colour shown = img.at(fx, fy);
    const GiStatus againSt = s->giStatus();
    showBox("volume, root shown again", againSt);
    std::printf("   floor root shown again r=%.3f g=%.3f | pillar probe r=%.3f g=%.3f b=%.3f\n",
                shown.r, shown.g, img.at(px, py).r, img.at(px, py).g, img.at(px, py).b);
    CHECK(std::fabs((shown.r - shown.g) - (withModel.r - withModel.g)) < 0.02f,
          "showing the root restores the panel's bounce");
    CHECK(!isGreen(img.at(px, py)), "...and the pillar the user hid is STILL not drawn");
    CHECK(againSt.boundsMax.y - againSt.boundsMin.y < 8.5f,
          "...nor back in the lit volume");

    // Showing the pillar itself is the one thing that brings it back.
    s->setNodeVisible(pillar, true);
    s->refreshGlobalIllumination();
    render(engine, 6);
    view->readPixels(img);
    const GiStatus pillarSt = s->giStatus();
    showBox("volume, pillar shown", pillarSt);
    CHECK(isGreen(img.at(px, py)) && pillarSt.boundsMax.y - pillarSt.boundsMin.y > 9.5f,
          "showing the pillar itself draws it and puts it in the volume");

    GiParams off;
    s->setGlobalIllumination(off);
    render(engine, 2);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

// ---------------------------------------------------------------------------
// 2. S14 — the automatic volume's metres-per-voxel ceiling.
// ---------------------------------------------------------------------------
static void volumeCeiling(Engine *engine, View *view)
{
    std::printf("-- S14: the automatic volume is capped in metres per voxel\n");
    Scene *s = engine->createScene("gi_ceiling");
    view->setScene(s);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.05f, 0.05f, 0.06f));

    // THE DEFAULT PROJECT'S SHAPE: one enormous ground plane and a directional
    // light, and nothing else. This is the scene the ceiling exists for.
    const NodeId ground = enginetest::addTestCube(s, Colour(0.8f, 0.8f, 0.8f), 0.0f, 1.0f);
    enginetest::setNodePosition(s, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(s, ground, Vec3(1024.0f, 0.1f, 1024.0f));

    const NodeId lightNode = s->createNode();
    const float half = 35.0f * 3.14159265f / 180.0f;
    s->setNodeTransform(lightNode, Vec3(0, 10, 0),
                        Quat(std::sin(half), 0.0f, 0.0f, std::cos(half)), Vec3(1, 1, 1));
    LightDesc light;
    light.type = LightType::Directional;
    light.colour = Colour(1, 1, 1);
    // DIM ON PURPOSE. The probes below compare the ground's luminance across
    // the frame, and a ground clipped at 1.0 is trivially "smooth" — the
    // assertion would pass on any volume at all. 0.35 puts the plane in the
    // middle of the range where the cell steps were visible.
    light.intensity = 0.35f;
    light.castShadows = false;
    s->setLight(lightNode, light);

    // Looking along the ground from a metre up, so several volume cells would
    // be in frame at once if the volume were the whole plane — the owner's
    // "huge light/dark lighting tiles when zoomed out".
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 12.0f), Vec3(0.0f, 0.0f, -20.0f));

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;      // 128^3, the Epic tier's resolution
    gi.numBounces = 3;
    CHECK(s->setGlobalIllumination(gi), "VCT arms at High over a 1024 m plane");
    render(engine, 4);
    GiStatus st = s->giStatus();
    showBox("1024 m plane, ceiling on", st);
    const float cap = 64.0f;           // GiParams::autoBoundsMax's default
    const float edge = std::max(std::max(st.boundsMax.x - st.boundsMin.x,
                                         st.boundsMax.y - st.boundsMin.y),
                                st.boundsMax.z - st.boundsMin.z);
    // The one-voxel margin computeGiBounds adds rides on top of the ceiling.
    CHECK(edge <= cap * 1.05f, "the automatic volume's largest axis is inside the ceiling");
    CHECK(st.voxelMetres > 0.0f && st.voxelMetres <= 0.55f,
          "the volume resolves to about half a metre per voxel");

    // THE GROUND IS SMOOTHLY LIT ACROSS THE VIEW: five probes along the
    // horizontal centre band of the floor, which is where the owner's cell
    // steps appeared. A RELATIVE statement (the spread against the mean), so it
    // survives exposure and tonemap changes that move every probe together.
    //
    // HOW STRONG THIS ASSERTION IS, PLAINLY: it is a guard against a gross
    // regression, NOT a reproduction of the owner's screenshot. Measured both
    // ways on this scene, the ceiling-off volume (8.1 m voxels) probes just as
    // flat as the capped one — an untextured plane under one directional light
    // and a uniform ambient has nothing for the voxel/probe cells to vary, with
    // or without DDGI and the probe grid (tried; both read 0.0% spread). The
    // sharp gate for S14 is the VOLUME BOUND above: 0.508 m per voxel against
    // the 8.125 the same scene resolves to with the ceiling off.
    Image img;
    view->readPixels(img);
    const unsigned y = 100;
    float lo = 1e9f, hi = -1e9f, sum = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const unsigned x = unsigned(14 + i * 25);
        const Colour c = img.at(x, y);
        const float l = (c.r + c.g + c.b) / 3.0f;
        std::printf("   ground probe x=%3u  luma %.4f\n", x, l);
        lo = std::min(lo, l); hi = std::max(hi, l); sum += l;
    }
    const float mean = sum / 5.0f;
    std::printf("   spread %.4f over mean %.4f (%.1f%%)\n", hi - lo, mean,
                mean > 0.0f ? 100.0f * (hi - lo) / mean : 0.0f);
    CHECK(mean > 0.02f && mean < 0.9f,
          "the ground is lit and NOT clipped (a clipped ground is trivially smooth)");
    CHECK(hi - lo < mean * 0.25f, "the ground's lighting is smooth across the view");

    // THE KNOB. 0 removes the ceiling, and the volume goes back to the plane —
    // which is also the proof that the assertion above is measuring the ceiling
    // and not some other clamp.
    gi.autoBoundsMax = 0.0f;
    CHECK(s->setGlobalIllumination(gi), "autoBoundsMax = 0 re-pushes");
    render(engine, 4);
    st = s->giStatus();
    showBox("1024 m plane, ceiling off", st);
    CHECK(st.boundsMax.x - st.boundsMin.x > 1000.0f,
          "with the ceiling off the volume is the whole plane again");
    CHECK(st.voxelMetres > 4.0f, "...at metres per voxel (what the ceiling exists to stop)");

    // A PINNED VOLUME IGNORES THE CEILING: that is how a scene bigger than the
    // cap asks for more, and it must not be quietly overruled.
    gi.autoBoundsMax = 64.0f;
    gi.boundsMin = Vec3(-150.0f, -10.0f, -150.0f);
    gi.boundsMax = Vec3(150.0f, 10.0f, 150.0f);
    CHECK(s->setGlobalIllumination(gi), "an explicit volume re-pushes");
    render(engine, 4);
    st = s->giStatus();
    showBox("explicit 300 m volume", st);
    CHECK(st.boundsMax.x - st.boundsMin.x > 290.0f,
          "a pinned volume is not clamped by the ceiling");

    GiParams off;
    s->setGlobalIllumination(off);
    render(engine, 2);
    view->setScene(nullptr);
    engine->destroyScene(s);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-hidden-volume-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("gi_hidden_volume", 128, 128, Colour(0, 0, 0));

    hiddenGeometry(engine.get(), view);
    hiddenParent(engine.get(), view);
    volumeCeiling(engine.get(), view);

    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
