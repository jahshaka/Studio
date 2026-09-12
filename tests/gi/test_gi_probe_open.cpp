// gi.probe_open — THE ENCLOSURE CONTRACT: WHICH SCENES GET A PROBE GRID
// (owner decision 2026-09-13 Q3, SPECS/REFLECTION_PROBE_AUDIT.md).
//
// Owner: "a user starts in the editor in a new project with an open scene and
// then builds by adding assets and objects... I would think the sky is your
// first reflection asset." Exactly so, and the engine can MEASURE it rather
// than assume — a reflection probe is a photograph of an enclosure taken from a
// point, so `computeProbeRegion` reads the LAYOUT of the scene's objects: for
// each world axis it finds the outermost SLABS (thin on that axis, broad on the
// other two relative to themselves) either side of the content, fits the probe
// region between their inner faces, and counts the axis ENCLOSED when those two
// face each other across a real gap. Below two enclosed axes there is nothing
// to photograph but the sky, and 18-32 cube captures of the sky cost 128-512 MB
// of probe array, a probe shadow atlas per probe and the per-pixel probe loop,
// to reproduce — with a visible grid seam — what the sky cubemap already holds
// perfectly (the 2026-09-11 lighting audit's finding #4).
//
// THIS SUITE IS THE CONTRACT, as a table. Every case stands on the SAME default
// 100 m ground the editor gives a new project, with NO pinned GI bounds, which
// is the whole point: the ordinary user's room is a few metres of walls on an
// enormous floor, and a measurement that cannot see it is worthless however
// well it does on a test-sized world.
//
//   1. four 10 m walls + a mirror     ENCLOSED, probes built
//   2. four 30 m walls                ENCLOSED
//   3. four walls + a ceiling         ENCLOSED (all three axes)
//   4. ground + props, no walls       OPEN — no probes, and the SKY reflects
//   5. ground + one large object      OPEN
//   6. four corner pillars            OPEN (a pillar is not a slab, at any size)
//
// Cases 7 and 8 of the contract — the Mirror Room's free-standing MirrorPanel
// (defect A1) and the Grand Showroom's columns (defect A2) — live in
// gi.pcc_bounds, which already models both; they are named here so the table is
// readable as a whole.
//
// FAIL-BEFORE, measured on the lane tip before the slab measurement replaced
// the hull one: case 1 reported enclosedAxes 0 and refused the grid. The cause
// was that the enclosure was read off the CONTENT HULL, in which
// `giItemBounds` blends a trimmed outlier's half-size geometrically — the 100 m
// ground put the hull at +-31 m, so a 10 m wall could not "cover half the hull"
// and was never even tested for its outer face. Cases 2 and 6 are the reason
// the obvious repair (build the hull from untrimmed items) was not taken: at
// 30 m walls the ground stops being an outlier at all and the hull becomes
// +-50, so that fix repairs the small room and breaks the large one.
//
// ONE SCENE AT A TIME, always: the HlmsPbs VCT/PCC binding is process-wide
// (OgreGi.cpp sVctBindingOwner), so a second live scene would fight for it.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 8) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }
static void show(const char *what, const Colour &c)
{
    std::printf("   %-34s r=%.4f g=%.4f b=%.4f\n", what, c.r, c.g, c.b);
}

static NodeId addBox(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

/// THE DEFAULT GROUND a new project opens with: 100 m square (the selftest's
/// own floor since the rayontiers merge). Every case in the table stands on it.
static void addDefaultGround(Scene *s)
{
    addBox(s, Colour(0.45f, 0.45f, 0.45f), Vec3(0.0f, -0.1f, 0.0f), Vec3(100.0f, 0.2f, 100.0f));
}

/// Four walls of the given half-span, height and thickness, centred on origin.
static void addWalls(Scene *s, float span, float height, float thick = 0.2f)
{
    const Colour white(0.85f, 0.85f, 0.85f);
    const float y = height * 0.5f, outer = span * 2.0f + thick * 2.0f;
    addBox(s, white, Vec3(-span, y, 0.0f), Vec3(thick, height, outer));
    addBox(s, white, Vec3( span, y, 0.0f), Vec3(thick, height, outer));
    addBox(s, white, Vec3(0.0f, y, -span), Vec3(outer, height, thick));
    addBox(s, white, Vec3(0.0f, y,  span), Vec3(outer, height, thick));
}

/// The sky, in two separately coloured halves so "the sky IBL is bound" is a
/// HUE assertion and not a brightness one: the VISIBLE sky is a blue equirect
/// (what a probe would photograph), the IBL reflection cubemap is GREEN (what a
/// datablock samples when the engine binds the sky cubemap to it). While a
/// probe grid exists the engine UNBINDS that cubemap from every datablock
/// (`reflectionTexForDatablocks`, OgreSky.cpp — the env-probe slot has one
/// occupant), so a mirror shows BLUE through a probe and GREEN through the sky.
static void bindTwoTonedSky(Scene *s)
{
    SkyDesc sky;
    const unsigned char bluePx[4] = { 12, 30, 255, 255 };
    sky.mode = SkyMode::Equirectangular;
    sky.equirect = s->createTexture(1, 1, bluePx, true);
    const unsigned char greenPx[4] = { 20, 255, 40, 255 };
    sky.reflections = true;
    for (int f = 0; f < 6; ++f) sky.reflectionFaces[f] = s->createTexture(1, 1, greenPx, true);
    s->setSky(sky);
}

struct Verdict {
    int  enclosedAxes = 0;
    bool refused = false;
    int  probes = 0;
    Vec3 regionMin, regionMax;
};

static Verdict measure(Engine *engine, Scene *s, int frames = 10)
{
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    gi.updateBudget = 0;                 // deterministic: no re-captures mid-read
    if (!s->setGlobalIllumination(gi))
        std::printf("   engine error: %s\n", engine->lastError().c_str());
    render(engine, frames);
    const GiStatus st = s->giStatus();
    Verdict v;
    v.enclosedAxes = st.probeEnclosedAxes;
    v.refused = st.probeGridRefused;
    v.probes = st.probeCount;
    v.regionMin = st.probeRegionMin;
    v.regionMax = st.probeRegionMax;
    return v;
}

static void report(const char *name, const Verdict &v)
{
    std::printf("-- %s\n   enclosedAxes=%d refused=%s probes=%d  region ["
                "%.2f %.2f %.2f] .. [%.2f %.2f %.2f]\n",
                name, v.enclosedAxes, v.refused ? "true" : "false", v.probes,
                v.regionMin.x, v.regionMin.y, v.regionMin.z,
                v.regionMax.x, v.regionMax.y, v.regionMax.z);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-probe-open-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("open", 128, 128, Colour(0, 0, 0));

    // ---- 1. FOUR 10 m WALLS AND A MIRROR, on the default ground -------------
    // The case the owner's decision was about, and the one the hull measurement
    // could not see. A 10 x 10 room with 4 m walls on a 100 m floor.
    {
        Scene *s = engine->createScene("room10");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f);
        PbrParams mirrorP; mirrorP.albedo = Colour(1, 1, 1);
        mirrorP.metalness = 1.0f; mirrorP.roughness = 0.0f;
        const NodeId mirror = s->createNode();
        s->attachMesh(mirror, s->createMesh(enginetest::unitCubeMesh()),
                      s->createPbrMaterial(mirrorP));
        s->setNodeTransform(mirror, Vec3(0.0f, 1.4f, 0.0f), Quat(), Vec3(1.8f, 1.8f, 1.8f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 5.0f), Vec3(0.0f, 1.4f, 0.0f));

        const Verdict v = measure(engine.get(), s);
        report("1. four 10 m walls + a mirror, default 100 m ground", v);
        CHECK(v.enclosedAxes >= 2, "1: a room on the default ground measures ENCLOSED");
        CHECK(!v.refused && v.probes == 4, "1: ...so the probe grid is built");
        // The region is the ROOM's interior, not the floor's empty acres — the
        // other half of the same reading (A1/A2's shrink-fit pathology).
        CHECK(v.regionMin.x > -6.0f && v.regionMax.x < 6.0f &&
              v.regionMin.z > -6.0f && v.regionMax.z < 6.0f,
              "1: ...and the probe region is the ROOM, not the 100 m floor");
        CHECK(v.regionMin.y > -0.5f && v.regionMin.y < 0.5f,
              "1: ...standing on the ground's top face");
        engine->destroyScene(s);
    }

    // ---- 2. FOUR 30 m WALLS -------------------------------------------------
    // The case that kills the obvious repair: at this size the ground is no
    // longer an outlier at all, so an untrimmed hull is +-50 and 30 m walls
    // still could not "cover half" of it.
    {
        Scene *s = engine->createScene("room30");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 15.0f, 6.0f, 0.4f);
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("2. four 30 m walls, default 100 m ground", v);
        CHECK(v.enclosedAxes >= 2, "2: a 30 m room measures ENCLOSED too (scale-free)");
        CHECK(!v.refused && v.probes == 4, "2: ...so the probe grid is built");
        engine->destroyScene(s);
    }

    // ---- 3. FOUR WALLS AND A CEILING ---------------------------------------
    {
        Scene *s = engine->createScene("roofed");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f);
        addBox(s, Colour(0.85f, 0.85f, 0.85f), Vec3(0.0f, 4.1f, 0.0f), Vec3(10.4f, 0.2f, 10.4f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("3. four walls + a ceiling", v);
        CHECK(v.enclosedAxes == 3, "3: a closed room encloses all THREE axes");
        CHECK(!v.refused && v.probes == 4, "3: ...so the probe grid is built");
        CHECK(v.regionMax.y < 4.2f && v.regionMin.y > -0.5f,
              "3: ...and the region is floor-to-ceiling, not floor-to-nothing");
        engine->destroyScene(s);
    }

    // ---- 4. GROUND AND PROPS, NO WALLS: THE OPEN SCENE ---------------------
    // The new project the owner described. No probes, and the SKY is the
    // reflection — asserted in pixels, not just in the status struct.
    {
        Scene *s = engine->createScene("open");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        bindTwoTonedSky(s);
        addDefaultGround(s);
        addBox(s, Colour(0.8f, 0.3f, 0.2f), Vec3(-3.0f, 0.8f, -2.0f), Vec3(1.6f, 1.6f, 1.6f));
        addBox(s, Colour(0.2f, 0.8f, 0.3f), Vec3( 3.2f, 1.2f,  1.5f), Vec3(1.2f, 2.4f, 1.2f));
        PbrParams mirrorP; mirrorP.albedo = Colour(1, 1, 1);
        mirrorP.metalness = 1.0f; mirrorP.roughness = 0.0f;
        const NodeId mirror = s->createNode();
        s->attachMesh(mirror, s->createMesh(enginetest::unitCubeMesh()),
                      s->createPbrMaterial(mirrorP));
        s->setNodeTransform(mirror, Vec3(0.0f, 1.4f, 0.0f), Quat(), Vec3(1.8f, 1.8f, 1.8f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        // Looking slightly DOWN at the box's +Z face, so its reflection vector
        // points up past the camera into the sky.
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 5.0f), Vec3(0.0f, 1.4f, 0.0f));

        const Verdict v = measure(engine.get(), s);
        report("4. ground + props, no walls", v);
        CHECK(v.enclosedAxes < 2, "4: an open scene measures OPEN");
        CHECK(v.refused, "4: ...so the renderer declines the probe grid, and says so");
        CHECK(v.probes == 0, "4: ...and no probes exist");

        const GiStatus st = s->giStatus();
        CHECK(!st.pccBound, "4: nothing is bound as a probe grid");
        CHECK(st.probeCaptureSize == 0, "4: and there is no capture size to report");
        CHECK(st.vctBound, "4: the voxel half is untouched — cone tracing still carries the bounce");

        Image img;
        view->readPixels(img);
        const Colour openMirror = img.at(64, 70);
        show("mirror box, open scene", openMirror);
        CHECK(openMirror.g > 0.25f && openMirror.g > openMirror.b + 0.15f,
              "4: the mirror reflects the GREEN IBL cubemap — the sky IS the open scene's\n"
              "          reflection source and it is BOUND (a probe capture would show blue)");
        engine->destroyScene(s);
    }

    // ---- 5. GROUND AND ONE LARGE OBJECT ------------------------------------
    // An imported car, parked on the ground. Big, and no kind of room.
    {
        Scene *s = engine->createScene("car");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addBox(s, Colour(0.2f, 0.25f, 0.6f), Vec3(0.0f, 0.7f, 0.0f), Vec3(4.5f, 1.4f, 1.9f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("5. ground + one large object (a car)", v);
        CHECK(v.enclosedAxes < 2, "5: one big object is not a room");
        CHECK(v.refused && v.probes == 0, "5: ...so no probes, and the sky reflects");
        engine->destroyScene(s);
    }

    // ---- 6. FOUR CORNER PILLARS --------------------------------------------
    // Four objects as big as the walls of case 1, in the same four places, and
    // not a room: a pillar is not a slab at any scale, so it never takes part
    // in the measurement at all.
    {
        Scene *s = engine->createScene("pillars");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        for (int i = 0; i < 4; ++i) {
            const float x = (i & 1) ? 4.6f : -4.6f, z = (i & 2) ? 4.6f : -4.6f;
            addBox(s, Colour(0.85f, 0.85f, 0.85f), Vec3(x, 2.0f, z), Vec3(0.8f, 4.0f, 0.8f));
        }
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("6. four corner pillars", v);
        CHECK(v.enclosedAxes < 2, "6: pillars enclose nothing");
        CHECK(v.refused && v.probes == 0, "6: ...so no probes, and the sky reflects");
        engine->destroyScene(s);
    }

    // ---- THE ESCAPE HATCH --------------------------------------------------
    // A scene that has TYPED its lit volume has stated where the space is, and
    // the measurement stands down — the documented remedy for the one case it
    // provably cannot make (a room imported as one hollow mesh). Case 4's
    // geometry, with bounds pinned.
    {
        Scene *s = engine->createScene("pinned");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addBox(s, Colour(0.8f, 0.3f, 0.2f), Vec3(-3.0f, 0.8f, -2.0f), Vec3(1.6f, 1.6f, 1.6f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        GiParams gi;
        gi.mode = GiMode::VctPccHybrid;
        gi.quality = GiQuality::Medium;
        gi.numBounces = 1;
        gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
        gi.updateBudget = 0;
        gi.boundsMin = Vec3(-6.0f, -0.4f, -6.0f);
        gi.boundsMax = Vec3( 6.0f,  5.0f,  6.0f);
        CHECK(s->setGlobalIllumination(gi), "pinned: the hybrid builds");
        render(engine.get(), 10);
        const GiStatus st = s->giStatus();
        std::printf("-- pinned bounds, open geometry --\n   enclosedAxes=%d refused=%s probes=%d\n",
                    st.probeEnclosedAxes, st.probeGridRefused ? "true" : "false", st.probeCount);
        CHECK(st.probeEnclosedAxes < 2, "pinned: the geometry still measures OPEN");
        CHECK(!st.probeGridRefused && st.probeCount == 4,
              "pinned: ...and the grid is built anyway — typed bounds stand the rule down");
        CHECK(st.probeCaptureSize == 256, "pinned: Medium and High both capture at 256 px");
        engine->destroyScene(s);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
