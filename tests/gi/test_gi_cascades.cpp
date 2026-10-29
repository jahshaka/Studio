// gi.cascades (archived: archive/tests/gi) — PHOTON'S CAMERA-CENTRED VOXEL CASCADES (SPECS/PHOTON_SPEC.md P0).
//
// The voxels are a chain of camera-centred boxes, chained through Ogre's own
// `VctLighting::addCascade` and scheduled by us (the single scene-fitted volume
// they replaced is deleted, D4-PHOTON-TIERS).
//
// What a scheduler has to get right, and what each case here proves:
//
//   1. THE CHAIN IS BUILT AND BOUND. N cascades, innermost first, with the
//      tier's cells and a step per cascade derived so that every cascade steps
//      the same DISTANCE (the pin's autoCalculateStepSizes shape, written out
//      in resolveCascadeTable so it is ours to tune).
//   2. AT REST IT COSTS EXACTLY NOTHING. A still camera re-voxelises nothing,
//      for ever — the cache policy this engine is built on
//      (ENGINE_CACHE_POLICY_SPEC), and the one property upstream's manager
//      already had.
//   3. A SCROLL RE-CENTRES, ONE CASCADE PER FRAME. Crossing a cascade's step
//      moves its centre onto the world lattice and re-voxelises it, and no
//      frame ever pays for two.
//   4. THE AMBIENT IS IN EVERY CASCADE. Binding a VctLighting suppresses
//      HlmsPbs' own ambient scene-wide, so a cascade whose hemispheres were
//      never set contributes darkness. Upstream's cascade manager never sets
//      them and its room renders 2,2,2 (spikes/photon-s1 §4.3). Asserted as a
//      PICTURE: a scene lit by nothing but its ambient must render the same
//      brightness with the chain up as with GI off.
//   5. A TELEPORT IS SAFE, BOUNDED, AND NEVER BLACK. A 1 km jump is the case
//      that lost the GPU three separate ways on upstream's incremental path
//      (S1 §4.4: VK_ERROR_DEVICE_LOST, a segfault, a >2 s stall). Here the two
//      DIRTY_ALL guards turn it into exactly N counted rebuilds, one per frame,
//      innermost first — and every frame of that window is lit, because a
//      cascade that has not been rebuilt yet still holds a correctly lit
//      volume and the ambient covers the rest.
//   6. THE GUARD DOES NOT FIRE ON AN ORDINARY SCROLL. The counter would be
//      worthless if walking tripped it.
//
// Its own binary like every GI suite.
// ITS VIEW IS AN UNDECLARED OFFSCREEN VIEW (no View::setOffscreenContract), whatever the rays: its chain never gathers
// (ChainDesc::probeGather is a still picture's, PHOTON-7 7-5) and shades through the no-ray route's screen integrate
// (7-6) - this suite measures the voxel chain, the field, the cones and the probes beneath it, never the gather.
//
// SIX ROWS, ONE PROOF EACH (HARNESS-2, 2026-10-29: the one row was priced 50.68 s, over the 45-s quiet law, and timed out at
// 60 s twice under load). `test_gi_cascades_asan <part>` runs ONE part in its own process - every case keeps its frames and
// its bars, and a part that needed the state an earlier case left STAGES it (named where it is staged):
//   schedule      cases 0, 1, 2, 2b, 3, 5, 6, 8, 9, 10, 16a-16d - THE CAMERA'S SCHEDULE: where the chain stands and what a
//                 camera move costs (the open, the table, rest, the sway band, the scroll, the teleport, empty space, the
//                 validated tables, the monitor's rows, `items`, the near field, the lattice, the free pin). Counts and
//                 placements, no picture.
//   faults        cases 11, 13 - A FAILED REBUILD CHANGES NOTHING BUT THE CLOCK (a scroll's and a whole rebuild's throw:
//                 not counted, the placement kept or owed, the chain bound, the retry within its count). The sanitizer's
//                 own subject: the failure paths around a cascade's voxeliser.
//   edits         cases 7 (edits), 14, 15, 15b - AN EDIT RE-VOXELISES EXACTLY THE CASCADES THAT HOLD IT, one a frame, never
//                 the chain; the instance budget bounds what a rebuild voxelises; the dirty boxes merge by margin, and
//                 the merged rebuilds' light rests at a fresh build's within two codes (case 15, the solver's bound).
//   ambient       cases 4, 7 (specular) - THE CHAIN CARRIES THE AMBIENT AS ONE CASCADE DOES, diffuse and specular.
//   mover         case 12, the three poses with the rays off - A2's LAW: a declared mover is lit at the pose it is in
//                 (motion against rest), and at rest the picture is a fresh build's.
//   mover_routes  case 12, the corner with the rays ON and a STILL turned by script - at rest each is a fresh build's
//                 (asserted); the rays-on residue (the non-gathering route's screen integrate, PHOTON-7's leftover) and
//                 the still's move quantum are printed.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 128;

/// Every frame the part renders, counted - printed at the end as the row's FRAMES (never estimated).
static long long gFrames = 0;

static void render(Engine *e, int frames = 1)
{
    enginetest::frames(e, frames);
    gFrames += frames;
}

/// enginetest::settleVoxelReadings with its frames counted (it renders up to its 64 on a reading that never lands).
static int settleReadings(Engine *e, Scene *s)
{
    const int f = enginetest::settleVoxelReadings(e, s);
    gFrames += f >= 0 ? f : 64;
    return f;
}

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

/// Mean luminance of the lower half of the picture — the ground, wherever the
/// camera stands. A band and not a pixel so one dithered texel cannot decide.
static float groundLum(const Image &img)
{
    float sum = 0.0f; unsigned n = 0;
    for (unsigned y = kSize * 5u / 8u; y < kSize * 7u / 8u; ++y)
        for (unsigned x = kSize / 4u; x < kSize * 3u / 4u; ++x) { sum += lum(img.at(x, y)); ++n; }
    return n ? sum / float(n) : 0.0f;
}

/// A FLAT quad in the XZ plane, 1x1, centred on its origin: a Plane primitive's
/// geometry, and the degenerate case any box metric has to survive — its world
/// AABB has ZERO HEIGHT (case 15b).
static MeshData flatPlaneMesh()
{
    MeshData d;
    const float h = 0.5f;
    const float pos[12] = { -h, 0.0f, -h,   h, 0.0f, -h,   h, 0.0f, h,   -h, 0.0f, h };
    d.positions.assign(pos, pos + 12);
    for (int i = 0; i < 4; ++i) { d.normals.push_back(0.0f); d.normals.push_back(1.0f); d.normals.push_back(0.0f); }
    const float uv[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    d.uvs.assign(uv, uv + 8);
    const unsigned idx[6] = { 0, 1, 2, 0, 2, 3 };
    d.indices.assign(idx, idx + 6);
    return d;
}

static GiParams cascadeGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;          // no probe work; this suite is about the voxels
    return gi;
}

int main(int argc, char **argv)
{
    // THE PART (the header's six rows): exactly one, or the run is refused - the parts stage their own state, so two in
    // one process would stage it twice.
    const std::string part = argc == 2 ? argv[1] : "";
    const bool doSchedule = part == "schedule", doFaults = part == "faults", doEdits = part == "edits",
               doAmbient = part == "ambient", doMover = part == "mover", doMoverRoutes = part == "mover_routes";
    if (!(doSchedule || doFaults || doEdits || doAmbient || doMover || doMoverRoutes)) {
        std::printf("FAIL: usage: test_gi_cascades_asan schedule|faults|edits|ambient|mover|mover_routes (got '%s')\n",
                    part.c_str());
        return 2;
    }
    std::printf("== part: %s ==\n", part.c_str());
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-cascades-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    // A 4 km ground so a 1 km jump still lands on something, a wall to bounce
    // from, and an ambient that is the ONLY light in case 4.
    View *view = e->createOffscreenView("cascades", kSize, kSize, Colour(0, 0, 0), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("cascades");
    view->setScene(scene);
    scene->setAmbient(Colour(0.35f, 0.35f, 0.35f), Colour(0.35f, 0.35f, 0.35f));
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(4000.0f, 0.1f, 4000.0f));
    const NodeId wall = enginetest::addTestCube(scene, Colour(0.9f, 0.05f, 0.05f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, wall, Vec3(0.0f, 3.0f, -6.0f));
    enginetest::setNodeScale(scene, wall, Vec3(12.0f, 6.0f, 0.2f));
    GiStatus st;   // (the cases' running read of the chain)

    // ---- A PART'S PRECONDITIONS, STAGED (HARNESS-2: each was the state an earlier case happened to leave) ----
    // THE CHAIN PARKED AT THE SUITE'S HOME POSE: up at (0, 2, 6), nothing pending and nothing scrolling - the precondition
    // of a case that counts rebuilds (an open's own N builds still in the queue would be counted as the case's). The
    // frames are spent only until the queue is empty; 240 (case 16b's park bound) is the ceiling only a stuck queue reaches.
    const auto chainOwes = [&]() {
        for (const GiStatus::CascadeStatus &c : scene->published().gi.cascades)
            if (c.pending || c.scrolling) return true;
        return false;
    };
    const auto parkAtHome = [&](const char *why) {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f)));
        CHECK(scene->setGlobalIllumination(cascadeGi()), why);
        render(e, 8);
        int parked = 8;
        for (; parked < 240 && chainOwes(); ++parked) render(e, 1);
        std::printf("   (staged: the chain parked at the home pose in %d frames)\n", parked);
        CHECK(!scene->published().gi.cascades.empty() && !chainOwes(),
              "staged: the chain is up and its queue is empty before the case counts anything");
    };
    // THE SCENE CASE 7's EDITS LEAVE - A DIRECTIONAL LAMP (the suite's ONLY direct light; the ambient is the rest) and three
    // cubes: the dragged one where its gesture ended, the painted one after its parameter edit and its two presets, the
    // promoted mover. Case 7 never removes them, so cases 12, 13 and 15 were measured in this scene (case 12's turner is lit
    // by this lamp and bounces it; case 15's lit voxels are its light). The parts that run them without case 7 build it
    // here with case 7's own calls, in its order. A dependency made explicit, not a change of scene.
    const auto stageEditedScene = [&]() {
        const NodeId dragged = enginetest::addTestCube(scene, Colour(0.2f, 0.8f, 0.2f), 0.0f, 0.8f);
        enginetest::setNodePosition(scene, dragged, Vec3(1.0f + 0.05f * 19.0f, 0.5f, 1.0f));
        const NodeId lamp = enginetest::addDirectionalLight(scene, Vec3(0.2f, -1.0f, -0.4f), 2.0f);
        LightDesc d;
        d.type = LightType::Directional;
        d.colour = Colour(1.0f, 0.4f, 0.2f);
        d.intensity = 3.0f;
        scene->setLight(lamp, d);
        const MeshId m = scene->createMesh(enginetest::unitCubeMesh());
        PbrParams pp; pp.albedo = Colour(0.1f, 0.1f, 0.9f); pp.roughness = 0.7f;
        const MaterialId mat = scene->createPbrMaterial(pp);
        const NodeId painted = scene->createNode();
        scene->attachMesh(painted, m, mat);
        enginetest::setNodePosition(scene, painted, Vec3(-1.5f, 0.5f, 1.0f));
        pp.albedo = Colour(0.9f, 0.9f, 0.1f);
        const bool edited = scene->setPbrMaterial(mat, pp) && scene->setShadingModel(mat, ShadingModel::Unlit) &&
                            scene->setShadingModel(mat, ShadingModel::Lit);
        const NodeId promoted = enginetest::addTestCube(scene, Colour(0.3f, 0.3f, 0.9f), 0.0f, 0.6f);
        enginetest::setNodePosition(scene, promoted, Vec3(2.0f, 0.5f, 2.0f));
        scene->setNodeMovable(promoted, true);
        render(e, 2);
        CHECK(dragged && lamp && painted && promoted && edited,
              "staged: case 7's scene - the lamp, the dragged, painted and promoted cubes");
    };

    if (doSchedule) {   // (schedule: cases 0, 1, 2, 2b, 3)
    // =====================================================================
    // CASE 0 — THE OPEN: the chain is built WHERE THE CAMERA IS
    // =====================================================================
    // A scene is opened and the document pushes its GI before the first frame
    // has rendered: nothing has tracked a camera yet. A camera-centred arm
    // built at that moment is built around the ORIGIN, and the first tracked
    // frame then finds every cascade a teleport away from the camera and
    // re-centres all of them, one per frame — N counted full rebuilds on every
    // open, the whole chain voxelised twice (audit B3).
    //
    // The rule is that there is nothing to build until a camera is known: the
    // arm waits, and the first tracked camera builds it once, in the right
    // place. Measured here as the rebuild total after an open: N, not 2N.
    std::printf("\n== case 0: the open ==\n");
    {
        // 200 m from the origin, so a chain built at the origin cannot overlap
        // a single one of its cascades — the honest form of the defect.
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(200.0f, 2.0f, 206.0f),
                                                         Vec3(200.0f, 1.0f, 200.0f)));
        const unsigned long long rebuildsBefore = scene->published().gi.rebuilds;
        CHECK(scene->setGlobalIllumination(cascadeGi()),
              "the cascade arm accepts before any frame has rendered");
        // WAITING IS NOT A REBUILD, and it says so rather than looking like a
        // failed build (round-2 review F4/F5). The host reads it off a frame's snapshot (U3 stage 2b): one frame
        // with the only view disabled — nothing draws the scene, so nothing tracks a camera — then the view back on.
        view->setEnabled(false);
        render(e, 1);
        const GiStatus waiting = scene->published().gi;
        view->setEnabled(true);
        CHECK(waiting.cascadesAwaitingCamera && waiting.cascades.empty(),
              "...and reports that it is WAITING FOR A CAMERA, not that it built nothing");
        CHECK(waiting.rebuilds == rebuildsBefore,
              "...without counting the wait as a from-scratch rebuild");
        render(e, 24);
        const GiStatus o = scene->published().gi;
        unsigned long long total = 0;
        for (const auto &c : o.cascades) total += c.rebuilds;
        std::printf("   after the open: %zu cascades, %llu rebuilds in total, "
                    "%llu full rebuilds, %llu deferrals\n",
                    o.cascades.size(), total, o.cascadeFullRebuilds, o.cascadeDeferrals);
        CHECK(!o.cascades.empty() && o.vctBound,
              "the chain is up and bound once a camera has been tracked");
        CHECK(!o.cascadesAwaitingCamera, "...and it is not waiting for anything any more");
        CHECK(o.rebuilds == rebuildsBefore + 1u,
              "THE WHOLE OPEN IS ONE COUNTED REBUILD (it was two: the wait and the build)");
        CHECK(total == (unsigned long long)o.cascades.size(),
              "EVERY CASCADE IS VOXELISED EXACTLY ONCE ON AN OPEN (not twice: origin, then camera)");
        CHECK(o.cascadeFullRebuilds == 0,
              "...and no cascade was dragged in from the origin by the teleport guard");
        for (const auto &c : o.cascades)
            if (std::fabs(c.centre.x - 200.0f) > c.halfSize) {
                CHECK(false, "every cascade was placed around the camera");
                break;
            }
        GiParams down; down.mode = GiMode::Off;
        CHECK(scene->setGlobalIllumination(down), "and it comes down again");
    }

    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f)));
    render(e, 4);

    // =====================================================================
    // CASE 1 — the chain builds, and its table is the one we asked for
    // =====================================================================
    std::printf("\n== case 1: the chain builds ==\n");
    CHECK(scene->setGlobalIllumination(cascadeGi()), "the cascade arm accepts");
    render(e, 4);
    st = scene->published().gi;
    CHECK(st.vctBound, "the chain's head is bound to the shader");
    CHECK(st.cascades.size() >= 2, "more than one cascade exists");
    float prevCell = 0.0f, prevHalf = 0.0f;
    bool stepsOk = true, orderOk = true;
    for (size_t i = 0; i < st.cascades.size(); ++i) {
        const GiStatus::CascadeStatus &c = st.cascades[i];
        std::printf("   c%zu half %6.2f m  res %3d  cell %.4f m  step %.3f m  centre %.2f,%.2f,%.2f\n",
                    i, c.halfSize, c.resolution, c.cell, c.step, c.centre.x, c.centre.y, c.centre.z);
        if (i && (c.halfSize <= prevHalf || c.cell <= prevCell)) orderOk = false;
        // Every cascade steps the same DISTANCE (within the ceil() the whole-cell
        // rounding forces), which is what keeps the chain from tearing.
        if (i && std::fabs(c.step - st.cascades[0].step) > 0.51f * c.cell) {
            // the outer cascades' step is allowed to be coarser, never finer
            if (c.step < st.cascades[0].step - 1e-3f) stepsOk = false;
        }
        prevCell = c.cell; prevHalf = c.halfSize;
    }
    CHECK(orderOk, "the cascades grow outward: every one is bigger and coarser than the last");
    CHECK(stepsOk, "no outer cascade steps more often than the innermost");
    CHECK(st.cascades[0].cell < 0.2f,
          "the innermost cascade resolves detail a room-sized volume cannot");

    // =====================================================================
    // CASE 2 — at rest it costs exactly nothing
    // =====================================================================
    std::printf("\n== case 2: at rest ==\n");
    const GiStatus rest0 = scene->published().gi;
    render(e, 60);
    const GiStatus rest1 = scene->published().gi;
    unsigned long long moved = 0;
    for (size_t i = 0; i < rest1.cascades.size(); ++i)
        moved += rest1.cascades[i].rebuilds - rest0.cascades[i].rebuilds;
    std::printf("   60 still frames: %llu cascade rebuilds\n", moved);
    CHECK(moved == 0, "a still camera re-voxelises nothing, for ever");
    CHECK(rest1.cascadeDeferrals == rest0.cascadeDeferrals, "and queues nothing");

    // =====================================================================
    // CASE 2b — A SWAYING CAMERA ON A STEP PLANE RE-VOXELISES NOTHING
    //          (the re-centre's hysteresis band; lane V1-RIG, 2026-09-18)
    //
    // The scroll test is "the camera is past the plane of the step cell it was
    // built in", and it used to have NO BAND: a camera oscillating across such a
    // plane re-voxelised on every crossing, for ever. In a headset that is not a
    // corner case — a wearer's head sway is centimetres and the runtime reports
    // every millimetre of it — and it was measured: a +-0.12 m sinusoid about
    // x = 0, which IS a step plane at every tier, cost 64 cascade rebuilds and 16
    // whole irradiance-field re-integrations in 200 STILL frames, 32 % of frames
    // doing 5.5-8.8 ms of GPU work each.
    //
    // The band is `kStepHysteresis` (0.1) of the step, applied on a lattice
    // SHRUNK by the same fraction, so the worst distance the camera can be from
    // the centre it was built at is unchanged at exactly one step. The two arms
    // here are the two things that must both be true: a sway INSIDE the band
    // costs nothing, and a walk PAST it still steps — exactly once.
    //
    // This case needs no runtime, no GPU timing and no headset: the subject is
    // the scheduler's arithmetic.
    // =====================================================================
    std::printf("\n== case 2b: a sway on a step plane, and a walk past the band ==\n");
    {
        const GiStatus s0 = scene->published().gi;
        const float stepH = s0.cascades[0].step;
        // PARK THE CAMERA ON A STEP PLANE. The lattice the test quantises on is
        // step*(1-0.1) metres, so x = 0 is a plane of it whatever the tier.
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        render(e, 8);
        const GiStatus a = scene->published().gi;
        unsigned long long beforeSway = 0;
        for (const auto &c : a.cascades) beforeSway += c.rebuilds;
        const unsigned long long followsBeforeSway = a.ifdFollows;

        // THE SWAY: 8 % of the step, which is inside the 10 % band with room to
        // spare (0.4 m of a 5 m step) and still crosses x = 0 on every cycle —
        // so the band is what stops it, not the amplitude. 200 frames, 12 frames
        // a cycle.
        const float amp = 0.08f * stepH;
        for (int f = 0; f < 200; ++f) {
            const float x = amp * std::sin(float(f) * 3.14159265f / 12.0f);
            view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 6.0f),
                                                             Vec3(x, 1.0f, 0.0f)));
            render(e, 1);
        }
        const GiStatus b = scene->published().gi;
        unsigned long long afterSway = 0;
        for (const auto &c : b.cascades) afterSway += c.rebuilds;
        std::printf("   200 frames of +-%.2f m sway about a step plane (step %.2f m, "
                    "band %.2f m): %llu cascade rebuilds, %llu field re-placements\n",
                    amp, stepH, 0.1f * stepH, afterSway - beforeSway,
                    b.ifdFollows - followsBeforeSway);
        CHECK(afterSway == beforeSway,
              "A SWAY INSIDE THE BAND RE-VOXELISES NOTHING (it crossed the plane ~16 times)");
        CHECK(b.ifdFollows == followsBeforeSway, "...and re-places the field not once");

        // THE WALK PAST THE BAND: 1.1 steps in one move is past
        // step*(1-h) + h*step = step, so it must trip, and having tripped the
        // camera is inside its NEW cell's band — so exactly one step, not two.
        unsigned long long c0Before = b.cascades[0].rebuilds;
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(1.1f * stepH, 2.0f, 6.0f),
                                                         Vec3(1.1f * stepH, 1.0f, 0.0f)));
        render(e, 12);
        const GiStatus c = scene->published().gi;
        std::printf("   a %.2f m move (1.1 steps): c0 rebuilds %llu -> %llu\n",
                    1.1f * stepH, c0Before, c.cascades[0].rebuilds);
        CHECK(c.cascades[0].rebuilds == c0Before + 1ull,
              "a walk PAST the band steps cascade 0 exactly once");

        // ...AND PUT THE CAMERA BACK where this case found it, so CASE 3's walk
        // is the forward-only one it was written as (it takes its own fresh
        // baseline below; this only keeps the walk's direction honest).
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        render(e, 12);
    }

    // =====================================================================
    // CASE 3 — a scroll re-centres, at most one cascade per frame
    // =====================================================================
    std::printf("\n== case 3: a scroll ==\n");
    // A FRESH BASELINE, not `rest1`'s: case 2b walked the camera and spent a
    // rebuild, so the at-rest snapshot no longer describes where the chain is.
    // (Reading a stale baseline here made the first frame of the walk look like
    // it had paid for two cascades.)
    const GiStatus scroll0 = scene->published().gi;
    const float step0 = scroll0.cascades[0].step;
    const Vec3 centreBefore = scroll0.cascades[0].centre;
    unsigned long long before = 0, worstFrame = 0;
    for (const auto &c : scroll0.cascades) before += c.rebuilds;
    // Walk 6 of cascade 0's steps, one frame per 1/8 step, and watch the total
    // rebuild count per FRAME.
    unsigned long long prevTotal = before;
    for (int f = 1; f <= 48; ++f) {
        const float x = float(f) * step0 / 8.0f;
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 6.0f),
                                                         Vec3(x, 1.0f, 0.0f)));
        render(e, 1);
        const GiStatus s = scene->published().gi;
        unsigned long long t = 0;
        for (const auto &c : s.cascades) t += c.rebuilds;
        worstFrame = std::max(worstFrame, t - prevTotal);
        prevTotal = t;
    }
    st = scene->published().gi;
    std::printf("   after %.1f m of travel: c0 centre %.2f -> %.2f, rebuilds %llu, "
                "worst frame %llu rebuild(s), deferrals %llu, dirtyMajority %llu\n",
                6.0f * step0, centreBefore.x, st.cascades[0].centre.x,
                prevTotal - before, worstFrame, st.cascadeDeferrals, st.cascadeDirtyMajority);
    CHECK(st.cascades[0].centre.x > centreBefore.x + step0 * 0.5f,
          "cascade 0 followed the camera");
    CHECK(prevTotal > before, "the walk re-voxelised something");
    CHECK(worstFrame <= 1, "NO FRAME EVER PAYS FOR TWO CASCADES (the one-per-frame budget)");
    CHECK(st.cascadeFullRebuilds == 0, "an ordinary walk never trips the teleport guard");
    }   // (schedule: cases 0, 1, 2, 2b, 3)

    if (doAmbient) {   // (ambient: case 4)
    // =====================================================================
    // CASE 4 — the ambient is in every cascade (upstream's 2,2,2 room)
    // =====================================================================
    std::printf("\n== case 4: the ambient reaches every cascade ==\n");
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 60.0f),
                                                         Vec3(0.0f, 1.0f, 40.0f)));
        // Far from the wall, so the ONLY thing lighting the ground here is the
        // ambient — and the camera is beyond the inner cascades' reach of the
        // scene's geometry, which is exactly where a cascade with black
        // hemispheres would print itself on the floor.
        Image img;
        const auto measureArm = [&](const GiParams &p, const char *what) {
            CHECK(scene->setGlobalIllumination(p), what);
            render(e, 8);
            view->readPixels(img);
            return groundLum(img);
        };
        GiParams off = cascadeGi(); off.mode = GiMode::Off;
        GiParams one = cascadeGi();
        one.cascadeCount = 1;
        one.cascadeSet[0] = GiParams::GiCascadeDesc{ 5.0f, 128, 0.0f };

        const float ambientOff = measureArm(off, "GI off");
        // HOW THE LOSS SCALES WITH THE CHAIN LENGTH — the diagnostic that says
        // whether it is the chain's composite (per-hop) or the cascades
        // themselves (per-cascade placement).
        for (int n = 1; n <= 4; ++n) {
            GiParams p = cascadeGi();
            p.cascadeCount = n;
            const float halves[4] = { 5.0f, 10.0f, 15.0f, 60.0f };
            const int   ress[4]   = { 128, 128, 64, 64 };
            for (int i = 0; i < n; ++i) p.cascadeSet[i] = GiParams::GiCascadeDesc{ halves[i], ress[i], 0.0f };
            const float v = measureArm(p, n == 1 ? "chain length sweep" : "chain length sweep");
            std::printf("   chain of %d: ambient %.4f (%.2fx of GI off)\n", n, v, v / ambientOff);
        }
        const float ambientOne = measureArm(one, "a chain of ONE camera-centred cascade");
        const float ambientChain = measureArm(cascadeGi(), "the full chain again");
        std::printf("   ground lit by the ambient alone: GI off %.4f "
                    "| 1 cascade %.4f (%.2fx) | %zu cascades %.4f (%.2fx)\n",
                    ambientOff, ambientOne, ambientOne / ambientOff,
                    scene->published().gi.cascades.size(), ambientChain, ambientChain / ambientOff);
        // THE CONTRACT IS AGAINST ONE CAMERA-CENTRED CASCADE, not against GI-off:
        // cone-traced diffuse always eats some of the ambient (the cone's own
        // starting surface occludes it — fork 8f09c0cd4 (was 0021) measures 65% surviving
        // on an open floor), and that is a property of VCT, not of cascades. What
        // must not happen is the chain being DARKER than one volume: that is the
        // 2,2,2 room, and it is what upstream's cascade manager renders.
        // ONE CASCADE IS THE STRUCTURAL TEST: it proves the ambient pair really
        // is pushed into a camera-centred volume (upstream never pushes it at all).
        CHECK(ambientOne > 0.02f, "a camera-centred cascade carries the ambient");
        // A CHAIN READS THE OPEN GROUND AS ONE VOLUME DOES (PHOTON-VOXEL-4, SUITES-REANCHOR-1).
        // The ground is open: in every cascade the four cones leave the one surface they stand on
        // and see nothing else, so the chain's own quadrature of it - the same cones over the
        // chain's stores, each march handed to the next cascade - is one volume's, exactly. The
        // bar is the pictures' quantum: two block means of 8-bit pixels, half a code each. (The
        // reference was the single scene-fitted volume until D4-PHOTON-TIERS deleted it; one
        // camera-centred cascade is the same physics without the fixed volume.)
        const float kQuantum = 1.0f / 255.0f;
        CHECK(std::fabs(ambientChain - ambientOne) <= kQuantum,
              "a four-cascade chain reads the open ground's ambient as one cascade does (within one code)");
        CHECK(ambientChain > 0.02f, "and it is a lit picture, not a black one");

        // ---- THE FIELD VARIANT (PHOTON_SPEC §13 G3, audit B1's own test) ----
        //
        // The bar G3 exists for. An irradiance field makes HlmsPbs switch the
        // cone-traced diffuse off scene-wide (it assumes the field covers what
        // the cones cover). Under a chain the field rides CASCADE 0, so with
        // that rule in force every pixel out here — 60 m from the content, in
        // the outer cascade — would lose its diffuse bounce entirely and the
        // outer cascades would be computing something nothing reads. G3-a
        // re-opens the gate under a chain (FogHlmsListener::propertiesMerged-
        // PreGenerationStep) and blends the two terms by the field's own
        // confidence in JahIfd: the field inside cascade 0, the cones outside.
        //
        // So: the chain with the field ON against the same chain with it off. Out
        // here the pixel is beyond cascade 0 (the field's box), so the bound field
        // must change nothing: the outer cascades' cones are the whole answer.
        GiParams chainF  = cascadeGi(); chainF.ddgi = GiToggle::On; chainF.updateBudget = 1;
        const float fieldChain = measureArm(chainF, "the chain with the field on");
        const GiStatus fst = scene->published().gi;
        std::printf("   WITH THE FIELD: chain %.4f (%.2fx of the chain without it, %.2fx of GI "
                    "off) | field bound %s, %d probes\n",
                    fieldChain, ambientChain > 0.0f ? fieldChain / ambientChain : 0.0f,
                    ambientOff > 0.0f ? fieldChain / ambientOff : 0.0f,
                    fst.ifdBound ? "y" : "n", fst.ifdProbes);
        CHECK(fst.ifdBound, "the chain really has a field bound");
        CHECK(std::fabs(fieldChain - ambientChain) <= kQuantum,
              "A PIXEL BEYOND CASCADE 0 READS THE CHAIN'S CONES WITH THE FIELD BOUND (within one code)");
        CHECK(fieldChain > 0.02f, "...and it is a lit picture, not a black one");
    }
    }   // (ambient: case 4)

    if (doSchedule) {   // (schedule: cases 5, 6)
    // STAGED (HARNESS-2): the teleport was always measured from the chain case 4 left - the FIELD BOUND (ddgi on, budget 1)
    // at (0, 2, 60) after its 8 frames - so its settle window shares the slot with the field's follow (the 2n + 2 bound).
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 60.0f), Vec3(0.0f, 1.0f, 40.0f)));
        GiParams chainF = cascadeGi(); chainF.ddgi = GiToggle::On; chainF.updateBudget = 1;
        CHECK(scene->setGlobalIllumination(chainF), "staged: the chain with the field on, where case 4 left it");
        render(e, 8);
    }
    // =====================================================================
    // CASE 5 + 6 — the teleport: bounded, counted, and never black
    // =====================================================================
    std::printf("\n== case 5: a 1 km teleport ==\n");
    {
        const GiStatus b = scene->published().gi;
        const size_t n = b.cascades.size();
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(1000.0f, 2.0f, 1000.0f),
                                                         Vec3(1000.0f, 1.0f, 980.0f)));
        float darkest = 1e9f;
        size_t framesToSettle = 0;
        for (size_t f = 0; f < 4u * n + 4u; ++f) {
            render(e, 1);
            Image img; view->readPixels(img);
            darkest = std::min(darkest, groundLum(img));
            const GiStatus s = scene->published().gi;
            if (!framesToSettle && s.cascadeFullRebuilds - b.cascadeFullRebuilds >= n)
                framesToSettle = f + 1;
        }
        st = scene->published().gi;
        std::printf("   full rebuilds %llu (cascades %zu), settled after %zu frames, "
                    "darkest ground during the window %.4f\n",
                    st.cascadeFullRebuilds - b.cascadeFullRebuilds, n, framesToSettle, darkest);
        CHECK(st.cascadeFullRebuilds - b.cascadeFullRebuilds == (unsigned long long)n,
              "the guard fired exactly once per cascade — a teleport is N rebuilds, not a storm");
        CHECK(framesToSettle >= n, "and they were spread over frames, never paid in one");
        CHECK(framesToSettle <= 2u * n + 2u, "the chain is whole again within a fifth of a second");
        CHECK(darkest > 0.02f, "NO FRAME OF THE TELEPORT WINDOW IS BLACK");
        for (const auto &c : st.cascades)
            if (std::fabs(c.centre.x - 1000.0f) > c.halfSize) {
                CHECK(false, "every cascade ended up around the new camera");
                break;
            }
        std::printf("   every cascade re-centred on the new position\n");
    }

    // =====================================================================
    // CASE 6 — a cascade standing in EMPTY SPACE
    // =====================================================================
    // `VctVoxelizer::build` sizes its instance-to-world job from the instances
    // that survive the region cull, and Ogre refuses a compute job whose thread
    // groups multiply to zero — so a region containing nothing THROWS
    // (measured on an 8,404-node lattice, where it left a chain of cascades
    // holding nothing and a scene rendering with no GI and no visible error).
    // For a CAMERA-CENTRED volume that is not a corner case: fly off the edge
    // of a scene and the inner cascade is empty by definition.
    std::printf("\n== case 6: a cascade in empty space ==\n");
    {
        GiParams tiny = cascadeGi();
        tiny.cascadeCount = 2;
        tiny.cascadeSet[0] = GiParams::GiCascadeDesc{ 1.0f, 128, 0.0f };
        tiny.cascadeSet[1] = GiParams::GiCascadeDesc{ 4.0f, 64, 0.0f };
        // 80 m above the ground: nothing at all reaches either cascade.
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 80.0f, 0.0f),
                                                         Vec3(0.0f, 80.0f, -20.0f)));
        render(e, 2);
        CHECK(scene->setGlobalIllumination(tiny), "a chain whose cascades contain nothing builds");
        render(e, 6);
        st = scene->published().gi;
        CHECK(st.cascades.size() == 2, "and it is a whole chain, not a half-built one");
        CHECK(st.vctBound, "and it is bound");
        Image img; view->readPixels(img);
        CHECK(groundLum(img) >= 0.0f, "and the frame renders");
        // ...and it recovers when the camera comes back down to the geometry.
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        render(e, 8);
        st = scene->published().gi;
        CHECK(st.cascades.size() == 2 && st.cascades[0].rebuilds > 0,
              "and the cascades re-fill when the camera returns to the geometry");
    }
    }   // (schedule: cases 5, 6)

    if (doAmbient) {   // (ambient: case 7 (specular))
    // =====================================================================
    // CASE 7 — THE SPECULAR AMBIENT SURVIVES THE CHAIN (fork 8f09c0cd4 (was 0033))
    // =====================================================================
    // The chain's cone walk is run TWICE per pixel — once for the diffuse
    // cones and once for the specular one — and fork 8f09c0cd4 (was 0033) originally fixed
    // only the first. The specular continuation kept `irrLight.alpha +=
    // newRes.alpha` (the opacity the call already returns as a running total,
    // so adding it doubles it per hop) and re-applied no self-occlusion bias at
    // the new cascade's entry, so `specAlpha = 1 - min(1, alpha/0.95)` reached
    // 0 after one hop and the ambient term in rough reflections — the sky in a
    // mirror — vanished at 2+ cascades (audit B5).
    //
    // Measured on a SMOOTH METAL plate whose albedo is black: it has no diffuse
    // term at all, so what it renders is the specular cone plus the ambient the
    // escape weight lets through. Far from the scene's wall, pointed at open
    // space, so the ambient is what dominates it.
    std::printf("\n== case 7: the specular ambient under a chain ==\n");
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(40.0f, 2.0f, 47.0f),
                                                         Vec3(40.0f, 0.55f, 40.0f)));
        Image img;
        const float roughs[4] = { 0.15f, 0.35f, 0.55f, 0.75f };
        for (int r = 0; r < 4; ++r) {
            const NodeId plate = enginetest::addTestCube(scene, Colour(0.95f, 0.95f, 0.95f),
                                                         1.0f /*metal*/, roughs[r]);
            enginetest::setNodePosition(scene, plate, Vec3(40.0f, 0.5f, 40.0f));
            enginetest::setNodeScale(scene, plate, Vec3(10.0f, 0.2f, 10.0f));
            render(e, 2);
            const auto plateLum = [&](int n) {
                GiParams p = cascadeGi();
                p.cascadeCount = n;
                const float halves[4] = { 5.0f, 10.0f, 15.0f, 60.0f };
                const int   ress[4]   = { 128, 128, 64, 64 };
                for (int i = 0; i < n; ++i)
                    p.cascadeSet[i] = GiParams::GiCascadeDesc{ halves[i], ress[i], 0.0f };
                scene->setGlobalIllumination(p);
                render(e, 8);
                view->readPixels(img);
                return groundLum(img);
            };
            const float l1 = plateLum(1), l2 = plateLum(2), l4 = plateLum(4);
            std::printf("   roughness %.2f: 1 cascade %.4f | 2 %.4f (%.2fx) | 4 %.4f (%.2fx)\n",
                        roughs[r], l1, l2, l2 / std::max(l1, 1e-6f), l4, l4 / std::max(l1, 1e-6f));
            // THE ASSERTION IS ON THE SMOOTHEST PLATE, and the rest of the sweep
            // is printed as evidence rather than pinned: a wide cone from a
            // plate lying on a ground plane is LEGITIMATELY occluded (half of
            // it points into the floor), so at roughness 0.55 the term is a few
            // thousandths either way and a ratio over it is noise. At 0.15 the
            // reflection is of open space, the ambient is the whole reading,
            // and the defect was unmistakable: 1.00 / 0.56 / 0.00 before the
            // amendment to fork 8f09c0cd4 (was 0033), 1.00 / 0.85 / 0.85 after. The
            // residual 0.85 is the same march-distance restart the diffuse half
            // leaves at 0.51 (the patch header; OGRE_UPSTREAM_ISSUES).
            if (r == 0) {
                CHECK(l1 > 0.02f, "a chain of ONE lights the plate through the ambient (reference)");
                CHECK(l2 > 0.70f * l1,
                      "TWO cascades keep the ambient in the reflection (it was lost after ONE hop)");
                CHECK(l4 > 0.70f * l1, "...and FOUR still do (it was black)");
            }
            scene->removeNode(plate);
            render(e, 2);
        }
    }
    }   // (ambient: case 7 (specular))

    if (doSchedule) {   // (schedule: cases 8, 9, 10)
    // =====================================================================
    // CASE 8 — A PINNED TABLE THAT CANNOT BE HONOURED IS REFUSED WHOLE
    // =====================================================================
    // The same rule the mirror states for a half-specified row (audit B10): a
    // request the renderer cannot honour is not honoured halfway. A table whose
    // cascades do not grow outward gives Ogre a NEGATIVE `cascadeMaxLod` and
    // the march never hands over; a pinned step below the lattice's two cells
    // re-centres every frame, and one above half the resolution lets the camera
    // leave the box.
    std::printf("\n== case 8: pinned tables are validated ==\n");
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        GiParams bad = cascadeGi();
        bad.cascadeCount = 3;
        bad.cascadeSet[0] = GiParams::GiCascadeDesc{  5.0f, 128, 0.0f };
        bad.cascadeSet[1] = GiParams::GiCascadeDesc{ 10.0f, 128, 0.0f };
        bad.cascadeSet[2] = GiParams::GiCascadeDesc{  8.0f, 128, 0.0f };   // SMALLER
        CHECK(scene->setGlobalIllumination(bad), "the arm accepts the call");
        render(e, 6);
        st = scene->published().gi;
        std::printf("   non-monotonic table -> %zu cascades (outermost half %.1f m)\n",
                    st.cascades.size(), st.cascades.empty() ? 0.0f : st.cascades.back().halfSize);
        CHECK(st.cascades.size() == 4 && std::fabs(st.cascades.back().halfSize - 60.0f) < 0.01f,
              "a table that does not grow outward is dropped WHOLE and the tier's is used");

        GiParams steps = cascadeGi();
        steps.cascadeCount = 2;
        steps.cascadeSet[0] = GiParams::GiCascadeDesc{  5.0f, 128,    0.2f };   // below a cell
        steps.cascadeSet[1] = GiParams::GiCascadeDesc{ 20.0f,  64, 10000.0f };  // runaway
        CHECK(scene->setGlobalIllumination(steps), "a table with out-of-range steps is accepted");
        render(e, 6);
        st = scene->published().gi;
        std::printf("   pinned steps -> c0 step %.4f m (cell %.4f), c1 step %.3f m (cell %.3f)\n",
                    st.cascades[0].step, st.cascades[0].cell,
                    st.cascades[1].step, st.cascades[1].cell);
        CHECK(st.cascades.size() == 2, "the table itself is honoured");
        // (THE FLOOR IS THE LATTICE's, PHOTON-7 stage 10, 10-2e: the step IS the placement quantum Q = 2^K cells, K >= 1 -
        // the even placement every cascade has always stood on (a one-cell step was never a placement: the centre moved
        // by two cells). Was "floored at one cell".)
        CHECK(std::fabs(st.cascades[0].step - 2.0f * st.cascades[0].cell) < 1e-4f,
              "a step below the lattice's floor is floored at TWO cells (Q = 2, the even placement)");
        CHECK(std::fabs(st.cascades[1].step - st.cascades[1].cell * 32.0f) < 1e-3f,
              "and a step beyond half the resolution is capped there");
    }

    // =====================================================================
    // CASE 9 — WHAT THE MONITOR SEES: the camera's own work, one row a frame
    // =====================================================================
    // A scroll rebuild used to be filed under `mLastStaleReason` — whatever
    // last staled the PROBE grid, possibly an edit from minutes ago — so a
    // capture said a walk was "material" or "light" work (audit B12/D5). The
    // reason a camera-following cache re-does work is the CAMERA, and it is the
    // number the cadence gate needs: how much of a session's GI cost is
    // exploring, and how much is editing.
    std::printf("\n== case 9: the monitor's rows ==\n");
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 6.0f),
                                                         Vec3(0.0f, 1.0f, 0.0f)));
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up for the walk");
        render(e, 8);
        e->setFrameMonitor(MonitorLevel::Review);
        render(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
        const float step = scene->published().gi.cascades[0].step;
        for (int f = 1; f <= 40; ++f) {
            const float x = float(f) * step / 4.0f;
            view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 6.0f),
                                                             Vec3(x, 1.0f, 0.0f)));
            render(e, 1);
        }
        std::vector<MonitorRecord> recs;
        e->popMonitorRecords(recs);
        unsigned rows = 0, camera = 0, other = 0, worstPerFrame = 0, counterTotal = 0;
        for (const MonitorRecord &r : recs) {
            unsigned inFrame = 0;
            for (const CacheWork &w : r.cacheWork) {
                if (w.cache != CacheKind::Gi) continue;
                if (std::string(w.detail).rfind("vct.cascade", 0) != 0) continue;
                ++rows; ++inFrame;
                if (w.reason == WorkReason::Camera) ++camera; else ++other;
            }
            worstPerFrame = std::max(worstPerFrame, inFrame);
            counterTotal += r.cascadeRebuilds;
            if (r.cascadeRebuilds > 1)
                CHECK(false, "a frame reported more than one cascade rebuild");
        }
        e->setFrameMonitor(MonitorLevel::Off);
        std::printf("   %zu frames: %u cascade rows (%u Camera, %u other), worst frame %u, "
                    "frame counter total %u\n",
                    recs.size(), rows, camera, other, worstPerFrame, counterTotal);
        CHECK(rows > 0, "the walk filed cascade rebuild rows");
        CHECK(other == 0 && camera == rows,
              "EVERY SCROLL REBUILD IS FILED UNDER `Camera`, not under the last probe reason");
        CHECK(worstPerFrame <= 1, "...at most one row per frame");
        CHECK(counterTotal == rows,
              "...and MonitorRecord::cascadeRebuilds counts exactly them");
    }

    // =====================================================================
    // CASE 10 — `items` IS WHAT THIS REBUILD VOXELISED
    // =====================================================================
    // `GiStatus::cascades[].items` promises "inside its box" (every size, at every
    // cascade: the sub-voxel cull is deleted, PHOTON-7 10-3). It used to be counted inside setCascadeItems,
    // which by rule 1 runs only when the ATTACH SET changes — so the number
    // froze at the last selection and a cascade that scrolled across a room
    // went on reporting the contents of the room it had left (round-2 review
    // F1). Here the wall leaves cascade 0's 5 m box as the camera walks away
    // from it, and the number has to say so.
    std::printf("\n== case 10: items follows the cascade ==\n");
    {
        // A prop where the camera starts, so cascade 0's 5 m box holds the
        // ground AND it; the scene's wall is 12 m away and was never in it.
        const NodeId prop = enginetest::addTestCube(scene, Colour(0.7f, 0.7f, 0.2f), 0.0f, 0.6f);
        enginetest::setNodePosition(scene, prop, Vec3(0.0f, 1.0f, 6.0f));
        enginetest::setNodeScale(scene, prop, Vec3(1.2f, 1.2f, 1.2f));
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 9.0f),
                                                         Vec3(0.0f, 1.0f, 6.0f)));
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up around the prop");
        render(e, 8);
        CHECK(settleReadings(e, scene) >= 0,   // the voxel read-back lands frames late
              "the voxel read-back landed: every cascade's reading is its newest gather's");
        st = scene->published().gi;
        const int near = st.cascades[0].items;
        const float step = st.cascades[0].step;
        for (int f = 1; f <= 40; ++f) {
            const float x = float(f) * step / 4.0f;
            view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 9.0f),
                                                             Vec3(x, 1.0f, 6.0f)));
            render(e, 1);
        }
        CHECK(settleReadings(e, scene) >= 0,   // the voxel read-back lands frames late
              "the voxel read-back landed: every cascade's reading is its newest gather's");
        st = scene->published().gi;
        const int far = st.cascades[0].items;
        std::printf("   cascade 0 items: %d beside the prop -> %d %.0f m away\n",
                    near, far, 10.0f * step);
        CHECK(near >= 2, "beside the prop the inner cascade voxelises the ground AND the prop");
        CHECK(far < near, "walking away DROPS the prop from what it voxelises");
        CHECK(far >= 1, "...and the ground is still in it");
        scene->removeNode(prop);
        render(e, 4);
    }
    }   // (schedule: cases 8, 9, 10)

    if (doFaults) {   // (faults: case 11)
    parkAtHome("staged: the chain is up for the failed scroll");
    // =====================================================================
    // CASE 11 — A REBUILD THAT THROWS CHANGES NOTHING BUT THE CLOCK
    // =====================================================================
    // The scheduler re-centres a cascade BEFORE building it, and the
    // voxeliser's region is read live by the shader — so a build that throws
    // would leave the new region mapped onto the old place's voxels: a wrong
    // bounce, silently, until the next scroll (audit B4). The revert path had
    // never executed, which is not a proof of anything (round-2 review F2), so
    // one build is forced to throw here through the same kind of fault switch
    // the texture-wait budget uses (JAH_TEXTURE_WAIT_FAULT).
    std::printf("\n== case 11: a failed rebuild ==\n");
    {
        st = scene->published().gi;
        const GiStatus::CascadeStatus c0 = st.cascades[0];
        const float step = c0.step;
        const float x0 = c0.centre.x;
        e->setArm("gi.cascadeFault", 0.0);   // latched at the next frame's top
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(x0 + 1.5f * step, 2.0f, 6.0f),
                                                         Vec3(x0 + 1.5f * step, 1.0f, 0.0f)));
        render(e, 1);
        GiStatus bad = scene->published().gi;
        std::printf("   forced failure: rebuilds %llu -> %llu, centre %.2f -> %.2f, pending %d\n",
                    c0.rebuilds, bad.cascades[0].rebuilds, c0.centre.x, bad.cascades[0].centre.x,
                    bad.cascades[0].pending);
        CHECK(bad.cascades[0].rebuilds == c0.rebuilds,
              "a rebuild that threw is NOT counted as one");
        CHECK(std::fabs(bad.cascades[0].centre.x - c0.centre.x) < 1e-3f,
              "AND THE CASCADE KEEPS THE PLACEMENT ITS VOXELS ARE FOR (the revert path)");
        CHECK(bad.cascades[0].pending != 0, "...with the rebuild still owed");
        CHECK(scene->published().gi.vctBound, "...and the chain still bound and rendering");
        e->setArm("gi.cascadeFault", -1.0);
        // THE RETRY IS THE CASCADE'S SCROLL (PHOTON-7 stage 2's schedule): cascade 0's camera move takes the scroll path,
        // spread over its tier's count of chunk frames (giCascadeScrollFrames) and committed in the frame after the last
        // of them - so it lands within the count and its commit's frame, never later (the lag law of gi.scroll_lag).
        GiParams::GiCascadeDesc d;
        d.halfSize = c0.halfSize;
        d.resolution = c0.resolution;
        d.stepCells = c0.step / c0.cell;
        const int bound = giCascadeScrollFrames(d) + 1;
        int retry = 0;
        for (; retry < bound && scene->published().gi.cascades[0].rebuilds == c0.rebuilds; ++retry) render(e, 1);
        st = scene->published().gi;
        std::printf("   after the fault is cleared: rebuilds %llu after %d frame(s) (the scroll's count and its commit: %d), "
                    "centre %.2f, pending %d\n",
                    st.cascades[0].rebuilds, retry, bound, st.cascades[0].centre.x, st.cascades[0].pending);
        CHECK(st.cascades[0].rebuilds == c0.rebuilds + 1u,
              "the retry succeeds within the cascade's scroll count and its commit's frame");
        // A STEP OF 1.5 STEPS IS PAST THE SCRATCH'S LAYERS (one step's slab and a frame's travel), and the camera moved on
        // more than one axis: the retry's scroll is CUT, the other axes and the cut's remainder follow as their own scrolls
        // (PhotonComponent::planScroll: an axis left out asks again) - each within its count and commit frame, at most one
        // a moved axis plus the cut's remainder.
        int rest = 0;
        for (; rest < 4 * bound &&
               (scene->published().gi.cascades[0].pending || scene->published().gi.cascades[0].scrolling);
             ++rest)
            render(e, 1);
        st = scene->published().gi;
        std::printf("   ...the rest: %llu more scroll(s) in %d frame(s), centre %.2f %.2f %.2f, pending %d\n",
                    st.cascades[0].scrolls - bad.cascades[0].scrolls - 1u, rest, st.cascades[0].centre.x,
                    st.cascades[0].centre.y, st.cascades[0].centre.z, st.cascades[0].pending);
        CHECK(st.cascades[0].centre.x > c0.centre.x + step * 0.5f,
              "...and the cascade is where the camera is");
        CHECK(st.cascades[0].pending == 0, "...and owes nothing once the cut's remainder has scrolled");
    }
    }   // (faults: case 11)

    if (doEdits) {   // (edits: case 7 (edits))
    parkAtHome("staged: the chain is up, still, for the edits");
    // =====================================================================
    // CASE 7 — AN EDIT IS NOT A CHAIN REBUILD (PHOTON_SPEC G1)
    // =====================================================================
    // THE DEFECT THIS CLOSES, in one line: until the per-cascade dirty path,
    // every settle re-solve, material edit, spawn, hide, delete and mobility
    // flip took `refreshVctFast` (which refuses under cascades) straight to
    // `rebuildVct` -> `teardownVct` + `buildCascadeArm` — N voxelisers destroyed
    // and rebuilt FROM SCRATCH IN ONE FRAME, with every mesh buffer re-derived
    // and re-uploaded because `removeAllItems` clears the voxeliser's mesh
    // bookkeeping. Cases 2, 3 and 5 all measure a still or walking CAMERA and
    // never touch the scene, so the whole class was invisible to this suite
    // (audit B2's own note).
    //
    // What is asserted, for every kind of edit, is the same three things:
    //   * ZERO whole-chain builds — `GiStatus::rebuilds` (bumped by every
    //     `rebuildVct`) does not move;
    //   * never more than ONE cascade re-voxelised in any single frame;
    //   * and no cascade is left owing a rebuild once the queue has drained.
    //
    // The camera stands still throughout, so every rebuild counted here was
    // paid for by the EDIT and by nothing else.
    std::printf("\n== case 7: edits under the chain ==\n");
    {
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up for the edit cases");
        render(e, 8);
        const size_t nCascades = scene->published().gi.cascades.size();

        // A PLACE ONLY THE OUTERMOST CASCADE REACHES, derived from the chain as
        // it actually stands: the camera has been walked and teleported by the
        // cases above, so a hard-coded world coordinate measures nothing (it was
        // 96 m from the chain the first time this was written).
        const auto farPlace = [&]() {
            const auto &cs = scene->published().gi.cascades;
            const float inner = cs[cs.size() - 2u].halfSize, outer = cs.back().halfSize;
            return Vec3(cs.back().centre.x + (inner + outer) * 0.5f, 0.5f, cs.back().centre.z);
        };
        const auto chainRebuilds = [&]() {
            unsigned long long t = 0;
            for (const auto &c : scene->published().gi.cascades) t += c.rebuilds;
            return t;
        };
        const auto pendingCount = [&]() {
            int n = 0;
            for (const auto &c : scene->published().gi.cascades) n += c.pending ? 1 : 0;
            return n;
        };
        // Runs `frames` frames one at a time after an edit and reports the worst
        // single-frame cascade-rebuild count and the total. `arm0` is the
        // whole-chain build count sampled BEFORE the edit — and it has to be,
        // because `refreshGlobalIllumination()` is synchronous: on the base
        // engine the chain build lands inside that call, not inside the frames
        // that follow it.
        const auto measure = [&](const char *what, unsigned long long arm0, int frames,
                                 unsigned long long &outTotal, int &outWorst) {
            unsigned long long prev = chainRebuilds();
            const unsigned long long first = prev;
            int worst = 0;
            for (int i = 0; i < frames; ++i) {
                render(e, 1);
                const unsigned long long now = chainRebuilds();
                worst = std::max(worst, int(now - prev));
                prev = now;
            }
            outTotal = prev - first;
            outWorst = worst;
            const unsigned long long arm1 = scene->published().gi.rebuilds;
            std::printf("   %-22s chain builds %llu -> %llu, cascade rebuilds %llu "
                        "(worst frame %d), pending left %d\n",
                        what, arm0, arm1, outTotal, worst, pendingCount());
            char msg[256];
            std::snprintf(msg, sizeof(msg), "%s: ZERO whole-chain builds", what);
            CHECK(arm1 == arm0, msg);
            std::snprintf(msg, sizeof(msg), "%s: never two cascades in one frame", what);
            CHECK(worst <= 1, msg);
            std::snprintf(msg, sizeof(msg), "%s: the queue drained", what);
            CHECK(pendingCount() == 0, msg);
        };
        unsigned long long total = 0; int worst = 0;

        // ---- (a) a box dragged and RELEASED --------------------------------
        // A STILL OBJECT THAT IS MOVING IS RE-VOXELISED WHILE IT MOVES (smoke
        // rig 2026-09-15, ledger 320): leaving it until the settle is what left
        // a dragged cube lit against a voxel copy of itself at its old pose, so
        // the drag costs AT MOST ONE cascade per frame — the same budget a
        // camera scroll lives inside — and the release costs the cascades that
        // can SEE the box, never the chain.
        const NodeId dragged = enginetest::addTestCube(scene, Colour(0.2f, 0.8f, 0.2f), 0.0f, 0.8f);
        enginetest::setNodePosition(scene, dragged, Vec3(1.0f, 0.5f, 1.0f));
        render(e, 8);                                    // the spawn's own work drains
        {
            const unsigned long long arm0 = scene->published().gi.rebuilds;
            unsigned long long prev = chainRebuilds();
            const unsigned long long dragFirst = prev;
            int worstDrag = 0;
            for (int i = 0; i < 20; ++i) {               // the gesture: no settle asked for
                enginetest::setNodePosition(scene, dragged,
                                            Vec3(1.0f + 0.05f * float(i), 0.5f, 1.0f));
                render(e, 1);
                const unsigned long long now = chainRebuilds();
                worstDrag = std::max(worstDrag, int(now - prev));
                prev = now;
            }
            std::printf("   the drag itself: chain builds %llu -> %llu, cascade rebuilds %llu, "
                        "worst frame %d\n",
                        arm0, scene->published().gi.rebuilds, prev - dragFirst, worstDrag);
            CHECK(scene->published().gi.rebuilds == arm0,
                  "a drag in flight costs ZERO whole-chain builds");
            CHECK(worstDrag <= 1,
                  "...and never more than one cascade in a frame");
            CHECK(prev - dragFirst >= 1,
                  "...but the moving object IS re-voxelised while it moves, not left stale");
        }
        const unsigned long long armDrag = scene->published().gi.rebuilds;
        scene->refreshGlobalIllumination();              // the settle, as the mirror fires it
        measure("a box released", armDrag, 12, total, worst);
        // The RELEASE costs almost nothing, and that is the point rather than a
        // let-off: the gesture above already brought every cascade the box
        // reaches up to date, one per frame, so by the time the host settles
        // there is nothing geometric left to answer for. What the settle still
        // does is re-inject at the full bounce count and stale the probes.
        CHECK(total <= (unsigned long long)nCascades,
              "...and the release adds at most the cascades that still owed one");

        // ---- (b) a LIGHT edit: a re-injection, never a re-voxelisation ------
        const NodeId lamp = enginetest::addDirectionalLight(scene, Vec3(0.2f, -1.0f, -0.4f), 2.0f);
        render(e, 12);
        {
            LightDesc d;
            d.type = LightType::Directional;
            d.colour = Colour(1.0f, 0.4f, 0.2f);
            d.intensity = 3.0f;
            const unsigned long long arm0 = scene->published().gi.rebuilds;
            scene->setLight(lamp, d);
            scene->refreshGlobalIllumination();
            measure("a light edit", arm0, 12, total, worst);
            CHECK(total == 0,
                  "...and NOT ONE VOXEL was re-written: a light is a re-injection");
        }

        // ---- (b2) a LIGHT REMOVED: still a re-injection --------------------
        // Not one voxel's albedo changed when a lamp left the scene, and the
        // chain must say so. It reaches the engine through `invalidateGiCaches`
        // like every structural edit, so before round-2 F2 it marked the whole
        // chain through `itemsStale` and the removed light's bounce vanished
        // cascade by cascade over N frames.
        {
            const NodeId doomed =
                enginetest::addDirectionalLight(scene, Vec3(-0.3f, -1.0f, 0.2f), 1.0f);
            render(e, 16);
            const unsigned long long arm0 = scene->published().gi.rebuilds;
            CHECK(scene->removeLight(doomed), "a light is removed");
            scene->refreshGlobalIllumination();
            measure("a light removal", arm0, 16, total, worst);
            CHECK(total == 0,
                  "...and it costs ZERO cascade rebuilds: a lamp leaving is a re-injection");
        }

        // ---- (c) a MATERIAL PARAMETER edit ---------------------------------
        // The voxeliser's VctMaterial cached that datablock's colour by pointer,
        // so this one really does need fresh voxels — but a REPLACEMENT
        // voxeliser per cascade, one per frame, with the chain's lighting
        // objects (and their raw `mExtraCascades` pointers) untouched.
        {
            const MeshId m = scene->createMesh(enginetest::unitCubeMesh());
            PbrParams pp; pp.albedo = Colour(0.1f, 0.1f, 0.9f); pp.roughness = 0.7f;
            const MaterialId mat = scene->createPbrMaterial(pp);
            const NodeId painted = scene->createNode();
            scene->attachMesh(painted, m, mat);
            const Vec3 paintedAt(-1.5f, 0.5f, 1.0f);
            enginetest::setNodePosition(scene, painted, paintedAt);
            render(e, 16);
            pp.albedo = Colour(0.9f, 0.9f, 0.1f);
            const unsigned long long armMat = scene->published().gi.rebuilds;
            // THE CASCADES THAT HOLD THE WEARER (PHOTON-7 stage 3, AB A19 deleted): a voxel input of a material changes the
            // voxels of the items that wear it and nothing else, so exactly the cascades whose box meets the painted cube's
            // re-voxelise (the whole-chain dirty this used to assert re-voxelised the cascades that do not hold it too).
            unsigned long long holding = 0;
            for (const GiStatus::CascadeStatus &cs : scene->published().gi.cascades) {
                bool meets = true;
                const float c[3] = { cs.centre.x, cs.centre.y, cs.centre.z }, p[3] = { paintedAt.x, paintedAt.y, paintedAt.z };
                for (int a = 0; a < 3; ++a) meets = meets && std::fabs(p[a] - c[a]) <= cs.halfSize + 0.5f;
                holding += meets ? 1u : 0u;
            }
            CHECK(scene->setPbrMaterial(mat, pp), "a material parameter is edited");
            scene->refreshGlobalIllumination();
            measure("a material edit", armMat, 16, total, worst);
            std::printf("   the cascades holding the painted cube: %llu of %zu\n", holding, nCascades);
            CHECK(holding >= 1u && total == holding,
                  "...and EXACTLY THE CASCADES THAT HOLD THE WEARER got fresh voxels - one cascade per frame");
            render(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
            CHECK(scene->published().gi.vctBound,
                  "...with the chain still bound after every voxeliser was replaced");

            // ---- (d) a PRESET APPLY: the datablock itself is destroyed ------
            // The churn rig's case, and the one the reuse arm could never take:
            // a shading-model change destroys the datablock and builds another,
            // so a recycled address would make every voxeliser's VctMaterial
            // cache paint the new material with the old one's colour. Answered
            // the same way a parameter edit is — a replacement voxeliser per
            // cascade, one cascade per frame.
            render(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
            const unsigned long long armPre = scene->published().gi.rebuilds;
            CHECK(scene->setShadingModel(mat, ShadingModel::Unlit),
                  "a preset apply destroys the datablock (the item leaves GI)");
            measure("a preset apply", armPre, 16, total, worst);
            render(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
            const unsigned long long armPre2 = scene->published().gi.rebuilds;
            CHECK(scene->setShadingModel(mat, ShadingModel::Lit),
                  "...and another preset puts it back");
            measure("a preset undone", armPre2, 16, total, worst);
            CHECK(total >= 1 && total <= (unsigned long long)nCascades,
                  "...with fresh voxels spread one cascade per frame");
            render(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
            CHECK(scene->published().gi.vctBound, "...and the chain still bound");
        }

        // ---- (e) a SPAWN ---------------------------------------------------
        {
            render(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
            unsigned long long arm0 = scene->published().gi.rebuilds;
            const NodeId spawned =
                enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.2f), 0.0f, 0.6f);
            enginetest::setNodePosition(scene, spawned, Vec3(0.0f, 0.5f, 2.0f));
            measure("a spawn", arm0, 16, total, worst);

            // ---- (f) a HIDE ------------------------------------------------
            render(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
            arm0 = scene->published().gi.rebuilds;
            scene->setNodeVisible(spawned, false);
            measure("a hide", arm0, 16, total, worst);
            scene->setNodeVisible(spawned, true);
            render(e, 16);

            // ---- (g) an AUTHORING MOBILITY FLIP ----------------------------
            // Its own cube, so the DELETE below still deletes GI geometry: a
            // mover carries no kGiGeometryBit, and deleting one correctly costs
            // the voxels nothing at all.
            const NodeId promoted =
                enginetest::addTestCube(scene, Colour(0.3f, 0.3f, 0.9f), 0.0f, 0.6f);
            enginetest::setNodePosition(scene, promoted, Vec3(2.0f, 0.5f, 2.0f));
            render(e, 16);
            arm0 = scene->published().gi.rebuilds;
            scene->setNodeMovable(promoted, true);
            measure("an authoring flip", arm0, 16, total, worst);

            // ---- (h) a DELETE of LIT geometry -------------------------------
            render(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
            arm0 = scene->published().gi.rebuilds;
            scene->removeNode(spawned);
            measure("a delete", arm0, 16, total, worst);
            CHECK(total >= 1, "...and the cascades that held it DID re-voxelise");

            // ---- (h2) a DELETE 40 m AWAY: the outer cascade only -------------
            // The same structural edit, out of the inner cascades' reach. Before
            // round-2 F2 `invalidateGiCaches` flagged every cascade's item set
            // stale and the hit test read that as "this cascade is dirty", so
            // the box test was inert for exactly the edits it was written for.
            const NodeId distant =
                enginetest::addTestCube(scene, Colour(0.4f, 0.7f, 0.4f), 0.0f, 0.6f);
            const Vec3 far1 = farPlace();
            enginetest::setNodePosition(scene, distant, far1);
            render(e, 16);
            std::vector<unsigned long long> delBefore;
            for (const auto &c : scene->published().gi.cascades) delBefore.push_back(c.rebuilds);
            scene->removeNode(distant);
            render(e, 16);
            unsigned long long delInner = 0, delOuter = 0;
            for (size_t i = 0; i < delBefore.size(); ++i) {
                const unsigned long long d = scene->published().gi.cascades[i].rebuilds - delBefore[i];
                std::printf("   a delete at %.1f m out: c%zu +%llu (half %.1f m)\n",
                            far1.x - scene->published().gi.cascades.back().centre.x, i, d,
                            scene->published().gi.cascades[i].halfSize);
                if (i + 1u < scene->published().gi.cascades.size()) delInner += d; else delOuter += d;
            }
            CHECK(delInner == 0,
                  "a DELETE beyond the inner cascades costs them nothing");
            CHECK(delOuter >= 1, "...and the cascade that held it re-voxelises");
        }

        // ---- (j) THE BOX TEST DISCRIMINATES --------------------------------
        // The whole point of recording WHERE an edit happened: a change 40 m
        // away is outside the inner cascades' boxes, and they must not spend a
        // frame on it. (The tier table is 5 / 10 / 15 / 60 m half-sizes around
        // the camera, which stands at the origin throughout this case.)
        {
            render(e, 16);
            const NodeId faraway =
                enginetest::addTestCube(scene, Colour(0.9f, 0.3f, 0.3f), 0.0f, 0.6f);
            const Vec3 far2 = farPlace();
            enginetest::setNodePosition(scene, faraway, far2);
            render(e, 16);                                   // its arrival drains
            std::vector<unsigned long long> before;
            for (const auto &c : scene->published().gi.cascades) before.push_back(c.rebuilds);
            enginetest::setNodePosition(scene, faraway, Vec3(far2.x + 1.0f, far2.y, far2.z));
            render(e, 2);
            scene->refreshGlobalIllumination();
            render(e, 12);
            std::vector<unsigned long long> after;
            for (const auto &c : scene->published().gi.cascades) after.push_back(c.rebuilds);
            unsigned long long inner = 0, outer = 0;
            for (size_t i = 0; i < after.size(); ++i) {
                std::printf("   c%zu rebuilds +%llu (half %.1f m)\n", i, after[i] - before[i],
                            scene->published().gi.cascades[i].halfSize);
                if (i + 1u < after.size()) inner += after[i] - before[i];
                else outer += after[i] - before[i];
            }
            CHECK(inner == 0,
                  "an edit beyond the inner cascades costs the ones that cannot see it NOTHING");
            CHECK(outer >= 1, "...and the one that can see it re-voxelises");
            scene->removeNode(faraway);
            render(e, 12);
        }

        // ---- (i) the picture still works -----------------------------------
        // Every one of the edits above kept objects, textures and buffers that a
        // from-scratch chain would have thrown away; the assertion that they are
        // still the RIGHT ones is a rendered frame that is not black.
        render(e, 4);
        Image shot;
        CHECK(view->readPixels(shot), "the chain renders after every edit");
        CHECK(groundLum(shot) > 0.01f, "...and the ground is still lit");

        GiParams down; down.mode = GiMode::Off;
        CHECK(scene->setGlobalIllumination(down), "the edited chain comes down");
        render(e, 2);
    }
    }   // (edits: case 7 (edits))

    if (doMover || doMoverRoutes) {   // (mover / mover_routes: case 12)
    stageEditedScene();
    // =====================================================================
    // CASE 12 — A STILL OBJECT THAT IS MOVING IS LIT AT THE POSE IT IS IN
    // =====================================================================
    // The smoke rig's finding (ledger §320), pinned as a gate rather than as a
    // picture in a spike folder. On the shipped MONOLITHIC arm a STILL-classified
    // object dragged in the editor keeps being lit against a voxel copy of
    // ITSELF at its OLD pose: a geometry move restarts both of the mirror's
    // stability gates on every frame of the gesture, so the full re-solve never
    // fires during it, the cheap re-injection lights the stale voxels, and the
    // VCT cone's self-occlusion start bias paints stripes on the lit face. The
    // rig measured that on the shipped arm at 960x540 as **max 152/255 with
    // 86,653 pixels at or above 10** — 16.7 % of the frame.
    //
    // Under the chain the mover's box marks the cascades it reaches and the
    // scheduler spends them. Two measurements: the last frame of the motion
    // against the SAME POSE at rest, and the rest against the oracle below.
    //
    // (The monolithic arm is deliberately NOT asserted here: what to do about it
    // is the owner's decision, not this suite's.)
    //
    // THE ORACLE (PHOTON-7 stage 10, 2026-10-25): the rest picture is compared with a FRESH BUILD of the chain at the
    // final pose (GI off, then on) - "after the motion stops, the picture equals a fresh rest of the final scene". The
    // motion-against-rest diff alone was a FALSE GREEN: the walk's moved test read the world box only (giAabbMoved, a
    // size/64 quantum), a yaw moves a cube's box as cos t + sin t - flat at 45 degrees - so the marks thinned out and
    // stopped at 35 degrees, and the voxels held the cube at 35 degrees while it stood at 59, at rest too, for ever
    // (rest against the fresh build 209 / 226 / 185 codes at the three poses below, 2,000-4,700 px; motion against rest
    // 6-16 codes because both held the same stale copy). The walk now sees ANY affine change - the item's local-box
    // corners carried by the recorded and the current world transform (giPoseMoved: 82-86 codes left, the last
    // sub-quantum pose) - and REST IS EXACT: an item off its record by less than the quantum is read again by the next
    // walk and marked once when it has stopped (walkItems' giMove: 1 code, 0 px).
    // THREE POSES of cascade 0's box against the turner: a CORNER (two of its faces cut the turner's footprint), ONE
    // face, NO face - the precondition is checked, so a placement change that moves a pose fails loudly here.
    // THE MOTION LAW (motion against rest within 16 codes, none off by 10), PHOTON-7 stage 8A's A2 - A MOVER's regions in
    // step: the turner a DECLARED MOVER (its slot listed every frame it moves), judged with the RAYS OFF - the voxel truth,
    // the store's bounce against the rest's. Measured 2 / 2 / 2 codes, 0 px (irisgl 0d6fae97 on). Two causes made it red: the cascades' order (stage 10:
    // cascade 0's reserved slot rebuilt the turner, the one-writer rule held 1..3) - gone with A1's region rebuild in every
    // cascade every frame it moves (measured: +29 region rebuilds a cascade over the 60 frames, every second frame: the
    // walk's move quantum for a STILL) yet still 224 / 145 / 137 codes; and THE BOUNCE'S CONVERGENCE - a re-voxelised
    // region's bounce restarted from its direct term and the feedback converged it a pass a frame (the error fell to 0
    // over 8-32 frames after the motion stopped). A2: the region's bricks take k passes IN THE FRAME they are rebuilt (k
    // from the region's largest albedo: PhotonComponent::regionBounce). The RAYS-ON numbers are PRINTED with the frames
    // the picture takes to reach the bar and a code after the motion. WHICH ROUTE THEY MEASURE (PHOTON-7's leftover, 8B's
    // B5, 2026-10-29 §3574): this view is a Live offscreen view that never gathers (probeGather false), so the rays-on
    // residue is the NON-GATHERING ROUTE's - the screen integrate's (the irradiance field's temporal or its probe budget),
    // unmeasured; the gather's history off reads the identical numbers. It is not the gather's temporal hold. Its numbers
    // of record: 56/255, 500 px >= 10 on the base f9544f67f (the bar at 31 frames, a code at 37); 81/255, 341 px on 8B's
    // tip (27 / 33). Its row (row mover_routes) asserts only the rest law on that route.
    std::printf("\n== case 12: a moving still object is lit at its own pose ==\n");
    {
        struct Pose { const char *name; Vec3 cam; int faces; };
        const Pose poses[3] = { { "corner",   Vec3(4.0f, 3.0f, 6.0f), 2 },
                                { "one face", Vec3(2.5f, 3.0f, 6.0f), 1 },
                                { "no face",  Vec3(2.5f, 3.0f, 2.5f), 0 } };
        const auto diffOf = [&](const Image &a, const Image &b, int &worst, unsigned &over10) {
            worst = 0; over10 = 0;
            for (unsigned y = 0; y < kSize; ++y)
                for (unsigned x = 0; x < kSize; ++x) {
                    const Colour p = a.at(x, y), q = b.at(x, y);
                    const int d = int(std::max(std::max(std::fabs(p.r - q.r), std::fabs(p.g - q.g)),
                                               std::fabs(p.b - q.b)) * 255.0f + 0.5f);
                    worst = std::max(worst, d);
                    if (d >= 10) ++over10;
                }
        };
        // The turner's footprint over the whole motion: a 2 m cube about the vertical, half-diagonal sqrt 2.
        const float reach = 1.41422f;
        // THE RUNS: the MOVER turning at the three poses with the rays off (A2's law, asserted); the corner with the rays on
        // (printed: the non-gathering route's residue, the frames to the bar and to a code); the corner with a STILL turned by script
        // (printed: the document-to-engine mirror's move quantum - the walk marks a 1-degree-a-frame turn every second
        // frame, so the store holds the still up to a quantum behind: an editor-side agreement, not the renderer's law).
        struct Run { const Pose *pose; int raysOn; bool still; };
        const Run runs[5] = { { &poses[0], 0, false }, { &poses[1], 0, false }, { &poses[2], 0, false },
                              { &poses[0], 1, false }, { &poses[0], 0, true } };
        int raysWere = -1;
        for (int ri = 0; ri < 5; ++ri) {
        // (the part: `mover` the three poses with the rays off - A2's law; `mover_routes` the rays-on corner and the still)
        if (ri < 3 ? !doMover : !doMoverRoutes) continue;
        const Run &run = runs[ri];
        const Pose &pose = *run.pose;
        const int raysOn = run.raysOn;
        if (raysOn != raysWere) {
            e->setRayTracing(raysOn != 0);
            render(e, 2);
            raysWere = raysOn;
        }
        std::printf("   -- %s, the rays %s --\n", run.still ? "a STILL turned by script (printed: the move quantum)"
                                                          : "a MOVER turning",
                    raysOn ? "ON (printed: the non-gathering route's residue)" : "OFF (A2's law)");
        {
            view->setCamera(enginetest::testCameraDescLookAt(pose.cam, Vec3(0.0f, 1.0f, 0.0f)));
            const NodeId turner = enginetest::addTestCube(scene, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.5f);
            if (!run.still) scene->setNodeMovable(turner, true);   // (a declared mover: A2's subject)
            enginetest::setNodePosition(scene, turner, Vec3(0.0f, 1.0f, 0.0f));
            enginetest::setNodeScale(scene, turner, Vec3(2.0f, 2.0f, 2.0f));
            CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up for the motion");
            render(e, 60);
            int faces = 0;
            {
                const GiStatus g = scene->published().gi;
                if (!g.cascades.empty()) {
                    const auto &c0 = g.cascades[0];
                    const float lo[2] = { c0.centre.x - c0.halfSize, c0.centre.z - c0.halfSize };
                    const float hi[2] = { c0.centre.x + c0.halfSize, c0.centre.z + c0.halfSize };
                    for (int k = 0; k < 2; ++k) {
                        if (lo[k] > -reach && lo[k] < reach) ++faces;
                        if (hi[k] > -reach && hi[k] < reach) ++faces;
                    }
                    std::printf("   [%s] camera %.1f %.1f %.1f: cascade 0 at %.2f %.2f %.2f, half %.2f m - %d of its faces "
                                "cut the turner\n", pose.name, pose.cam.x, pose.cam.y, pose.cam.z,
                                c0.centre.x, c0.centre.y, c0.centre.z, c0.halfSize, faces);
                }
            }
            CHECK(faces == pose.faces, "the pose is the one named (cascade 0's faces across the turner)");
            // 60 frames of rotation, one degree a frame - a scripted mover (no host gesture: an edit every frame).
            const auto yaw = [&](float deg) {
                const float r = deg * 3.14159265358979323846f / 180.0f * 0.5f;
                const Quat q{ 0.0f, std::sin(r), 0.0f, std::cos(r) };
                scene->setNodeTransform(turner, Vec3(0.0f, 1.0f, 0.0f), q, Vec3(2.0f, 2.0f, 2.0f));
            };
            const GiStatus g0 = scene->published().gi;
            // (the region's own bounce PRICED at the turner: its monitor rows over the motion, the first pose with the rays
            // off - each rebuild's k passes' GPU ms)
            const bool price = !raysOn && !run.still && &pose == &poses[0];
            if (price) e->setFrameMonitor(MonitorLevel::Review);
            for (int f = 0; f < 60; ++f) { yaw(float(f)); render(e, 1); }
            Image motion; CHECK(view->readPixels(motion), "the last motion frame reads back");
            if (price) {
                render(e, 6);   // (the GPU samples land a few frames late; the rest frame below is the same pose)
                e->setFrameMonitor(MonitorLevel::Off);
                std::vector<MonitorRecord> records;
                while (e->popMonitorRecords(records) > 0) {}
                double ms[8] = {}, passes[8] = {};
                unsigned count[8] = {};
                for (const MonitorRecord &r : records)
                    for (const CacheWork &w : r.cacheWork)
                        if (w.detail.rfind("vct.regionBounce", 0) == 0 && w.gpuMs >= 0.0f) {
                            const int k = std::atoi(w.detail.c_str() + 16);
                            if (k < 0 || k > 7) continue;
                            ms[k] += double(w.gpuMs);
                            passes[k] += double(w.units);
                            ++count[k];
                        }
                std::printf("   [%s] THE REGION'S OWN BOUNCE PRICED at the turner (rays off):", pose.name);
                for (int k = 0; k < 4; ++k)
                    std::printf("  c%d %u rebuilds, %.1f passes, %.3f ms each", k, count[k], count[k] ? passes[k] / count[k] : 0.0,
                                count[k] ? ms[k] / count[k] : 0.0);
                std::printf("\n");
            }
            // (the rays on: the frames after the motion, to find when the picture reaches the bar and a code)
            std::vector<Image> after;
            if (raysOn)
                for (int q = 0; q < 64; ++q) { render(e, 1); Image im; view->readPixels(im); after.push_back(im); }
            {
                // (printed: each cascade's rebuilds over the motion - whole, and by region (stage 8A))
                const GiStatus g1 = scene->published().gi;
                std::printf("   [%s] over the 60 motion frames:", pose.name);
                for (size_t k = 0; k < g1.cascades.size() && k < g0.cascades.size(); ++k)
                    std::printf("  c%zu rebuilds +%llu (region +%llu, its own bounce passes to convergence +%llu, the last frame's %u)", k,
                                g1.cascades[k].rebuilds - g0.cascades[k].rebuilds,
                                g1.cascades[k].regionRebuilds - g0.cascades[k].regionRebuilds,
                                g1.cascades[k].regionBouncePasses - g0.cascades[k].regionBouncePasses,
                                g1.cascades[k].regionBounceLast);
                std::printf("\n");
            }
            // ...the SAME POSE at rest, after everything has caught up...
            render(e, raysOn ? 56 : 120);
            Image rest; CHECK(view->readPixels(rest), "the rest frame reads back");
            // ...and THE ORACLE: the chain built fresh at that pose.
            GiParams off; off.mode = GiMode::Off;
            CHECK(scene->setGlobalIllumination(off), "the chain comes down for the fresh build");
            render(e, 2);
            CHECK(scene->setGlobalIllumination(cascadeGi()), "...and is built fresh at the final pose");
            render(e, 120);
            Image truth; CHECK(view->readPixels(truth), "the fresh build reads back");
            int restMax = 0, motionMax = 0, motionTruthMax = 0;
            unsigned restOver = 0, motionOver = 0, motionTruthOver = 0;
            diffOf(rest, truth, restMax, restOver);
            diffOf(motion, rest, motionMax, motionOver);
            diffOf(motion, truth, motionTruthMax, motionTruthOver);
            std::printf("   [%s] rest vs the fresh build: max %d/255, %u px >= 10; motion vs rest: max %d/255, %u px >= 10; "
                        "motion vs the fresh build: max %d/255, %u px >= 10 (of %u; the monolithic arm measured 152 and "
                        "86,653 at 960x540)\n", pose.name, restMax, restOver, motionMax, motionOver, motionTruthMax,
                        motionTruthOver, kSize * kSize);
            CHECK(restMax <= 1 && restOver == 0,
                  "AT REST THE OBJECT IS LIT AT THE POSE IT IS IN: the picture equals a fresh build there (within a code)");
            if (run.still) {
                std::printf("   [%s] A STILL (printed): motion vs rest %d/255, %u px >= 10 - the move quantum's lag\n", pose.name,
                            motionMax, motionOver);
            } else if (!raysOn) {
                // A2's (the head of the case): the store's bounce in the frame of its geometry
                CHECK(motionMax <= 16, "the moving object is lit at the pose it is IN, not the one it left (rays off)");
                CHECK(motionOver == 0, "...with not one pixel off by 10 (rays off)");
            } else {
                int toBar = -1, toCode = -1;
                for (size_t q = 0; q < after.size(); ++q) {
                    int w = 0; unsigned o = 0;
                    diffOf(after[q], rest, w, o);
                    if (toBar < 0 && w <= 16 && o == 0u) toBar = int(q) + 1;
                    if (toCode < 0 && w <= 1) toCode = int(q) + 1;
                }
                std::printf("   [%s] RAYS ON (the non-gathering route, printed): motion vs rest %d/255, %u px >= 10; the bar reached %d frames "
                            "after the motion, a code %d frames after (-1: not within %zu)\n", pose.name, motionMax,
                            motionOver, toBar, toCode, after.size());
            }
            CHECK(scene->setGlobalIllumination(off), "the motion's chain comes down");
            render(e, 2);
            scene->removeNode(turner);
        }
        }
        e->setRayTracing(true);
        render(e, 2);
    }
    }   // (mover / mover_routes: case 12)

    if (doFaults) {   // (faults: case 13)
    // STAGED: case 13 ran after case 12 - the chain down, in case 7's scene.
    {
        GiParams down; down.mode = GiMode::Off;
        CHECK(scene->setGlobalIllumination(down), "staged: the chain down before the post-build fault");
        render(e, 2);
        stageEditedScene();
    }
    // =====================================================================
    // CASE 13 — A FAILURE AFTER THE BUILD
    // =====================================================================
    // Between a cascade's successful `build()` and the end of its rebuild there
    // are two more things that can throw — the ambient push and
    // `VctLighting::update`'s own dispatch (the VK_ERROR_OUT_OF_DEVICE_MEMORY class
    // is real on this box). By then the volumes on the GPU are correct and current
    // for the NEW placement and only the light injection is missing, so the cascade
    // keeps the new placement and the next frame re-runs the rebuild. (This case
    // used to guard a REPLACEMENT voxeliser swapped in for a material edit; that
    // path is deleted since the A5b fix round - a material edit is now a dirty
    // rebuild of the same voxeliser - and the case keeps guarding the post-build
    // failure, driven by a material edit exactly as before.)
    std::printf("\n== case 13: a failure AFTER the build ==\n");
    {
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up for the fault");
        render(e, 16);
        const MeshId fm = scene->createMesh(enginetest::unitCubeMesh());
        PbrParams fp; fp.albedo = Colour(0.2f, 0.6f, 0.2f); fp.roughness = 0.6f;
        const MaterialId fmat = scene->createPbrMaterial(fp);
        const NodeId fnode = scene->createNode();
        scene->attachMesh(fnode, fm, fmat);
        enginetest::setNodePosition(scene, fnode, Vec3(0.0f, 0.5f, 1.0f));
        render(e, 24);
        const GiStatus pre = scene->published().gi;
        // A WHOLE REBUILD of cascade 0 at its placement (the measuring door gi.rebuildCascade: a change with no box - a
        // boxed edit, the material edit this case used to drive, is a REGION rebuild since PHOTON-7 stage 8A and never
        // reaches the whole rebuild's post-build fault), its ONE attempt under the fault.
        e->setArm("gi.cascadeFaultPost", 0.0);
        e->setArm("gi.rebuildCascade", 0.0);
        render(e, 1);
        e->setArm("gi.rebuildCascade", -1.0);
        const GiStatus bad = scene->published().gi;
        std::printf("   post-build fault: c0 rebuilds %llu -> %llu, pending %d, vctBound %d\n",
                    pre.cascades[0].rebuilds, bad.cascades[0].rebuilds, bad.cascades[0].pending,
                    int(bad.vctBound));
        CHECK(bad.cascades[0].rebuilds == pre.cascades[0].rebuilds,
              "a rebuild that threw after the build is NOT counted as one");
        CHECK(bad.vctBound && bad.cascades.size() == pre.cascades.size(),
              "...and the chain is still up and still bound");
        Image live;
        CHECK(view->readPixels(live), "...and the scene still renders (no dangling voxeliser)");
        e->setArm("gi.cascadeFaultPost", -1.0);
        render(e, 24);
        const GiStatus healed = scene->published().gi;
        unsigned long long after13 = 0, before13 = 0;
        for (size_t i = 0; i < healed.cascades.size(); ++i) {
            after13 += healed.cascades[i].rebuilds;
            before13 += pre.cascades[i].rebuilds;
        }
        std::printf("   after the fault is cleared: %llu cascade rebuilds, pending left %d\n",
                    after13 - before13, healed.cascades[0].pending);
        CHECK(after13 > before13, "the next frames retry and the chain catches up");
        CHECK(healed.rebuilds == pre.rebuilds,
              "...without one whole-chain build anywhere in it");
        scene->removeNode(fnode);
        GiParams down; down.mode = GiMode::Off;
        CHECK(scene->setGlobalIllumination(down), "the faulted chain comes down cleanly");
        render(e, 2);
    }
    }   // (faults: case 13)

    if (doEdits) {   // (edits: cases 14, 15, 15b)
    // =====================================================================
    // CASE 14 — THE PER-CASCADE INSTANCE BUDGET (PHOTON_SPEC §7 E2 (1))
    // =====================================================================
    // The raster voxeliser's price is the geometry INSIDE the region and
    // nothing else, so a dense world's outer cascade is the most expensive
    // thing the chain does — measured on the 8,026-instance lattice at 88.9 ms
    // of GPU for ONE rebuild (spikes/photon-e2/BASELINE.md). The budget is the
    // patch-free lever: each cascade voxelises at most `cascadeInstanceCap`
    // objects, ranked by how much of ITS OWN voxel each one fills.
    //
    // THREE CAPS, and what each must hold:
    //   * the attach set never exceeds the cap (`cascades[].attached`);
    //   * what a rebuild voxelises never exceeds it either (`[].items`);
    //   * 0 is NO budget and must be the shipped arm exactly — same attach set,
    //     same enclosed count;
    //   * the CPU cost per cascade is PRINTED at every cap, never asserted: it
    //     is a property of the box, and the lane's own four-cap measurement on
    //     the real lattice is in the report.
    //
    // The dense scene here is 6x6x6 = 216 unit cubes rather than the lane's
    // 8,404-node lattice: this is a CONTRACT test that has to run inside a
    // gate's budget, and the contract ("never more than the cap, and 0 changes
    // nothing") does not need the big scene to be true. The big scene is what
    // the NUMBERS come from.
    std::printf("\n== case 14: the per-cascade instance budget ==\n");
    {
        std::vector<NodeId> dense;
        for (int x = 0; x < 6; ++x)
            for (int y = 0; y < 6; ++y)
                for (int z = 0; z < 6; ++z) {
                    const NodeId n = enginetest::addTestCube(scene, Colour(0.7f, 0.7f, 0.7f),
                                                             0.0f, 0.8f);
                    enginetest::setNodePosition(scene, n, Vec3(float(x) - 2.5f, float(y) + 0.5f,
                                                               float(z) - 2.5f));
                    dense.push_back(n);
                }
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 3.0f, 12.0f),
                                                         Vec3(0.0f, 2.0f, 0.0f)));
        render(e, 2);

        const int caps[3] = { 0, 64, 16 };
        int attachedAtCap[3] = { 0, 0, 0 };
        int itemsAtCap[3] = { 0, 0, 0 };
        long long dispatchesAtCap[3] = { 0, 0, 0 };
        for (int ci = 0; ci < 3; ++ci) {
            GiParams gi = cascadeGi();
            gi.cascadeInstanceCap = caps[ci];
            CHECK(scene->setGlobalIllumination(gi),
                  caps[ci] ? "the chain builds under an instance budget"
                           : "the chain builds with no budget (the shipped arm)");
            render(e, 6);
            CHECK(settleReadings(e, scene) >= 0,   // the voxel read-back lands frames late
                  "the voxel read-back landed: every cascade's reading is its newest gather's");
            const GiStatus st = scene->published().gi;
            if (st.cascades.empty()) { CHECK(false, "the chain is up at this cap"); continue; }
            std::printf("   cap %-4d :", caps[ci]);
            bool capHeld = true;
            for (size_t i = 0; i < st.cascades.size(); ++i) {
                const GiStatus::CascadeStatus &c = st.cascades[i];
                std::printf("  c%zu attached %4d items %4d disp %4lld %6.2f ms CPU |", i,
                            c.attached, c.items, c.voxelDispatches, c.lastCpuMs);
                if (caps[ci] > 0 && (c.attached > caps[ci] || c.items > caps[ci]))
                    capHeld = false;
            }
            std::printf("\n");
            attachedAtCap[ci] = st.cascades.back().attached;
            itemsAtCap[ci] = st.cascades.back().items;
            dispatchesAtCap[ci] = st.cascades.back().voxelDispatches;
            if (caps[ci] > 0)
                CHECK(capHeld, "no cascade attaches or voxelises more than the budget");
        }
        // 0 IS THE SHIPPED ARM, and the outermost cascade is where that shows:
        // it encloses the whole dense block, so an uncapped run must attach and
        // voxelise more of it than either budget allowed.
        CHECK(attachedAtCap[0] > attachedAtCap[1] && attachedAtCap[1] > attachedAtCap[2],
              "a smaller budget attaches strictly fewer objects (0 = no budget = the most)");
        CHECK(itemsAtCap[0] > itemsAtCap[2],
              "...and voxelises strictly fewer");
        // THE DISPATCH COUNT IS NOT THE OBJECT COUNT (fork ad452604a+155a56bf8+0338ca7f2+c4c80b5f7 (was 0065)), and this
        // is the regression guard for the 3.9x fork ad452604a+155a56bf8 (was 0062) cost on dense content.
        // A voxelisation dispatch is sized by the WHOLE VOLUME however few
        // instances it holds, and these 220 objects each own a material — so a
        // bucket key that names the material SLOT gives 220 whole-volume
        // dispatches per cascade, while one that names the material POOL (1024
        // materials per pool) gives a handful. `voxelDispatches` is the reading
        // that tells them apart, and it must stay in the handful.
        std::printf("   uncapped: the outermost cascade attached %d objects in %lld dispatches\n",
                    attachedAtCap[0], dispatchesAtCap[0]);
        CHECK(dispatchesAtCap[0] > 0 && dispatchesAtCap[0] <= 8,
              "A CASCADE VOXELISES A HUNDRED-MATERIAL SCENE IN A HANDFUL OF DISPATCHES, "
              "NOT ONE PER MATERIAL");
        CHECK(dispatchesAtCap[0] < attachedAtCap[0],
              "...and strictly fewer dispatches than it has objects");
        for (NodeId n : dense) scene->removeNode(n);
        render(e, 2);
        GiParams back = cascadeGi();
        CHECK(scene->setGlobalIllumination(back), "the unbudgeted arm comes back");
        render(e, 4);
    }


    // =====================================================================
    // CASE 15 — TWENTY MOVERS IN ONE PLACE ARE STILL ONE PLACE
    // (PHOTON audit 2026-09-17, PHOTON.md F14; lane ENGINE-SMALL-A)
    // =====================================================================
    // THE DEFECT. The dirty-box list is capped at `kGiCascadeDirtyBoxCap` (16),
    // and past the cap it used to collapse into "the scene changed EVERYWHERE" —
    // on the reasoning that a scene changing in sixteen places has to be
    // answered whole. But the commonest way to exceed the cap is not a
    // scene-wide edit: it is SEVENTEEN OBJECTS MOVING IN ONE CORNER (a physics
    // pile settling, an animated set, a crowd), which produce seventeen small
    // boxes a metre apart. Answering "everywhere" marked every cascade out to
    // the horizon, so the chain rebuilt one cascade per frame for as long as
    // they kept moving — the exact cost the per-cascade path exists to avoid,
    // paid for having TOO MUCH information rather than too little.
    //
    // WHAT THIS ASSERTS. Twenty cubes moving every frame, all of them in a place
    // only the OUTERMOST cascade reaches (derived from the chain as it stands,
    // like case 7's farPlace):
    //   1. the INNER cascades are never re-voxelised — they cannot see the
    //      movers, and with the merge in place the description still says so.
    //      FAILS BEFORE: the cap fires on frame one, every cascade is marked,
    //      and the scheduler spends one rebuild per frame, innermost first.
    //   2. the outermost cascade IS re-voxelised — the movers are real and the
    //      cascade that sees them must follow (the counterpart assertion: "no
    //      rebuilds" would be a chain that had stopped listening).
    //   3. AT REST IT FALLS TO ZERO: the frames after the movement stops
    //      re-voxelise nothing at all.
    std::printf("\n== case 15: twenty movers in one place (F14) ==\n");
    {
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up for the mover case");
        render(e, 8);
        const GiStatus base = scene->published().gi;
        if (base.cascades.size() < 2) {
            CHECK(false, "case 15 needs a chain of at least two cascades");
        } else {
            const size_t n = base.cascades.size();
            // A PLACE ONLY THE OUTERMOST CASCADE REACHES: half way between the
            // second-outermost cascade's edge and the outermost one's.
            const float inner = base.cascades[n - 2u].halfSize;
            const float outer = base.cascades[n - 1u].halfSize;
            const Vec3 centre = base.cascades[n - 1u].centre;
            const float x0 = centre.x + (inner + outer) * 0.5f;
            const float z0 = centre.z;

            // TWENTY of them — four more than the cap, which is the whole point.
            std::vector<NodeId> movers;
            for (int i = 0; i < 20; ++i) {
                const NodeId cube = enginetest::addTestCube(scene, Colour(0.6f, 0.6f, 0.9f),
                                                            0.0f, 0.8f);
                enginetest::setNodePosition(scene, cube,
                                            Vec3(x0 + float(i % 5) * 1.5f, 0.5f,
                                                 z0 + float(i / 5) * 1.5f));
                movers.push_back(cube);
            }
            render(e, 12);          // the spawn's own work drains

            const auto perCascade = [&]() {
                std::vector<unsigned long long> out;
                for (const auto &c : scene->published().gi.cascades) out.push_back(c.rebuilds);
                return out;
            };
            std::vector<unsigned long long> before = perCascade();
            const unsigned long long fullBefore = scene->published().gi.cascadeFullRebuilds;
            const unsigned long long restsBefore = scene->published().gi.feedbackRests;

            // THE MOVEMENT: every mover moves every frame, by more than the
            // scan's quantisation, for 24 frames.
            for (int f = 0; f < 24; ++f) {
                for (size_t i = 0; i < movers.size(); ++i)
                    enginetest::setNodePosition(scene, movers[i],
                                                Vec3(x0 + float(i % 5) * 1.5f + 0.3f * float(f),
                                                     0.5f, z0 + float(i / 5) * 1.5f));
                render(e, 1);
            }
            std::vector<unsigned long long> during = perCascade();
            unsigned long long innerRebuilds = 0, outerRebuilds = 0;
            std::printf("   24 frames of 20 movers in the outermost cascade only:");
            for (size_t i = 0; i < during.size(); ++i) {
                const unsigned long long d = during[i] - before[i];
                std::printf("  c%zu %llu", i, d);
                if (i + 1u == during.size()) outerRebuilds += d; else innerRebuilds += d;
            }
            std::printf("  (full-chain guards %llu)\n",
                        scene->published().gi.cascadeFullRebuilds - fullBefore);
            CHECK(innerRebuilds == 0ull,
                  "twenty movers past the dirty-box cap do NOT dirty the cascades that "
                  "cannot see them (F14: the cap merges, it does not give up)");
            CHECK(outerRebuilds > 0ull,
                  "...while the cascade that CAN see them follows them");

            // ...AND THE MERGED BOXES' REBUILDS' LIGHT CONVERGES, AT REST (PHOTON-7 stage 4: the feedback; the bar DERIVED
            // FROM THE SOLVER at stage 8A - the old 34 frames was the old behaviour's number). After the last change every
            // brick it reached converges by its own passes: a brick falls under a code within the passes of its own series
            // (at most kRegionBouncePassesMax = 32, the closed box's bound at albedo 0.85), and an owed cascade's floor
            // alone passes its every active brick once in 2 x lightWorkFrames frames (GiQualityFacts' law) - so a brick's
            // rest is within 32 x 2 x lightWorkFrames frames of its last resume; a resume PROPAGATES a hop a pass (a
            // stopped neighbour resumes on the frame its source's move accumulates past its threshold), at most one hop a
            // brick across the merged boxes' extent (their union's longest side over the cascade's brick), each adding a
            // pass. THE BOUND: (32 + hops) x 2 x lightWorkFrames, plus the readings' lag (4). The rest is waited for frame
            // by frame (its frames printed) - the bound only fails a rest that never comes - and the rested light must be
            // a fresh build's (the chain built again at the same scene) within TWO codes at every lit voxel of the
            // outermost cascade: each rest lies within a code of the fixed point (the stop rule), so two lie within two.
            const unsigned long long restsAtStop = scene->published().gi.feedbackRests;
            render(e, 4);
            before = perCascade();
            const GiStatus gs = scene->published().gi;
            const auto &outerC = gs.cascades.back();
            const float brickM = 8.0f * outerC.cell;
            const float extent = 4.0f * 1.5f + 0.3f * 24.0f + 1.0f;   // (the movers' union: 5 x 4 at 1.5 m, 24 x 0.3 m driven)
            const unsigned hops = unsigned(std::ceil(extent / std::max(brickM, 1e-3f)));
            const unsigned lwf = giQualityFacts(GiQuality::High, GiViewProfile::Desktop).lightWorkFrames;
            const unsigned bound = (32u + hops) * 2u * lwf + 4u;
            unsigned restFrame = 0u;
            for (unsigned q = 1; q <= bound; ++q) {
                render(e, 1);
                const GiStatus gq = scene->published().gi;
                if (!gq.feedbackOwed && gq.feedbackRests > restsAtStop) { restFrame = q; break; }
            }
            during = perCascade();
            unsigned long long atRest = 0;
            for (size_t i = 0; i < during.size(); ++i) atRest += during[i] - before[i];
            std::printf("   after they stop: %llu cascade rebuilds; the feedback at rest %u frames after (the solver's bound %u: "
                        "(32 + %u hops) x 2 x %u + 4)\n", atRest, restFrame, bound, hops, lwf);
            CHECK(atRest == 0ull, "and when they stop, the chain re-voxelises nothing");
            CHECK(restFrame > 0u, "the light of the rebuilds the merged dirty boxes caused CONVERGES once the movement stops "
                                  "(the feedback comes to rest within the solver's own bound)");
            {
                GiVoxelVolume rested, fresh;
                const int outer = int(gs.cascades.size()) - 1;
                const bool r1 = scene->giVoxelLight(outer, rested) && rested.available;
                GiParams down;
                down.mode = GiMode::Off;
                scene->setGlobalIllumination(down);
                render(e, 2);
                scene->setGlobalIllumination(cascadeGi());
                e->waitForAsyncShaders();
                for (int q = 0; q < 1500; ++q) {
                    render(e, 1);
                    const GiStatus gq = scene->published().gi;
                    if (q > 8 && gq.giAtRest && !gq.feedbackOwed) break;
                }
                const bool r2 = scene->giVoxelLight(outer, fresh) && fresh.available;
                size_t lit = 0, over = 0;
                double worst = 0.0;
                for (size_t v = 0; r1 && r2 && v * 4u + 2u < rested.light.size() && v * 4u + 2u < fresh.light.size(); ++v) {
                    double c = 0.0;
                    bool any = false;
                    for (int ch = 0; ch < 3; ++ch) {
                        const double o = double(fresh.light[v * 4u + ch]);
                        if (!(o > 1e-5)) continue;
                        any = true;
                        c = std::max(c, std::max(0.0, std::fabs(double(rested.light[v * 4u + ch]) - o) - std::ldexp(o, -10)) /
                                            (0.01 * o));
                    }
                    if (!any) continue;
                    ++lit;
                    worst = std::max(worst, c);
                    over += c > 2.0 ? 1u : 0u;
                }
                std::printf("   the rested outermost cascade against a fresh build: %zu lit voxels, %zu over two codes, worst "
                            "%.3f codes\n", lit, over, worst);
                CHECK(r1 && r2 && lit > 100u && over == 0u,
                      "...AND THE RESTED LIGHT IS A FRESH BUILD'S: every lit voxel of the outermost cascade within two codes");
            }

            for (NodeId m : movers) scene->removeNode(m);
            render(e, 6);
        }
    }


    // =====================================================================
    // CASE 15b — AND THE MERGE SURVIVES FLAT BOXES
    // (the lead's read of F14, 2026-09-18)
    // =====================================================================
    // THE DEFECT THE FIRST VERSION OF THE MERGE HAD. "Least enlargement" was
    // measured as the growth of the box's VOLUME, which is what a plain R-tree
    // uses — and this engine's boxes go FLAT: a Plane primitive's world AABB has
    // zero height, so every coplanar box and every union of them has volume
    // ZERO, the growth comparison is 0 against 0 for every candidate, the first
    // entry wins every time, and a floor of moving planes coalesces into ONE
    // SLAB spanning everything it touched. Conservative (a merged box covers
    // what both covered, so no cascade is ever missed) but the exact opposite of
    // the point: the dirty region ends up reaching cascades that never saw a
    // mover. The metric is the R*-tree's MARGIN now — the sum of the extents,
    // which no flat box makes degenerate.
    //
    // THE GEOMETRY IS THE ASSERTION. Ten flat movers at +X and ten at -X, all in
    // the same plane, all outside every cascade but the outermost, twenty boxes
    // against a cap of sixteen. Under the volume metric the two clusters merge
    // into one slab through the MIDDLE of the chain — where the camera and
    // cascade 0 are — and the inner cascades rebuild for movers they cannot see.
    // Under the margin metric each cluster stays its own entry.
    std::printf("\n== case 15b: twenty FLAT movers, two far clusters (F14 round 2) ==\n");
    {
        CHECK(scene->setGlobalIllumination(cascadeGi()), "the chain is up for the flat-mover case");
        render(e, 8);
        const GiStatus base = scene->published().gi;
        if (base.cascades.size() < 3) {
            CHECK(false, "case 15b needs a chain of at least three cascades");
        } else {
            const size_t n = base.cascades.size();
            const float inner = base.cascades[n - 2u].halfSize;
            const float outer = base.cascades[n - 1u].halfSize;
            const Vec3 centre = base.cascades[n - 1u].centre;
            const float far = (inner + outer) * 0.5f;   // only the outermost reaches it

            const MeshId plane = scene->createMesh(flatPlaneMesh());
            PbrParams flat; flat.albedo = Colour(0.7f, 0.7f, 0.7f);
            flat.metalness = 0.0f; flat.roughness = 0.8f;
            const MaterialId flatMat = scene->createPbrMaterial(flat);
            CHECK(plane && flatMat, "a flat 1x1 quad and its material");

            std::vector<NodeId> movers;
            std::vector<float> homeX, homeZ;
            for (int side = 0; side < 2; ++side) {
                const float sx = side == 0 ? far : -far;
                for (int i = 0; i < 10; ++i) {
                    const NodeId nd = scene->createNode();
                    if (!nd || !scene->attachMesh(nd, plane, flatMat)) continue;
                    const float x = centre.x + sx + float(i % 5) * 1.5f;
                    const float z = centre.z + float(i / 5) * 1.5f;
                    enginetest::setNodePosition(scene, nd, Vec3(x, 0.5f, z));
                    movers.push_back(nd);
                    homeX.push_back(x);
                    homeZ.push_back(z);
                }
            }
            std::printf("   twenty flat movers wanted, %zu attached\n", movers.size());
            CHECK(movers.size() == 20u, "twenty flat movers, four more than the cap");
            render(e, 12);          // the spawn's own work drains

            const auto perCascade = [&]() {
                std::vector<unsigned long long> out;
                for (const auto &c : scene->published().gi.cascades) out.push_back(c.rebuilds);
                return out;
            };
            std::vector<unsigned long long> before = perCascade();
            for (int f = 0; f < 24; ++f) {
                for (size_t i = 0; i < movers.size(); ++i)
                    enginetest::setNodePosition(scene, movers[i],
                                                Vec3(homeX[i] + 0.3f * float(f), 0.5f, homeZ[i]));
                render(e, 1);
            }
            const std::vector<unsigned long long> during = perCascade();
            unsigned long long innerRebuilds = 0, outerRebuilds = 0;
            std::printf("   24 frames of 20 FLAT movers in two far clusters:");
            for (size_t i = 0; i < during.size(); ++i) {
                const unsigned long long d = during[i] - before[i];
                std::printf("  c%zu %llu", i, d);
                if (i + 1u == during.size()) outerRebuilds += d; else innerRebuilds += d;
            }
            std::printf("\n");
            CHECK(innerRebuilds == 0ull,
                  "a ZERO-HEIGHT box does not merge with a coplanar one on the other side of "
                  "the chain: the inner cascades stay clean (the margin metric; the volume one "
                  "merged them at zero growth and dirtied the middle)");
            CHECK(outerRebuilds > 0ull,
                  "...while the cascade that CAN see the flat movers follows them");

            for (NodeId m : movers) scene->removeNode(m);
            render(e, 6);
        }
    }
    }   // (edits: cases 14, 15, 15b)

    if (doSchedule) {   // (schedule: cases 16a-16d)
    // =====================================================================
    // CASE 16 — THE NEAR-FIELD GUARANTEE (CASCADE-STEP-1)
    //
    // THE RULE: the innermost cascade guarantees a near-field radius around the
    // head — `kGiNearFieldRadiusFraction` of its own half-size — inside which
    // the bounce is ALWAYS read from it and never from the coarser cascade
    // behind. It is pure arithmetic on the table (halfSize - step - cell, at the
    // step's placement quantum: the step the engine places by), so
    // 16a asserts it for EVERY TIER AND BOTH COLUMNS with no scene at all, and
    // 16b walks a camera and proves the geometry the arithmetic claims.
    // =====================================================================
    std::printf("\n== case 16a: the near-field guarantee, every tier and both columns ==\n");
    {
        bool allOk = true, everyRowOk = true;   // c0's claim, and EVERY row's
        const GiQuality qualities[4] = { GiQuality::Low, GiQuality::Medium, GiQuality::High,
                                         GiQuality::Epic };
        const char *qNames[4] = { "low", "medium", "high", "epic" };
        const GiViewProfile profiles[2] = { GiViewProfile::Desktop, GiViewProfile::Vr };
        const char *pNames[2] = { "desktop", "vr" };
        for (int q = 0; q < 4; ++q) {
            for (int pr = 0; pr < 2; ++pr) {
                GiQualityFacts f = giQualityFacts(qualities[q], profiles[pr]);
                giResolveCascadeSteps(f.cascades, f.cascadeCount);
                std::printf("   %-6s %-7s:", qNames[q], pNames[pr]);
                for (int i = 0; i < f.cascadeCount; ++i) {
                    // AT THE STEP THE ENGINE PLACES BY: the derived (or pinned) step's placement quantum (PHOTON-7
                    // stage 10, 10-2e: giPlacementQuantumCells - the largest power of two at most one cell past it).
                    GiParams::GiCascadeDesc d = f.cascades[i];
                    d.stepCells = giPlacementQuantumCells(d.stepCells);
                    const float r  = giCascadeGuaranteedRadius(d);
                    const float rq = giNearFieldRadius(d);
                    std::printf("  c%d half %5.1f res %3d step %5.2f m r %6.2f/%5.2f", i,
                                d.halfSize, d.resolution, d.stepCells * giCascadeCell(d), r, rq);
                    // CASCADE 0 IS THE PRODUCT CLAIM — the near field is what a
                    // walker reads. It must meet the required radius exactly.
                    if (i == 0 && r < rq - 1e-4f) allOk = false;
                    // ...AND SO MUST EVERY OTHER ROW, in both columns. The rule
                    // is a property of a cascade, not a property of being the
                    // innermost one: a mid cascade that guarantees less than its
                    // own fraction becomes the CHAIN's binding constraint — the
                    // nearest distance at which any hand-over in the chain can
                    // happen — however good cascade 0 is. Every shipped row
                    // honours it with room to spare (the outermost by a factor
                    // of 1.6 at Medium), so this is a rule the tables can be
                    // held to and not a bar they scrape past.
                    if (r < rq - 1e-4f) everyRowOk = false;
                }
                std::printf("\n");
            }
        }
        CHECK(allOk, "every tier and both columns: cascade 0 guarantees its near-field radius");
        CHECK(everyRowOk,
              "...AND EVERY OTHER ROW OF EVERY TIER AND BOTH COLUMNS honours it too "
              "(a mid cascade below its own fraction is the chain's binding constraint)");
    }

    std::printf("\n== case 16b: a 10 m walk never leaves the guarantee ==\n");
    {
        // The suite's own chain (the High tier table), walked at 1.4 m/s on the
        // 60 Hz clock — a person's pace, which is the case the rule is written
        // for. The instrument is giStatus: the camera's offset from cascade 0's
        // CENTRE, every frame, against `halfSize - guaranteedRadius`.
        //
        // IT STARTS BY PARKING. The previous case leaves the camera metres away,
        // and walking straight out of that relocation puts TWO cascades pending
        // on the first frame — with one rebuild a frame (and a field follow able
        // to own the slot) frame 1 would then read a centre from the old pose
        // and fail an assertion about a walk that had not begun. Park, let the
        // queue empty, and only then measure (the pattern of case 2). UNTIL IT IS
        // EMPTY, not a fixed count (PHOTON-7 stage 10, 10-2e: on the lattice the
        // 4.5 m relocation is two cut scrolls of cascade 0's six chunk frames, and
        // the old eight frames left the first five of the walk reading the old pose).
        //
        // AND IT STARTS OFF THE LATTICE. x0 is 0.37 m, not 0: the re-centre
        // planes are absolute world space, so a walk that begins on a plane
        // measures the one phase that trips immediately and never the WORST
        // phase, where the camera crosses a plane just after a frame boundary.
        const float stride = 1.4f / 60.0f;          // 0.0233 m a frame, walking
        const float x0 = 0.37f, z0 = 6.0f;
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(x0, 2.0f, z0),
                                                         Vec3(x0 + 1.0f, 2.0f, z0 - 1.0f)));
        render(e, 8);
        int parked = 8;
        const auto owed = [&]() {
            for (const GiStatus::CascadeStatus &c : scene->published().gi.cascades)
                if (c.pending || c.scrolling) return true;
            return false;
        };
        for (; parked < 240 && owed(); ++parked) render(e, 1);
        std::printf("   parked %d frames (the queue empty: %s)\n", parked, owed() ? "no" : "yes");
        const GiStatus w0 = scene->published().gi;
        CHECK(!w0.cascades.empty(), "the chain is up before the walk is measured");
        if (w0.cascades.empty()) { std::printf("   (no chain: the walk is skipped)\n"); }
        else {
        const float halfSize = w0.cascades[0].halfSize;
        const float required = 0.45f * halfSize;     // kGiNearFieldRadiusFraction
        const int frames = int(10.0f / stride);
        const unsigned long long reb0 = w0.cascades[0].rebuilds;
        float worstOff = 0.0f, worstGuarantee = 1e9f;
        bool ok = true;
        for (int f = 1; f <= frames; ++f) {
            const float x = x0 + float(f) * stride;
            view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, z0),
                                                             Vec3(x + 1.0f, 2.0f, z0 - 1.0f)));
            render(e, 1);
            const GiStatus s = scene->published().gi;
            const GiStatus::CascadeStatus &c = s.cascades[0];
            const float off = std::max(std::max(std::fabs(x - c.centre.x),
                                                std::fabs(2.0f - c.centre.y)),
                                       std::fabs(z0 - c.centre.z));
            worstOff = std::max(worstOff, off);
            worstGuarantee = std::min(worstGuarantee, c.halfSize - off);
            // THE TOLERANCE IS ONE STRIDE, and it is the honest bound rather
            // than a fudge: the re-centre test is a per-FRAME test, so the
            // camera is up to one frame's travel past the threshold when the
            // rebuild lands, and a rebuild the frame's one slot could not pay
            // adds another. The step is chosen with a cell of slack for exactly
            // this (giNearFieldMaxStepCells) — 0.078 m at this tier, four
            // strides — so the measurement below should clear `required`
            // outright; the stride is what the assertion may not.
            if (c.halfSize - off < required - stride) ok = false;
        }
        const GiStatus w1 = scene->published().gi;
        std::printf("   %d frames, 10.0 m at 1.4 m/s: worst offset from the centre %.3f m, "
                    "worst live guarantee %.3f m (required %.3f), c0 rebuilds %llu "
                    "(%.2f m per rebuild), deferrals %llu\n",
                    frames, worstOff, worstGuarantee, required,
                    w1.cascades[0].rebuilds - reb0,
                    (w1.cascades[0].rebuilds - reb0)
                        ? 10.0f / float(w1.cascades[0].rebuilds - reb0) : 0.0f,
                    w1.cascadeDeferrals);
        CHECK(ok, "ACROSS A 10 m WALK THE HEAD IS NEVER OUTSIDE CASCADE 0'S GUARANTEED RADIUS");
        CHECK(worstGuarantee >= required,
              "...and it clears the required radius OUTRIGHT, with the motion slack "
              "unspent (the tolerance above is the bound, not the budget)");
        // (ON THE LATTICE, PHOTON-7 stage 10, 10-2e: the centre is the camera rounded to the quantum and it re-centres
        // past (0.5 + h) of a step, so the supremum of the travel is 0.6 of a step - not one step, as it was against the
        // last re-centre's camera. The reported guarantee keeps the whole step and a cell (giCascadeGuaranteedRadius: the
        // scroll counts derive from it), so the walk holds it with 0.4 of a step to spare. Was "the hysteresis itself
        // costs no radius: the supremum of the travel is one step, not 1.1 of them".)
        CHECK(worstGuarantee >= w1.cascades[0].guaranteedRadius - stride,
              "...and the guarantee giStatus reports holds on the walk, to within the frame the "
              "per-frame test costs (the lattice's supremum is 0.6 of a step; the report keeps a whole one)");
        {
            // ...and the scroll's own lag: its chunk frames and its commit's (giCascadeScrollFrames, the count scroll_lag
            // holds to the table), walked while the readers keep the old box.
            GiParams::GiCascadeDesc d;
            d.halfSize = w1.cascades[0].halfSize;
            d.resolution = w1.cascades[0].resolution;
            d.stepCells = w1.cascades[0].step / w1.cascades[0].cell;
            const float bound = (0.5f + kGiStepHysteresis) * w1.cascades[0].step + float(1 + giCascadeScrollFrames(d)) * stride;
            std::printf("   the lattice's supremum: %.3f m (0.6 of the %.3f m step and %d frames of the scroll's lag)\n",
                        double(bound), double(w1.cascades[0].step), 1 + giCascadeScrollFrames(d));
            CHECK(worstOff <= bound + 1e-4f,
                  "...and the walk's worst offset is the lattice's supremum: (0.5 + h) of a step, the frame the test "
                  "costs and the scroll's chunk frames");
        }
        // THE WALK's OWN COUNT (HARNESS-2): the counter is the chain's lifetime total, and the park before the walk may be a
        // relocation the guard rightly answers (in the schedule row it is 25 m, from case 10's end); the law is the walk's.
        CHECK(w1.cascadeFullRebuilds == w0.cascadeFullRebuilds, "a 10 m walk is not a teleport (the walk adds no full rebuild)");
        }
    }

    // =====================================================================
    // CASE 16c — THE PLACEMENT QUANTUM IS TWO CELLS (PHOTON-7 stage 1, Q = 2)
    //
    // A cascade's centre is the camera ROUNDED to the nearest EVEN multiple of its cell (PhotonGi.cpp
    // jahQuantEven), so the directional store's 2 x 2 x 2 blocks sit on one world lattice at every placement. Placed
    // (the placement pin, then a forced whole rebuild of every cascade) for cameras in an ODD and in an EVEN cell of
    // cascade 0, each a quarter and three quarters into it, on all three axes at once: every cascade's centre is an
    // even number of its cells, within one cell of the camera on each axis (16a's `- cell` is that bound), and the
    // camera's own two neighbours across an odd/even boundary land on the same centre as the rounding says.
    // =====================================================================
    // ON THE LATTICE (PHOTON-7 stage 10, 10-2e): the centre is a multiple of the cascade's QUANTUM Q = 2^K cells (its
    // step) and within half a quantum of the camera - the old even placement was the lattice at Q = 2 ("an EVEN
    // multiple", "within one cell"). The camera's phases are the same fractions of the quantum they were of Q = 2:
    // 1/8, 3/8, 5/8, 7/8 of it (even + 1/4, even + 3/4, odd + 1/4, odd + 3/4 cells), both sides of the rounding.
    std::printf("\n== case 16c: the centre is on its quantum's lattice, at every phase of the camera ==\n");
    {
        const GiStatus g0 = scene->published().gi;
        CHECK(!g0.cascades.empty(), "the chain is up for the placement case");
        if (!g0.cascades.empty()) {
            const float cell0 = g0.cascades[0].cell;
            const float q0 = g0.cascades[0].step / cell0;   // cascade 0's quantum, cells
            bool allEven = true, allNear = true;
            const float phases[4] = { 1.125f, 1.375f, 1.625f, 1.875f };   // quanta: the four phases of Q = 2's cells
            for (float ph : phases) {
                const Vec3 p(11.0f * q0 * cell0 + ph * q0 * cell0, 1.5f + ph * q0 * cell0, -6.0f * q0 * cell0 - ph * q0 * cell0);
                GiPlacementPin pin;
                pin.on = true;
                pin.cascadeBuiltCam.assign(g0.cascades.size(), p);
                scene->setGiPlacementPin(pin);
                for (size_t i = 0; i < g0.cascades.size(); ++i) {
                    e->setArm("gi.rebuildCascade", double(i));
                    render(e, 2);
                }
                e->setArm("gi.rebuildCascade", -1.0);
                render(e, 4);
                const GiStatus g = scene->published().gi;
                std::printf("   camera (%.4f, %.4f, %.4f) = %.3f quanta of cascade 0 into its lattice:", p.x, p.y, p.z, ph);
                for (size_t i = 0; i < g.cascades.size(); ++i) {
                    const GiStatus::CascadeStatus &c = g.cascades[i];
                    const float cc[3] = { c.centre.x, c.centre.y, c.centre.z }, pp[3] = { p.x, p.y, p.z };
                    const double q = double(c.step) / double(c.cell);   // its quantum, cells
                    std::printf("  c%zu centre (%.4f, %.4f, %.4f) Q %.0f", i, c.centre.x, c.centre.y, c.centre.z, q);
                    for (int a = 0; a < 3; ++a) {
                        const double k = double(cc[a]) / (double(c.cell) * q);
                        if (std::fabs(k - std::round(k)) > 1e-5) allEven = false;
                        if (std::fabs(double(pp[a]) - double(cc[a])) > 0.5 * q * double(c.cell) * (1.0 + 1e-5)) allNear = false;
                    }
                }
                std::printf("\n");
            }
            scene->setGiPlacementPin(GiPlacementPin());
            render(e, 2);
            CHECK(allEven, "every cascade's centre is a multiple of its QUANTUM (Q = 2^K cells, its step), at every phase");
            CHECK(allNear, "...within half a quantum of the camera on every axis (the lattice's rounding)");
        }
    }

    // =====================================================================
    // CASE 16d — A PIN LEAVES A CASCADE FREE (PHOTON-7 stage 10-0's P2: the outer cascade pinned ALONE)
    //
    // The placement pin used to be a PREFIX of the chain (cascade i pinned iff i < cascadeBuiltCam's size), so an inner
    // cascade could never follow the eye while an outer one held. GiPlacementPin::cascadeFree leaves cascade i free:
    // every cascade pinned at a far point p, cascade 0 left free, then a forced whole rebuild of each - cascade 0 is
    // placed for the EYE, every other cascade for p: within half a quantum (the lattice's rounding, PHOTON-7 stage 10,
    // 10-2e; was "within one of its cells", half of Q = 2).
    // =====================================================================
    std::printf("\n== case 16d: a pin leaves cascade 0 free ==\n");
    {
        const Vec3 eye(0.0f, 2.0f, 6.0f);
        view->setCamera(enginetest::testCameraDescLookAt(eye, Vec3(0.0f, 1.0f, 0.0f)));
        render(e, 2);
        const GiStatus g0 = scene->published().gi;
        CHECK(g0.cascades.size() >= 2u, "the chain is up for the free-cascade case");
        if (g0.cascades.size() >= 2u) {
            const Vec3 p(40.0f, 6.0f, -40.0f);
            GiPlacementPin pin;
            pin.on = true;
            pin.cascadeBuiltCam.assign(g0.cascades.size(), p);
            pin.cascadeFree.assign(g0.cascades.size(), 0u);
            pin.cascadeFree[0] = 1u;
            scene->setGiPlacementPin(pin);
            for (size_t i = 0; i < g0.cascades.size(); ++i) {
                e->setArm("gi.rebuildCascade", double(i));
                render(e, 2);
            }
            e->setArm("gi.rebuildCascade", -1.0);
            render(e, 4);
            const GiStatus g = scene->published().gi;
            const auto near = [](const Vec3 &c, const Vec3 &q, float halfQuantum) {
                const float t = halfQuantum * (1.0f + 1e-5f);
                return std::fabs(c.x - q.x) <= t && std::fabs(c.y - q.y) <= t && std::fabs(c.z - q.z) <= t;
            };
            const bool freeAtEye = near(g.cascades[0].centre, eye, 0.5f * g.cascades[0].step);
            bool pinnedAtP = true;
            for (size_t i = 1; i < g.cascades.size(); ++i)
                pinnedAtP = pinnedAtP && near(g.cascades[i].centre, p, 0.5f * g.cascades[i].step);
            for (size_t i = 0; i < g.cascades.size(); ++i)
                std::printf("   c%zu centre (%.3f, %.3f, %.3f)%s\n", i, g.cascades[i].centre.x, g.cascades[i].centre.y,
                            g.cascades[i].centre.z, i == 0 ? " (free)" : " (pinned)");
            scene->setGiPlacementPin(GiPlacementPin());
            render(e, 2);
            CHECK(freeAtEye, "a cascade left FREE under a pin (GiPlacementPin::cascadeFree) is placed for the eye");
            CHECK(pinnedAtP, "...and every other cascade for the pin's camera");
        }
    }
    }   // (schedule: cases 16a-16d)

    // The arm must come down cleanly — the chain's extra cascades are owned by
    // the scene and die with it (the teardown order the arm requires).
    GiParams off; off.mode = GiMode::Off;
    CHECK(scene->setGlobalIllumination(off), "the chain comes down");
    render(e, 2);
    CHECK(scene->published().gi.cascades.empty(), "and leaves no cascades behind");

    std::printf("\nframes rendered: %lld (part %s: the row's FRAMES)\n", gFrames, part.c_str());
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
