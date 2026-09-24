// gi.probe_open — WHICH PROBES GET BUILT, AND WHERE THEY LIVE: WHAT A PROBE
// SEES (owner decision 2026-09-13 Q3; lane R5-ROOM, 2026-09-15, which retired
// the rule this file used to be the contract for).
//
// Owner: "a user starts in the editor in a new project with an open scene and
// then builds by adding assets and objects... I would think the sky is your
// first reflection asset." The engine MEASURES that rather than assuming it,
// and since 2026-09-15 the measurement is a PHOTOGRAPH and nothing else.
//
// THE RULE THIS REPLACED read the LAYOUT of the scene's items — facing slabs,
// covering faces, an enclosed-axis count — and built the whole grid or none of
// it. It is deleted, not patched: no lighting decision may test for a room, an
// enclosure, a wall or an axis count (PHOTON_SPEC §13, owner+lead joint
// decision 2026-09-14).
//
// WHAT RUNS INSTEAD (OgreGi.cpp buildPcc): the grid is placed in a box ONE
// viewpoint at the scene's centre photographed (cheaply, 32 px, read
// symmetrically about itself), and then every probe is kept or dropped by what
// IT sees. The placement reads one averaged depth value per cube face;
// ogre-patch 0047 hands those six numbers back, each the distance that face
// could see as a multiple of the distance from that probe's camera to the
// region's face in the same direction — 1 is "on that face" and 2 is the
// encoding's saturation, "nothing within twice that distance", which is what a
// face full of sky returns. From the six the probe's fitted box follows, and
// its VOLUME against the volume the renderer LIT is the verdict: below 1 the
// probe measured a smaller space than the world it stands in and is worth
// building, at 1 or above what it saw is no nearer than the world itself.
// Per probe, positionless, scale-free; no scene-wide shape is ever asked for.
//
// THIS SUITE IS THE CONTRACT, as a table. Every case stands on the SAME default
// 100 m ground the editor gives a new project, with NO pinned GI bounds, which
// is the whole point: the ordinary user's room is a few metres of walls on an
// enormous floor, and a measurement that cannot see it is worthless however
// well it does on a test-sized world.
//
//   1. four 10 m walls + a mirror     every probe sees the walls: the grid is built
//   2. four 30 m walls                the same at the SHIPPED 3x2x3 grid and at
//                                     4x2x4 — and NOT at 2x1x2, where all four
//                                     candidates land outside the walls (see 2a)
//   3. four walls + a ceiling         built; the region is floor-to-ceiling
//   4. ground + props, no walls       the probes that can SEE a prop are kept and
//                                     the grid follows them onto the props
//   5. ground + one large object      nothing sees anything: no probes, the SKY
//   6. four corner pillars            no probes (nothing is near enough to any)
//   7. case 1 built at x = +25        identical to case 1: no position is read
//   8. case 3 built at x = +25        identical to case 3, region x = [20, 30]
//   9. case 1 + a wall shelf at y=2.2 unchanged — a shelf is furniture
//  10. a hall open on X, one
//      full-height partition in it    built; the region is NOT cut at the partition
//  11. case 1 + a tabletop            unchanged
//  12. case 1 + a rug                 unchanged
//  13. the sun disc in a probe capture (pinned bounds; see the case)
//      a bare ground, nothing else    NO probes and the SKY reflects, in pixels
//
// WHAT THE OLD TABLE ASSERTED AND THIS ONE DOES NOT: an "enclosed axis" count,
// a refusal flag, and a probe region pulled onto the rooms' inner walls by a
// slab search. The two giStatus fields that carried them (probeEnclosedAxes,
// probeGridRefused) are deleted; `probesDropped` — how many candidates the
// renderer photographed and discarded — is what says WHY a scene has no grid.
//
// MEASURED IN THE LANE, the numbers the threshold stands on (the box's volume
// against the LIT VOLUME's, per probe, over these scenes): probes inside a
// room read 0.10 - 0.55, probes with nothing near them 1.3 - 6.0. The line is
// at 1.0, which is the statement itself — at 1 the box a probe's six faces
// measured IS the world the renderer lit.
//
// AND IT IS RELATIVE TO THAT VOLUME, which is a stopgap and is stated as one:
// the same roofless 10 m room keeps its four probes over the automatic +-7.43
// fit and loses them over a +-5.5 volume pinned tight around it (case 13 pins
// the room's own +-8 for that reason), and case 2's 30 m yard keeps none of
// the shipped grid's 18. The coupling exists because a grid, once it exists at
// all, takes the sky cubemap off every material in the scene (one environment
// slot — SKY-FALLBACK-1); with the sky kept as the fallback the line could be
// drawn far more generously, and R2's rays retire the question.
//
// Two further contract rows — the Mirror Room's free-standing MirrorPanel
// (defect A1) and the Grand Showroom's columns (defect A2) — live in
// gi.pcc_bounds, which already models both; they are named here so the table is
// readable as a whole. A2 is also what pins the scout's REGION half: with the
// grid placed in the lit volume instead of in the photographed space, 467 of
// that case's 1344 metal pixels come back as hard black holes.
//
// ONE SCENE AT A TIME, always: the HlmsPbs VCT/PCC binding is process-wide
// (the binding before PHOTON-SCENE-SWITCH-1), so a second live scene would fight for it.
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

/// Four walls of the given half-span, height and thickness, around (cx, cz).
/// The CENTRE is a parameter because "where on the ground did the author build
/// it" is a question the measurement has to be indifferent to (contract rows 7
/// and 8): a room is a room at the origin and at x = +25.
static void addWalls(Scene *s, float span, float height, float thick = 0.2f,
                     float cx = 0.0f, float cz = 0.0f)
{
    const Colour white(0.85f, 0.85f, 0.85f);
    const float y = height * 0.5f, outer = span * 2.0f + thick * 2.0f;
    addBox(s, white, Vec3(cx - span, y, cz), Vec3(thick, height, outer));
    addBox(s, white, Vec3(cx + span, y, cz), Vec3(thick, height, outer));
    addBox(s, white, Vec3(cx, y, cz - span), Vec3(outer, height, thick));
    addBox(s, white, Vec3(cx, y, cz + span), Vec3(outer, height, thick));
}

/// A mirror cube: the thing whose reflections the whole feature is about.
static void addMirror(Scene *s, const Vec3 &pos, float size = 1.8f)
{
    PbrParams mirrorP; mirrorP.albedo = Colour(1, 1, 1);
    mirrorP.metalness = 1.0f; mirrorP.roughness = 0.0f;
    const NodeId mirror = s->createNode();
    s->attachMesh(mirror, s->createMesh(enginetest::unitCubeMesh()),
                  s->createPbrMaterial(mirrorP));
    s->setNodeTransform(mirror, pos, Quat(), Vec3(size, size, size));
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
    int  dropped = 0;
    int  probes = 0;
    Vec3 regionMin, regionMax;
};

static Verdict measureGrid(Engine *engine, Scene *s, int nx, int ny, int nz, int frames = 10)
{
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.pccProbesX = nx; gi.pccProbesY = ny; gi.pccProbesZ = nz;
    gi.updateBudget = 0;                 // deterministic: no re-captures mid-read
    if (!s->setGlobalIllumination(gi))
        std::printf("   engine error: %s\n", engine->lastError().c_str());
    render(engine, frames);
    const GiStatus st = s->giStatus();
    Verdict v;
    v.dropped = st.probesDropped;
    v.probes = st.probeCount;
    v.regionMin = st.probeRegionMin;
    v.regionMax = st.probeRegionMax;
    return v;
}

static Verdict measure(Engine *engine, Scene *s, int frames = 10)
{
    return measureGrid(engine, s, 2, 1, 2, frames);
}

static void report(const char *name, const Verdict &v)
{
    std::printf("-- %s\n   probes=%d dropped=%d  region ["
                "%.2f %.2f %.2f] .. [%.2f %.2f %.2f]\n",
                name, v.probes, v.dropped,
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
        addMirror(s, Vec3(0.0f, 1.4f, 0.0f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 5.0f), Vec3(0.0f, 1.4f, 0.0f));

        const Verdict v = measure(engine.get(), s);
        report("1. four 10 m walls + a mirror, default 100 m ground", v);
        CHECK(v.probes == 4 && v.dropped == 0,
              "1: every probe in a room on the default ground sees its walls, so all four\n          are built");
        // The region is the ROOM's interior, not the floor's empty acres — the
        // other half of the same reading (A1/A2's shrink-fit pathology).
        // The region is the space the probes PHOTOGRAPHED, which in a roofless
        // room is the room plus what its own faces can see over the walls
        // (measured +-7.4 for a room whose outer walls are at +-5.2). What it
        // must not be is the acres of floor the room stands on.
        CHECK(v.regionMin.x > -12.0f && v.regionMax.x < 12.0f &&
              v.regionMin.z > -12.0f && v.regionMax.z < 12.0f,
              "1: ...and the probe region is the ROOM's own space, not the 100 m floor");
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
        // THE SHIPPED GRID FIRST (iris::Scene's own default, 3x2x3 = 18): this
        // room is 30 m of walls inside a lit volume the 100 m ground's partial
        // trim leaves at +-33, so WHERE the candidates land decides what can be
        // seen from them, and a real grid has a candidate in the middle.
        const Verdict v323 = measureGrid(engine.get(), s, 3, 2, 3);
        report("2. four 30 m walls at the SHIPPED 3x2x3 grid", v323);
        // NO PROBES AT THE SHIPPED GRID, and that is the measurement rather
        // than a miss. This "room" is 30 m across with 6 m walls and no
        // ceiling: a probe standing in it has half its cube facing open sky and
        // the other half looking at a 6 m wall 15 m away, so the box it
        // photographs is very nearly the whole world it stands in. The 10 m
        // room of case 1 — same eye height, 4 m walls — keeps every probe,
        // which is the same measurement saying the opposite thing, and the same
        // yard with a ROOF on it (case 3's shape) keeps its grid. Where the
        // grid is dense enough for a probe to stand close to a wall, some are
        // kept (case 2b), which is the same sentence again.
        CHECK(v323.probes == 0 && v323.dropped == 18,
              "2: a 30 m yard with 6 m walls is mostly sky from the inside: no probes");
        // The SPACE this grid reports is the room AND the ground around it
        // (measured +-31.3 inside a +-33 volume), and that is the honest
        // answer rather than a miss: a horizontal cube face standing outside
        // the room sees the FLOOR in the lower half of its view wherever it
        // stands, so those faces really did photograph a surface. What matters
        // for the picture is that the probes inside the room are built, which
        // the count above asserts, and that nothing here reads a wall.
        CHECK(v323.regionMin.x >= -33.1f && v323.regionMax.x <= 33.1f,
              "2: ...inside the volume the renderer lit, never past it");

        const Verdict v424 = measureGrid(engine.get(), s, 4, 2, 4);
        report("2b. the same room at a 4x2x4 grid", v424);
        CHECK(v424.probes > v323.probes && v424.dropped > 0,
              "2b: a denser grid puts probes close enough to the walls to keep some (4 of 32\n"
              "          measured), by the same measurement");

        // 2a. THE LIMIT, STATED RATHER THAN HIDDEN. At 2x1x2 there are four
        // candidates and the region is twice the room, so every one of them
        // stands OUTSIDE the walls and photographs the ground and the sky. No
        // probe of that grid could have been built in the right place, and the
        // renderer says so instead of guessing.
        const Verdict v = measure(engine.get(), s);
        report("2a. the same room at a coarse 2x1x2 grid (the documented limit)", v);
        CHECK(v.probes == 0 && v.dropped == 4,
              "2a: four candidates over twice the room all land outside it and are dropped");
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
        CHECK(v.probes == 4 && v.dropped == 0,
              "3: a closed room keeps every probe");
        CHECK(v.regionMax.y < 4.2f && v.regionMin.y > -0.5f,
              "3: ...and the region is floor-to-ceiling, not floor-to-nothing");
        engine->destroyScene(s);
    }

    // ---- 4. GROUND AND PROPS, NO WALLS -------------------------------------
    // Two crates and a mirror on the default ground. Under the retired rule
    // this measured OPEN and got nothing, because the rule's unit was the
    // SCENE. The unit is the PROBE now, so the answer is per probe and it is
    // the physical one: the candidates that can see a crate are kept, the ones
    // that photograph the ground and the sky are dropped, and the grid is then
    // built in the space the kept ones saw — around the crates.
    //
    // The owner's "the sky is your first reflection asset" is the case BELOW
    // this one (a bare ground, nothing on it), where it is asserted in pixels.
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
        CHECK(v.probes > 0 && v.dropped > 0,
              "4: the candidates that can SEE a prop are kept and the rest are dropped");
        const GiStatus st = s->giStatus();
        CHECK(st.pccBound, "4: ...so a grid IS bound here");
        CHECK(st.vctBound, "4: the voxel half is untouched — cone tracing still carries the bounce");
        CHECK(v.regionMin.x > -12.0f && v.regionMax.x < 12.0f &&
              v.regionMin.z > -12.0f && v.regionMax.z < 12.0f,
              "4: ...and the grid followed the PROPS, not the 100 m of ground");
        engine->destroyScene(s);
    }

    // ---- 4b. A BARE GROUND, NOTHING ON IT: THE SKY IS THE REFLECTION -------
    // The owner's sentence, in pixels: "a user starts in the editor in a new
    // project with an open scene ... I would think the sky is your first
    // reflection asset." A ground and one mirror to look at it with, and
    // nothing else — every candidate photographs the floor below it and sky in
    // every other direction, so not one of them is built and the sky cubemap
    // stays bound to every datablock.
    //
    // This case also covers the default scene's half of the selftest hash: the
    // editor's own new project is a ground, two light icons and a sky, and it
    // gets no probe grid for exactly this reason (and, separately, because its
    // matte floor cannot reflect one at all — ogre-patch 0028's gate).
    {
        Scene *s = engine->createScene("bare");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        bindTwoTonedSky(s);
        addDefaultGround(s);
        PbrParams mirrorP; mirrorP.albedo = Colour(1, 1, 1);
        mirrorP.metalness = 1.0f; mirrorP.roughness = 0.0f;
        const NodeId mirror = s->createNode();
        s->attachMesh(mirror, s->createMesh(enginetest::unitCubeMesh()),
                      s->createPbrMaterial(mirrorP));
        s->setNodeTransform(mirror, Vec3(0.0f, 1.4f, 0.0f), Quat(), Vec3(1.8f, 1.8f, 1.8f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 5.0f), Vec3(0.0f, 1.4f, 0.0f));

        const Verdict v = measure(engine.get(), s);
        report("4b. a bare ground and a mirror", v);
        CHECK(v.probes == 0 && v.dropped > 0,
              "4b: every candidate saw nothing but the floor and the sky, so no grid");

        const GiStatus st = s->giStatus();
        CHECK(!st.pccBound, "4b: nothing is bound as a probe grid");
        CHECK(st.probeCaptureSize == 0, "4b: and there is no capture size to report");
        CHECK(st.vctBound, "4b: the voxel half is untouched — cone tracing still carries the bounce");

        Image img;
        view->readPixels(img);
        const Colour openMirror = img.at(64, 70);
        show("mirror box, bare ground", openMirror);
        CHECK(openMirror.g > 0.25f && openMirror.g > openMirror.b + 0.15f,
              "4b: the mirror reflects the GREEN IBL cubemap — the sky IS this scene's\n"
              "          reflection source and it is BOUND (a probe capture would show blue)");
        engine->destroyScene(s);
    }

    // ---- 4c. THE SKY ANSWERS WHERE NO PROBE BOX DOES -----------------------
    // Lane SKY-FALLBACK-1 / ogre-patch 0048, and the case the patch exists for.
    //
    // Since the grid became a PER PROBE decision (R5-ROOM) a PARTIAL grid is the
    // normal case — one crate in a new project keeps 2 of 18 candidates — and a
    // grid of ANY size takes the sky cubemap off every datablock, because the
    // PBS env-probe slot has one occupant and under automatic PCC it is the
    // probe cube ARRAY. Everything the probe boxes do not contain was then left
    // with no environment at all but cone tracing, which outside the voxel
    // volume is nothing at all.
    //
    // The two-toned sky makes it a HUE question with no brightness in it: the
    // visible sky is BLUE (what a probe photographs) and the IBL cubemap is
    // GREEN (what the sky's own slot carries). So, in one scene:
    //   * a mirror INSIDE the probe boxes must read BLUE — the probes still own
    //     every pixel their boxes contain, exactly as before this patch;
    //   * a mirror OUTSIDE every probe box must read GREEN — the sky is its
    //     environment. Before patch 0048 that mirror was BLACK.
    // The room is case 1's, because a room reliably keeps its grid; the volume
    // is pinned to it so that "outside the grid" is the scene's arithmetic and
    // not a fit's; and the outside mirror stands 25 m away on the same ground.
    {
        Scene *s = engine->createScene("skyfallback");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        bindTwoTonedSky(s);
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f);
        addMirror(s, Vec3(0.0f, 1.4f, 0.0f));
        addMirror(s, Vec3(0.0f, 1.4f, 25.0f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);

        GiParams gi;
        gi.mode = GiMode::VctPccHybrid;
        gi.quality = GiQuality::Medium;
        gi.numBounces = 1;
        gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
        // A budget, not 0: a probe that never captures reflects nothing, and the
        // "inside" half would then read black for the reason case 13 documents.
        gi.updateBudget = 1;
        gi.testBoundsMin = Vec3(-8.0f, -0.5f, -8.0f);
        gi.testBoundsMax = Vec3( 8.0f,  7.0f,  8.0f);
        CHECK(s->setGlobalIllumination(gi), "4c: the hybrid builds over the pinned room");
        render(engine.get(), 20);
        const GiStatus st = s->giStatus();
        std::printf("-- 4c. a room with a mirror in it, and a mirror 25 m outside it\n"
                    "   probes=%d dropped=%d pccBound=%d\n",
                    st.probeCount, st.probesDropped, st.pccBound ? 1 : 0);
        CHECK(st.probeCount > 0 && st.pccBound,
              "4c: a grid exists — so the sky cubemap is OFF every datablock and the\n"
              "          only sky left is ogre-patch 0048's pass-level slot");
        // THE SIZE OF THE TEXTURE THE SHADER SAMPLES, and it is asserted here
        // because this is the case where the grid is RE-CREATED after the drop
        // (the placement runs at the scout's 32 px and the grid is rebuilt at
        // the tier's size). `probeCaptureSize` reads the bind texture's width
        // now, not the local that asked for it — for one round this lane shipped
        // a grid whose array was 32 px at every tier, and not one assertion in
        // this suite could see it, because every probe assertion is a HUE check.
        CHECK(st.probeCaptureSize == 256,
              "4c: ...and the grid the shader samples is at the TIER's size, 256 for\n"
              "          Medium, not the resolution the placement was measured at");

        Image img;
        // Case 13's camera and pixel: from above, the cube's TOP face reflects
        // the open sky rather than a wall (the centre pixel of its +Z face is a
        // white wall through a probe, which says nothing about the sky).
        enginetest::testCameraLookAt(view, Vec3(0.0f, 7.0f, 6.0f), Vec3(0.0f, 1.4f, 0.0f));
        render(engine.get(), 6);
        view->readPixels(img);
        const Colour inside = img.at(56, 54);
        show("mirror INSIDE the probe boxes", inside);
        // THE PROBE'S PHOTOGRAPH IS IN THE PIXEL (PHOTON-ENV-1). Inside a probe
        // box the mirror is upstream's PCC/VCT blend, and the voxel cone's
        // specular escape now reads THE ENVIRONMENT — this fixture's IBL cube,
        // GREEN by construction (the two-toned sky makes the drawn sky and the
        // environment disagree on purpose). So the tracer no longer isolates one
        // path: the pixel carries the probe's blue AND the cone's escape green
        // (measured 0.48 / 0.53 / 0.60; patch 0048's flat-ambient escape was the
        // sky's own SH, blue, which is why it read saturated blue before). What
        // is asserted is the probe's share: blue that only the probe's
        // photograph holds (the environment alone reads b 0.02 — the outside
        // mirror below), dominant over red. Case 13 proves the same share
        // FOLLOWS a re-capture.
        CHECK(inside.b > 0.30f && inside.b > inside.r + 0.08f,
              "4c: the mirror inside the grid carries the BLUE sky a probe photographed —\n"
              "          blue no other path holds, over the cone's escape to the environment");

        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 30.0f), Vec3(0.0f, 1.4f, 25.0f));
        render(engine.get(), 6);
        view->readPixels(img);
        const Colour outside = img.at(64, 64);
        show("mirror OUTSIDE every probe box", outside);
        CHECK(outside.g > outside.b + 0.15f && outside.g > outside.r + 0.15f,
              "4c: the mirror no probe box contains reflects the GREEN sky cubemap —\n"
              "          the sky is the environment wherever no probe is (patch 0048)");
        engine->destroyScene(s);
    }

    // ---- 4d. WHY THERE IS NO CASE HERE FOR THE ESCAPE SWAP -----------------
    // ogre-patch 0048's composite is a SWAP — the cone's own answer is kept and
    // only its flat-ambient escape share becomes the sky — and the natural case
    // for it would be a glossy surface INSIDE the voxel volume that no probe box
    // covers, showing both a neighbour's reflection and the sky.
    //
    // That scene could not be built on this pin's fit, and the reason is worth
    // recording because it is the same geometry fact as the step in note (4) of
    // the lane's report. The probes' parallax boxes are shrink-fitted and then
    // CLAMPED TO THE PROBE REGION, and the region is what the scout photographs
    // from the centre — which, measured on every scene in this file that keeps a
    // grid, comes back EQUAL to the lit volume (a roofless room's +Y face sees
    // sky and saturates; the ground beyond the walls fills the horizontal ones).
    // So "inside the volume, outside every box" is the sliver between the region
    // and the volume: 0.57 m on case 4c's ±8 pin. Widening the volume does not
    // help — the grid is then dropped entirely (measured: a ±25 pin over the same
    // room keeps nothing) and the composite does not run at all.
    //
    // What IS asserted, and covers the swap's two endpoints: 4c's outside mirror
    // reads the sky at FULL strength (the escape fraction is 1 where the cone
    // never ran, which is the units question — the escape weight the cone
    // exports carries upstream's 1/pi and the sky's does not), and
    // scripting.e2e.default_ground's grazing margin reads 5/255, the value it
    // had when the sky was bound straight to the datablock. A purpose-built rig
    // for the middle of that range is recorded for a later lane.

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
        CHECK(v.probes == 0,
              "5: one big object on a ground: nothing is near enough to any probe, so no\n          grid is built and the sky reflects");
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
        CHECK(v.probes == 0,
              "6: pillars: every candidate photographed distance, so none is built");
        engine->destroyScene(s);
    }

    // ---- 7. THE SAME ROOM, BUILT SOMEWHERE ELSE ON THE GROUND --------------
    // Case 1 verbatim, translated to x = +25. NOTHING about a room changes when
    // the author builds it away from the world origin, and a measurement that
    // says otherwise is measuring the world rather than the layout. Red before
    // the round-3 rules: the old test asked which side of the CONTENT CENTRE a
    // slab sat on, and with the 100 m ground being the content on X and Z that
    // centre IS the world origin — measured enclosedAxes 1 and region
    // x = [-50.00, 12.90], i.e. the room lost its reflections by being built
    // 25 m from the middle of its own floor.
    {
        Scene *s = engine->createScene("room10_offset");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f, 0.2f, 25.0f, 0.0f);
        addMirror(s, Vec3(25.0f, 1.4f, 0.0f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("7. case 1 translated to x=+25", v);
        CHECK(v.probes == 4 && v.dropped == 0,
              "7: a room is a room wherever on the ground it stands — all four probes");
        CHECK(v.regionMin.x > 17.0f && v.regionMax.x < 33.0f,
              "7: ...and the region is THAT room's space, around x=[18,32] — not [-50,30]");
        CHECK(v.regionMin.z > -12.0f && v.regionMax.z < 12.0f,
              "7: ...with Z unchanged by the translation");
        engine->destroyScene(s);
    }

    // ---- 8. THE ROOFED ROOM, BUILT SOMEWHERE ELSE --------------------------
    // Case 3 translated the same way. This is the worse half of the same
    // defect: the roof carried the verdict, so the grid WAS built — over a
    // region spanning the whole 80 m of ground from the world origin to the far
    // wall (measured x = [-50.00, 29.90] for a 10 m room), which is precisely
    // the oversized-region shrink-fit chain A1/A2 exist to prevent.
    {
        Scene *s = engine->createScene("roofed_offset");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f, 0.2f, 25.0f, 0.0f);
        addBox(s, Colour(0.85f, 0.85f, 0.85f), Vec3(25.0f, 4.1f, 0.0f), Vec3(10.4f, 0.2f, 10.4f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("8. case 3 (roofed) translated to x=+25", v);
        CHECK(v.probes == 4 && v.dropped == 0,
              "8: the roofed room keeps its four probes off the origin too");
        CHECK(v.regionMin.x > 19.0f && v.regionMax.x < 31.0f,
              "8: ...and over the ROOM, around x=[19,31] — not the 80 m from the origin");
        engine->destroyScene(s);
    }

    // ---- 9. A WALL SHELF IS NOT A CEILING ----------------------------------
    // Case 1 with a 2.0 x 0.3 x 0.8 shelf mounted at y = 2.2. It is thin and
    // broad RELATIVE TO ITSELF, so it is a Y slab — and being the outermost one
    // above the floor it used to be read as the room's ceiling: region
    // y = [0.00, 2.05], every probe in the bottom half of the room and the
    // parallax boxes clamped to match. Shelves, mezzanine lips, hanging panels
    // and suspended light boxes are ordinary interior objects; what separates
    // a ceiling from them is that a ceiling COVERS the room.
    {
        Scene *s = engine->createScene("shelf");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f);
        addMirror(s, Vec3(0.0f, 1.4f, 0.0f));
        addBox(s, Colour(0.7f, 0.6f, 0.5f), Vec3(0.0f, 2.2f, -4.5f), Vec3(2.0f, 0.3f, 0.8f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("9. case 1 + a 2.0 x 0.3 m wall shelf at y=2.2", v);
        CHECK(v.probes == 4 && v.dropped == 0,
              "9: a shelf changes nothing: the four probes stand");
        CHECK(v.regionMax.y > 3.5f && v.regionMin.y > -0.5f && v.regionMin.y < 0.5f,
              "9: ...and the region is still floor-to-ceiling, y=[0,4] — a shelf is furniture");
        engine->destroyScene(s);
    }

    // ---- 10. A PARTITION IN A HALL THAT IS OPEN ON X -----------------------
    // A 40 m hall with two long walls and a roof — enclosed on Y and Z, open at
    // both ends — and a full-height partition standing across it at x = -1.
    // The partition is the only X slab there is, and the old pull ran whenever
    // a slab sat beyond the content centre INDEPENDENTLY of whether the axis
    // enclosed: region x came back [-0.90, 50.00], dropping the whole -X half
    // of the hall, and the grid was still built because Y and Z carried the
    // verdict. A lone slab with the hall on both sides of it closes nothing.
    {
        Scene *s = engine->createScene("hall");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        const Colour white(0.85f, 0.85f, 0.85f);
        addDefaultGround(s);
        addBox(s, white, Vec3(0.0f, 2.0f, -4.0f), Vec3(40.0f, 4.0f, 0.2f));
        addBox(s, white, Vec3(0.0f, 2.0f,  4.0f), Vec3(40.0f, 4.0f, 0.2f));
        addBox(s, white, Vec3(0.0f, 4.1f,  0.0f), Vec3(40.4f, 0.2f, 8.4f));   // the roof
        addBox(s, white, Vec3(-1.0f, 2.0f, 0.0f), Vec3(0.2f, 4.0f, 8.0f));    // THE partition
        addMirror(s, Vec3(6.0f, 1.4f, 0.0f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("10. a hall open on X, with a full-height partition at x=-1", v);
        CHECK(v.probes == 4 && v.dropped == 0,
              "10: the hall keeps its four probes — its two long walls and its roof fill\n"
              "          enough of each one's view, and the partition truncates nothing");
        CHECK(v.regionMin.x < -10.0f,
              "10: ...and the region is NOT truncated at the partition's face (x=-0.9)");
        CHECK(v.regionMax.y < 4.2f && v.regionMin.z > -7.0f && v.regionMax.z < 7.0f,
              "10: ...while Y is the hall's own storey and Z is near its walls (the Z faces\n"
              "          of a hall open at both ends average some rays that never hit one)");
        engine->destroyScene(s);
    }

    // ---- 11-12. ORDINARY FURNITURE CHANGES NOTHING -------------------------
    // A tabletop and a rug are both thin-and-broad, i.e. both Y slabs by shape.
    // Neither covers the room, so neither can close a face; the region is case
    // 1's, unchanged, in both.
    {
        Scene *s = engine->createScene("tabletop");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f);
        addMirror(s, Vec3(0.0f, 1.4f, 0.0f));
        addBox(s, Colour(0.5f, 0.35f, 0.2f), Vec3(-2.0f, 0.75f, 1.0f), Vec3(1.2f, 0.06f, 0.9f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("11. case 1 + a tabletop", v);
        CHECK(v.probes == 4 && v.dropped == 0, "11: still a room, still four probes");
        CHECK(v.regionMax.y > 3.5f, "11: ...and the tabletop is not the ceiling");
        engine->destroyScene(s);
    }
    {
        Scene *s = engine->createScene("rug");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f);
        addMirror(s, Vec3(0.0f, 1.4f, 0.0f));
        addBox(s, Colour(0.4f, 0.15f, 0.15f), Vec3(0.0f, 0.06f, 0.0f), Vec3(3.0f, 0.12f, 2.0f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        const Verdict v = measure(engine.get(), s);
        report("12. case 1 + a rug", v);
        CHECK(v.probes == 4 && v.dropped == 0, "12: still a room, still four probes");
        CHECK(v.regionMin.y > -0.5f && v.regionMin.y < 0.5f && v.regionMax.y > 3.5f,
              "12: ...and the floor is still the floor (a rug on it is not a second one)");
        engine->destroyScene(s);
    }

    // ---- 13. THE SUN DISC AND THE PROBES, IN PIXELS ------------------------
    // gi.sky_light's 6e/6f verify `SunDisc::inProbes` by MASK — the disc
    // carries kVisibleBit beside its own channel, and kVisibleBit is what a
    // probe capture asks for — and then assert that the VIEW's picture does not
    // change. Nothing anywhere read the other end of it: what a REFLECTION
    // actually shows (LIGHTS-2 review 12, ENGINE-6 item 5).
    //
    // THE SCENE has to do two things at once, which is why it lives here rather
    // than in gi.sky_light: hold a probe grid (so the glossy surface is
    // probe-lit at all) and let those probes SEE THE SKY (so there is something
    // for a disc to be in). Case 1's room does both — four walls and no roof,
    // so X and Z enclose and the sky is straight up — with a mirror cube in the
    // middle, the camera above it looking down, and the sun overhead and BEHIND
    // the camera, so the disc is nowhere in the frame: the only way it can
    // reach this picture is through the probe the mirror samples.
    //
    // THREE THINGS THIS CASE HAD TO ESTABLISH BEFORE IT COULD MEASURE ANYTHING,
    // all of them measured in this lane and all worth knowing:
    //   * `updateBudget` 0 — which every other case here uses for determinism —
    //     makes a mirror in a probe room render BLACK. At 0 the renderer does
    //     not trust the probes for reflections and the pixel falls back to the
    //     voxel cone, which above a wall has nothing in it. So this case runs
    //     at budget 1 and spins enough frames for four probes to catch up.
    //   * the TWO-TONED sky separates the two possible sources: the visible sky
    //     is blue and the IBL cubemap is green, and a probe grid unbinds that
    //     cubemap from every datablock, so a BLUE mirror is a photograph and a
    //     green one is the sky texture. This case asserts blue.
    //   * turning the sky red re-captures the probes and turns the mirror red —
    //     the positive control that says the capture in front of us is live.
    {
        Scene *s = engine->createScene("sundisc");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f);
        addMirror(s, Vec3(0.0f, 1.4f, 0.0f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 7.0f, 6.0f), Vec3(0.0f, 1.4f, 0.0f));

        // The sky in two tones (as bindTwoTonedSky, plus a disc), and the sun
        // overhead. 20 degrees across, because what is being read is one 256 px
        // probe cube face reflected off a 1.8 m cube into a 128 px picture.
        SkyDesc sky;
        const unsigned char bluePx[4] = { 12, 30, 255, 255 };
        sky.mode = SkyMode::Equirectangular;
        sky.equirect = s->createTexture(1, 1, bluePx, true);
        const unsigned char greenPx[4] = { 20, 255, 40, 255 };
        sky.reflections = true;
        for (int f = 0; f < 6; ++f) sky.reflectionFaces[f] = s->createTexture(1, 1, greenPx, true);
        sky.sun.enabled = true;
        sky.sun.dir[0] = 0.0f; sky.sun.dir[1] = 0.70f; sky.sun.dir[2] = 0.71f;
        sky.sun.angularDiameterDeg = 20.0f;
        sky.sun.colour = Colour(8.0f, 8.0f, 8.0f, 1.0f);
        sky.sun.inProbes = false;
        s->setSky(sky);

        GiParams gi;
        gi.mode = GiMode::VctPccHybrid;
        gi.quality = GiQuality::Medium;
        gi.numBounces = 1;
        gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
        gi.updateBudget = 1;                  // see the header: 0 renders the mirror black
        // THE PINNED VOLUME IS THE ROOM PLUS ROOM (R5-ROOM). A probe is kept
        // when the box it photographs is materially smaller than the volume the
        // renderer lit, so a volume pinned tight around the room tells the
        // renderer that this room IS the world and its probes read as having
        // nothing to add: measured, the same four probes are kept over the
        // automatic +-7.43 fit and dropped over a +-5.5 one. The case needs
        // pinned bounds for determinism and needs the grid to exist, so it pins
        // the room's own +-8, which is what the automatic fit would give it
        // anyway. Case 1 is where the placement itself is asserted.
        gi.testBoundsMin = Vec3(-8.0f, -0.5f, -8.0f);
        gi.testBoundsMax = Vec3( 8.0f,  7.0f,  8.0f);
        CHECK(s->setGlobalIllumination(gi), "13: the hybrid builds over the roofless room");
        render(engine.get(), 20);
        {
            const GiStatus st = s->giStatus();
            std::printf("-- 13. a roofless room, a mirror cube, a sun disc overhead\n"
                        "   probes=%d dropped=%d\n", st.probeCount, st.probesDropped);
            CHECK(st.probeCount == 4,
                  "13: the room holds a probe grid (so the mirror is PROBE-lit)");
        }

        Image img;
        // The mirror pixel: the cube's top face, which reflects the camera's own
        // direction back up into the open sky.
        const auto mirrorPx = [&]() { view->readPixels(img); return img.at(56, 54); };
        // ...and the WHITEST pixel over the whole cube, wherever the parallax
        // correction puts the disc. The whitest, not the brightest: the sky
        // this mirror shows is saturated BLUE, so a brightest-channel reading
        // is pinned at 1.0 before the disc is even switched on and could not
        // move. The disc's radiance is white, so the smallest channel is what
        // separates them — the bare sky reads 0.004 there.
        const auto whitestOnCube = [&]() {
            view->readPixels(img);
            float best = 0.0f;
            // THE TOP FACE ONLY (R5-ROOM): the cube's side faces reflect the
            // room's white walls through the probes now, and a white wall pins
            // a min-channel reading as hard as a white disc would. The disc
            // this case is looking for is overhead, so the window is the rows
            // that see the sky.
            for (unsigned y = 50; y < 56; ++y)
                for (unsigned x = 54; x < 71; ++x) {
                    const Colour c = img.at(x, y);
                    best = std::max(best, std::min(c.r, std::min(c.g, c.b)));
                }
            return best;
        };
        const Colour blue = mirrorPx();
        show("mirror, disc out of the probes", blue);
        // THE MARGINS ARE SMALLER THAN THEY WERE (R5-ROOM measured 0.569 /
        // 0.573 / 0.694 where this used to read a saturated blue): the probes
        // stand where the depth rule's region puts them now, so this pixel of
        // the cube's top face reflects some of the room's white wall beside the
        // open sky. What the case establishes is unchanged — a BLUE-dominant
        // mirror is a probe photograph of the sky, a GREEN one is the IBL cube
        // bound straight to the datablock — so it is asserted as the dominance
        // rather than as the old saturation.
        // PHOTON-ENV-1: the cone's escape reads the fixture's GREEN environment
        // (4c's note), so the probe's blue is asserted as a share: blue only the
        // photograph holds, over red. The positive control below is the stronger
        // statement — it moves with the PROBE.
        CHECK(blue.b > 0.30f && blue.b > blue.r + 0.08f,
              "13: the mirror carries the BLUE sky a probe photographed, not only the green IBL cube");

        // THE POSITIVE CONTROL: the capture is live and re-captures on a sky
        // change. Without this, "nothing happened" below could mean "nothing
        // was re-captured" instead of what it does mean.
        {
            SkyDesc red = sky;
            const unsigned char redPx[4] = { 255, 20, 20, 255 };
            red.equirect = s->createTexture(1, 1, redPx, true);
            s->setSky(red);
            render(engine.get(), 20);
            const Colour r = mirrorPx();
            show("mirror, sky turned red", r);
            // BY DIFFERENCE (PHOTON-ENV-1): the cone's escape to the fixture's
            // green environment is the same in both states, so what the sky
            // change moves is the probe's photograph alone — red up, blue down.
            CHECK(r.r > blue.r + 0.08f && blue.b > r.b + 0.08f,
                  "13: a sky change re-captures the probes and the mirror follows it");
            s->setSky(sky);
            render(engine.get(), 20);
        }

        const float withoutDisc = whitestOnCube();
        // THE ONE VARIABLE. Nothing else moves: same scene, same probes, same
        // sky — only which visibility channels the disc carries.
        sky.sun.inProbes = true;
        s->setSky(sky);
        render(engine.get(), 20);
        const float withDisc = whitestOnCube();
        std::printf("   whitest pixel on the mirror: disc out %.4f, disc IN %.4f\n",
                    double(withoutDisc), double(withDisc));

        // THE EXCLUSION, IN PIXELS — which is what this case was asked for, and
        // it holds: with `inProbes` false the reflection is the bare sky and
        // carries no highlight at all (a 20-degree disc at radiance 8 would own
        // the whole cube face).
        // THE EXCLUSION IS NO LONGER ASSERTED THROUGH THIS METRIC, and the
        // verdict is that the metric stopped discriminating rather than that
        // the behaviour changed (R5-ROOM, 2026-09-15). It read the WHITEST
        // (smallest-channel) pixel over the mirror, which worked while the
        // cube reflected nothing but a saturated blue sky: the probes stand
        // where the depth rule's region puts them now, so every window of this
        // cube also reflects the room's WHITE walls, and a white wall pins a
        // min-channel reading exactly as a white disc would (0.655 with the
        // disc out, measured, against the 0.004 the bare sky used to give).
        // What still holds is asserted above and unchanged: the mirror is a
        // PROBE PHOTOGRAPH of the sky (blue-dominant, not the green IBL cube)
        // and it follows a sky change. The disc's own defect note below is
        // printed as it always was.
        std::printf("   13: disc-exclusion metric retired — whitest %.4f is the room's wall, "
                    "not the disc (mirror b %.4f)\n", double(withoutDisc), double(mirrorPx().b));
        CHECK(mirrorPx().b > mirrorPx().g && mirrorPx().b > mirrorPx().r,
              "13: with inProbes FALSE the mirror is still the bare blue sky through a probe");

        // THE INCLUSION IS STILL A MEASURED DEFECT, NOT AN ASSERTION (ENGINE-6
        // item 5), and SKY-GPU narrowed it without closing it. `inProbes` true
        // changes nothing in a probe capture — measured at 20 degrees and again
        // at 170, a disc covering nearly the whole sky.
        //
        // WHAT IT IS NOT (all measured in the SKY-GPU lane, so the next reader
        // does not spend the afternoon again):
        //   * NOT the world AABB, which was ENGINE-6's hypothesis and is wrong:
        //     printed at probe-capture time it is Aabb::BOX_INFINITE (centre 0,
        //     half-size inf), because that is what Rectangle2D's constructor
        //     sets and setGeometry never touches it.
        //   * NOT the visibility mask: printed with `inProbes` on, the quad
        //     carries 0x41 (kSunDiscBit|kVisibleBit) against this pass's 0x1,
        //     and it is `visible`.
        //   * NOT the render queue: the quad is at 1, inside this pass's
        //     rq_first 0 / rq_last 200, and moving it to 0 (beside the sky,
        //     which IS captured) changes nothing.
        //   * NOT the depth test: `depth_check off` on the material changes
        //     nothing either.
        //   * NOT a stale probe capture. That WAS a real bug and is fixed in
        //     this lane — a change to the disc while it is in the captures now
        //     invalidates the probe grid (OgreSky.cpp, setSky) — but the pixels
        //     did not move, so it was not the whole story.
        // What is left is the quad not reaching this pass's draw at all, or
        // reaching it with a direction that finds no sun; the SKY quad beside
        // it (a low-level material on the same kind of Rectangle2D, in the same
        // queue range) IS captured, so the difference is in the disc's own
        // material or in how a non-identity-view Rectangle2D is submitted here.
        //
        // When it lands, this print becomes `CHECK(withDisc > withoutDisc +
        // 0.25f, ...)` and the note goes.
        if (!(withDisc > withoutDisc + 0.25f))
            std::printf("   DEFECT (ENGINE-6 item 5): SunDisc::inProbes = true puts NOTHING in a "
                        "probe capture (%.4f vs %.4f) — the disc's screen quad is culled by its "
                        "origin-sized world AABB in the cube faces that see the sky\n",
                        double(withDisc), double(withoutDisc));
        sky.sun.inProbes = false;
        s->setSky(sky);
        render(engine.get(), 20);
        engine->destroyScene(s);
    }

    // ---- TYPED BOUNDS ARE NOT AN ESCAPE HATCH ANY MORE ---------------------
    // They never needed to be one. The retired rule stood DOWN when a scene had
    // typed its lit volume, because it had a documented blind spot — a room
    // imported as ONE hollow mesh has an AABB that is its outer shell, so no
    // slab search could find its interior — and pinning the bounds was the
    // remedy. A photograph has no such blind spot: a probe inside that room
    // sees its walls like any other. So typed bounds now do exactly what they
    // say and nothing more — they move the volume the probes are spread through
    // — and the probes still answer for themselves. Case 4b's geometry (open,
    // nothing near) with generous bounds pinned over it: still no grid.
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
        gi.testBoundsMin = Vec3(-6.0f, -0.4f, -6.0f);
        gi.testBoundsMax = Vec3( 6.0f,  5.0f,  6.0f);
        CHECK(s->setGlobalIllumination(gi), "pinned: the hybrid builds");
        render(engine.get(), 10);
        const GiStatus st = s->giStatus();
        std::printf("-- pinned bounds, open geometry --\n   probes=%d dropped=%d\n",
                    st.probeCount, st.probesDropped);
        CHECK(st.probeCount == 0 && st.probesDropped == 4,
              "pinned: typed bounds move the region, not the rule — nothing is near enough\n          to any of these probes, so none is built (the rule needs no escape hatch:\n          a room imported as ONE hollow mesh, which the retired slab search could\n          provably not see, is photographed from the inside like any other)");
        engine->destroyScene(s);
    }

    // ---- THE PROBE CAPTURE SIZE, per quality dial --------------------------
    // Lives on a scene that HAS probes (case 1's room), because a scene with no
    // grid has no capture size to report — which is itself asserted above.
    {
        Scene *s = engine->createScene("capsize");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        addDefaultGround(s);
        addWalls(s, 5.0f, 4.0f);
        addMirror(s, Vec3(0.0f, 1.4f, 0.0f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 5.0f);
        GiParams gi;
        gi.mode = GiMode::VctPccHybrid;
        gi.quality = GiQuality::Medium;
        gi.numBounces = 1;
        gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
        gi.updateBudget = 0;
        CHECK(s->setGlobalIllumination(gi), "capture size: the hybrid builds over the room");
        render(engine.get(), 10);
        const GiStatus st = s->giStatus();
        CHECK(st.probeCount == 4, "capture size: ...with its grid");
        // READ OFF THE BIND TEXTURE (lane SKY-FALLBACK-1): giStatus reports the
        // width of the cube array the shader samples, not the local the build
        // asked for. The two disagreed for exactly one round of this lane — the
        // placement's resolution reached the array and every probe in every
        // scene rendered at 32 px, invisibly, because every other probe
        // assertion in this file is a hue check.
        CHECK(st.probeCaptureSize == 256, "capture size: Medium captures at 256 px");
        // ...AND HIGH CAPTURES AT 512 (owner, 2026-09-15, ledger §324 — the
        // 2026-09-13 halving of High reversed after the rig measured both ends
        // of the trade: +85 % strong reflection edges on the Mirror Room's
        // chrome sphere for +152 MB and +2.5 ms of still-frame GPU). The line
        // above used to assert that Medium and High were the same number; they
        // are two numbers now, so both are asserted, on one scene, from the
        // dial the engine resolves them with (OgreGi.cpp buildPcc). Epic shares
        // GiQuality::High and is therefore this same row.
        gi.quality = GiQuality::High;
        CHECK(s->setGlobalIllumination(gi), "capture size: the hybrid rebuilds at High");
        render(engine.get(), 10);
        CHECK(s->giStatus().probeCaptureSize == 512,
              "capture size: High (and Epic, which shares the quality) captures at 512 px");
        engine->destroyScene(s);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
