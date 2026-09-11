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
    volumeCeiling(engine.get(), view);

    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
