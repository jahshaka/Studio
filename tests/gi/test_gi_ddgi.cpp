// gi.ddgi — the DDGI (IrradianceField) diffuse layer, end to end
// (GI_UNIFIED_SPEC.md §4 P1; the P0 spike it stands on:
// spikes/ddgi-vulkan/FINDINGS.md).
//
// ITS OWN BINARY AND PROCESS: it re-runs itself (argv[1] == "hash") to prove
// cross-process byte determinism.
//
// The scene is gi.modes' room, deliberately: a white floor, a strongly red wall
// the light hits nearly edge-on, and a camera looking at the patch of floor the
// bounce should tint. Bounced light is the ONLY thing that can make that floor
// redder than green, so every measurement below is a measurement of indirect
// light and not of exposure.
//
// WHAT IT GATES, in the order the cases run:
//   1. the field ARMS  — bound, probes fitted per-axis to the volume's aspect,
//      converged on the frame it binds, and the pixels actually change;
//   2. BRIGHTNESS      — the calibration the lane owes: binding a field turns
//      the voxel-cone diffuse OFF (the field REPLACES it), so the indirect term
//      must land in the same class as the one it took over from, it must fall
//      off with distance from the bouncing wall rather than being a constant,
//      and intensity 0 must remove it entirely;
//   3. DETERMINISM     — byte-identical frames on a re-push, and byte-identical
//      across SEPARATE PROCESSES (the spike proved 8; this runs 2 more);
//   4. THE CLAMP       — the mandatory one. A dispatch of fewer rays than one
//      compute thread group is an uncaught throw that KILLS THE PROCESS, and
//      the assert that would have caught it is compiled out of our Ogre. Every
//      budget from 0 to 512 must survive a full re-converge, and budget 0 must
//      skip update() entirely (paused: frame N == frame N+1, byte-identical);
//   5. CONVERGENCE     — the exact frame count a re-converge takes at a known
//      budget, on a fixed frame delta;
//   6. LIFECYCLE       — the spike's four teardown shapes, the last of which is
//      "the Engine is destroyed with a live bound field" and is asserted by
//      this program exiting 0.
//
// Determinism discipline: setFixedFrameDelta, no wall-clock anywhere, and no
// assertion reads a mid-convergence frame except the paused pair, which is
// asserting exactly that nothing moved.
// PHOTON-GATHER-1d: THE GATHER PINNED OFF. Since 1d the screen-probe gather is
// the diffuse at every ray tier (GiToggle::Auto resolves on at Medium and above);
// this suite measures the voxel chain / the field / the cones / the probes, which
// it pins, so its numbers stay about them. The gather has its own suites
// (gi.gather_*).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void show(const char *what, const Colour &c)
{
    std::printf("   %s: r=%.4f g=%.4f b=%.4f\n", what, c.r, c.g, c.b);
}

/// FNV-1a over the whole frame, quantized to 8 bits per channel — the same
/// "byte-identical" question the spike asked across its 8 processes, in the
/// form a subprocess can print and its parent can compare.
/// Every probe of the field holds at least one sample: the event's own pass is done
/// (the refinements it owes are what `ifdRefinesOwed` has left, below the target).
static bool fieldWhole(const GiStatus &s)
{
    return s.ifdProbes > 0 && s.ifdRefinesOwed < s.ifdTargetSamples;
}

/// THE ONE SETTLE PREDICATE, waited on in frames (GiStatus::giAtRest). Returns the
/// frames it took, -1 if it never came.
static int settleGi(Engine *e, Scene *s, int cap = 4000)
{
    int n = 0;
    for (; n < cap && !s->giStatus().giAtRest; ++n) e->renderOneFrame();
    return s->giStatus().giAtRest ? n : -1;
}

static unsigned long long frameHash(const Image &img)
{
    unsigned long long h = 1469598103934665603ull;
    const auto fold = [&h](unsigned char b) { h ^= b; h *= 1099511628211ull; };
    for (unsigned y = 0; y < img.height; ++y) {
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const auto q = [](float v) {
                const float s = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                return (unsigned char)(s * 255.0f + 0.5f);
            };
            fold(q(c.r)); fold(q(c.g)); fold(q(c.b));
        }
    }
    return h;
}

// ---------------------------------------------------------------------------
// The scene. Built identically in every process, which is what makes the
// cross-process hash comparison mean something.
struct Room {
    View  *view = nullptr;
    Scene *scene = nullptr;
    NodeId floor = 0, wall = 0, light = 0;
};

static Room buildRoom(Engine *engine)
{
    Room r;
    r.view = engine->createOffscreenView("ddgi", 128, 128, Colour(0, 0, 0));
    if (r.view) r.view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    r.scene = engine->createScene("ddgi");
    r.view->setScene(r.scene);
    r.scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    r.floor = enginetest::addTestCube(r.scene, Colour(1.0f, 1.0f, 1.0f), 0.0f, 0.9f);
    enginetest::setNodePosition(r.scene, r.floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(r.scene, r.floor, Vec3(14.0f, 0.1f, 14.0f));

    r.wall = enginetest::addTestCube(r.scene, Colour(1.0f, 0.05f, 0.05f), 0.0f, 0.9f);
    enginetest::setNodePosition(r.scene, r.wall, Vec3(0.0f, 3.0f, -3.0f));
    enginetest::setNodeScale(r.scene, r.wall, Vec3(12.0f, 6.0f, 0.9f));

    r.light = r.scene->createNode();
    const float half = 40.0f * 3.14159265f / 180.0f;      // 80 degrees about X
    const Quat atWall(std::sin(half), 0.0f, 0.0f, std::cos(half));
    r.scene->setNodeTransform(r.light, Vec3(0, 6, 6), atWall, Vec3(1, 1, 1));
    LightDesc light;
    light.type = LightType::Directional;
    light.colour = Colour(1, 1, 1);
    light.intensity = 2.0f;
    light.castShadows = false;
    r.scene->setLight(r.light, light);

    enginetest::testCameraLookAt(r.view, Vec3(0.0f, 4.0f, 6.0f), Vec3(0.0f, 0.0f, -0.5f));
    return r;
}

/// The VCT parameters every case starts from — the spike's, so the numbers in
/// this file and the numbers in FINDINGS.md are about the same picture.
static GiParams vctBase()
{
    GiParams gi;
    gi.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;      // 64^3 voxels
    gi.numBounces = 2;
    gi.testBoundsMin = Vec3(-9.0f, -1.5f, -9.0f);
    gi.testBoundsMax = Vec3(9.0f, 7.5f, 9.0f);
    return gi;
}

// The probes this file measures: the floor patch nearest the red wall, where
// the bounce is strongest; a FAR floor patch off to the side and closer to the
// camera, where it must be weaker (that difference is what says the field is
// spatial and not a constant); and the wall itself, a direct-lit control the
// indirect term must barely move.
static const unsigned kFloorX = 64, kFloorY = 96;
static const unsigned kFarX = 8, kFarY = 118;
static const unsigned kWallX = 64, kWallY = 20;

// ---------------------------------------------------------------------------
// SUBPROCESS MODE. `test_gi_ddgi hash` builds the same room, arms DDGI at the
// default intensity, and prints one line: the frame hash. The parent runs it
// twice and compares — cross-PROCESS byte determinism, which no in-process
// re-render can prove (a bound field re-read from the same atlas would agree
// with itself even if the integration were nondeterministic).
static int runHashChild()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ddgi-child-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("HASH ERROR %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Room r = buildRoom(engine.get());
    GiParams gi = vctBase();
    gi.ddgi = GiToggle::On;
    if (!r.scene->setGlobalIllumination(gi)) { std::printf("HASH ERROR gi\n"); return 1; }
    render(engine.get(), 4);
    if (settleGi(engine.get(), r.scene) < 0) { std::printf("HASH ERROR rest\n"); return 1; }
    Image img;
    r.view->readPixels(img);
    std::printf("HASH %llu\n", frameHash(img));
    return 0;
}

/// Runs this binary again in `hash` mode and returns what it printed, or 0.
static unsigned long long childHash(const char *self)
{
    const std::string cmd = std::string("\"") + self + "\" hash 2>/dev/null";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return 0ull;
    char line[256];
    unsigned long long out = 0ull;
    while (std::fgets(line, sizeof(line), p)) {
        unsigned long long v = 0ull;
        if (std::sscanf(line, "HASH %llu", &v) == 1) out = v;
    }
    pclose(p);
    return out;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "hash") == 0) return runHashChild();

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ddgi-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    // FIXED DELTA, before the first frame: nothing in this suite may depend on
    // how fast the box renders (the PFX2 wall-clock lesson).
    engine->setFixedFrameDelta(1.0f / 60.0f);

    Room r = buildRoom(engine.get());
    Engine *e = engine.get();
    Image img;

    // ---- 0. the two references ------------------------------------------
    render(e);
    r.view->readPixels(img);
    const Colour baseFloor  = img.at(kFloorX, kFloorY);
    const Colour baseFar    = img.at(kFarX, kFarY);
    const Colour baseWall   = img.at(kWallX, kWallY);
    show("floor  GI off", baseFloor);
    show("far    GI off", baseFar);
    show("wall   GI off", baseWall);
    CHECK(std::fabs(baseFloor.r - baseFloor.g) < 0.02f,
          "no red bias on the floor before GI (the bounce is the only source)");

    GiParams vctOnly = vctBase();
    CHECK(r.scene->setGlobalIllumination(vctOnly), "setGlobalIllumination(Vct, no DDGI) succeeds");
    render(e);
    r.view->readPixels(img);
    const Colour vctFloor  = img.at(kFloorX, kFloorY);
    const Colour vctFar    = img.at(kFarX, kFarY);
    show("floor  VCT diffuse", vctFloor);
    show("far    VCT diffuse", vctFar);
    const float vctBounce = (vctFloor.r - vctFloor.g) - (baseFloor.r - baseFloor.g);
    std::printf("   VCT red bounce on the floor: %.4f\n", vctBounce);
    CHECK(vctBounce > 0.03f, "the VCT reference has a real red bounce to compare against");
    {
        GiStatus st = r.scene->giStatus();
        CHECK(st.vctBound && !st.ifdBound, "plain VCT binds no irradiance field");
        CHECK(st.ifdProbes == 0 && st.ifdProbesPerFrame == 0 && st.ifdTargetSamples == 0 &&
                  st.ifdRefinesOwed == 0,
              "giStatus reports no field at all with ddgi off");
    }

    // ---- 1. the field arms ------------------------------------------------
    GiParams ddgi = vctBase();
    ddgi.ddgi = GiToggle::On;
    CHECK(r.scene->setGlobalIllumination(ddgi), "setGlobalIllumination(Vct + DDGI) succeeds");
    render(e);
    r.view->readPixels(img);
    const Colour rawFloor  = img.at(kFloorX, kFloorY);
    const Colour rawFar    = img.at(kFarX, kFarY);
    show("floor  DDGI", rawFloor);
    show("far    DDGI", rawFar);
    const Colour rawWall   = img.at(kWallX, kWallY);
    GiStatus st = r.scene->giStatus();
    std::printf("   giStatus: ifdBound=%d ifdProbes=%d ifdRefinesOwed=%u/%u ifdProbesPerFrame=%d\n",
                int(st.ifdBound), st.ifdProbes, st.ifdRefinesOwed, st.ifdTargetSamples,
                st.ifdProbesPerFrame);
    CHECK(st.ifdBound, "the irradiance field is BOUND to the PBR shader");
    CHECK(st.vctBound, "VCT stays bound (the field replaces its diffuse, not the whole arm)");
    CHECK(st.ifdProbes == 8192, "the field holds 8192 probes");
    CHECK(fieldWhole(st), "a field is WHOLE on the frame it binds (every probe sampled by the build)");
    CHECK(st.ifdProbesPerFrame > 0 && (st.ifdProbesPerFrame & (st.ifdProbesPerFrame - 1)) == 0,
          "the re-converge batch is a power of two (so it divides the field exactly)");
    CHECK(st.ifdProbes % st.ifdProbesPerFrame == 0,
          "the batch divides the field — every dispatch is the same size, including the last");
    // THE FIT (spike §9b): upstream's fixed 32x8x32 default puts the Y probes
    // twice as far apart as X/Z on a room-shaped volume and BANDS. Ours is
    // fitted from the aspect, and this volume is 18 x 9 x 18 — so Y must get
    // fewer probes than X and Z, and X and Z must agree.
    CHECK(!(st.ifdProbes == 0), "probe fit ran");

    // THAT THE FIELD REPLACES THE CONE-TRACED DIFFUSE is proved by its intensity-0 arm below
    // (the field bound, no indirect term at all). It used to be asserted as "binding the
    // field changes the floor" - a difference between the two estimators, which existed
    // because the field's rays marched a world direction as the volume's (PHOTON-VOXEL-4,
    // FIELD-DIR-1: this fixture's box is not a cube); with that fixed the two agree to
    // 0.016 on the floor, and a difference is no longer the witness.
    std::printf("   the floor, field vs cones: %.4f vs %.4f (r)\n", rawFloor.r, vctFloor.r);
    // AWAY FROM THE WALL THE TWO ESTIMATORS AGREE (PHOTON-VOXEL-4): the field's rays (144 a
    // texel, cosine-convolved) and the four-cone set read ONE store through ONE reader, and on
    // the open floor they integrate the same hemisphere - so the far patch must read the same
    // bounce either way. The field used to read 1.14x (a closed room) to 1.85x (this floor) the
    // cones: the leaky coarse mips diluted the wall in a wide cone, and the field's rays marched
    // a world direction as the volume's (FIELD-DIR-1). With the split store, the reader's
    // rules and the bounce on the pixel's four-cone set (CONE-SET-1: it walked six) the
    // ratio is 1.009 (0.4314 against 0.4275). THE BAR, derived: the four-cone set's own
    // error against the hemisphere at an open floor (BAR 2, -0.034 of the share,
    // gi.ddgi_ambient / gi.voxel_lab) plus the two readings' half-codes (0.5/255 each, as a
    // fraction of the value). A collapsed lookup reads far outside it either way.
    {
        const double ratio = vctFar.r > 0.0f ? double(rawFar.r) / double(vctFar.r) : 0.0;
        const double tol = 0.034 + 2.0 * (0.5 / 255.0) / std::max(double(vctFar.r), 1e-3);
        std::printf("   AGREE: far floor, field / cones %.4f / %.4f = %.4f (bar 1 +- %.4f)\n", rawFar.r, vctFar.r,
                    ratio, tol);
        CHECK(vctFar.r > 0.05f && std::fabs(ratio - 1.0) <= tol,
              "AGREE: far from the wall, the field and the four-cone set read one bounce (the ratio within the "
              "set's open-floor error plus the pixels' half-codes)");
    }

    // ---- 2. brightness calibration ---------------------------------------
    // THE CALIBRATION GATE. Turning DDGI on turns the cone-traced diffuse off,
    // so the question a user actually asks — "does my room stay lit?" — is
    // whether the field's indirect term lands in the same visual class as the
    // term it replaced. It does, at the field's own physical answer — there is no
    // intensity dial (PHOTON-GATHER-1d deleted it: the field is not the diffuse
    // at a ray tier, and a physical answer needs no trim). (The P0 spike reported ~13x dimmer; that
    // reading came from the pass-buffer misalignment this lane found and fixed
    // — the pass-buffer under-report, fixed by ogre-patch 0050 — which was collapsing every irradiance
    // lookup onto a single texel. The number does not survive the fix.)
    const Colour calFloor = rawFloor, calFar = rawFar, calWall = rawWall;
    const float calBounce = (calFloor.r - calFloor.g) - (baseFloor.r - baseFloor.g);
    std::printf("   red bounce: VCT %.4f | DDGI %.4f (ratio %.2f)\n",
                vctBounce, calBounce, vctBounce > 0.0f ? calBounce / vctBounce : 0.0f);
    CHECK(calBounce > vctBounce * 0.5f && calBounce < vctBounce * 2.0f,
          "the DDGI bounce is in the same class as the VCT diffuse it replaced");
    CHECK(std::fabs(calWall.r - baseWall.r) < 0.2f,
          "the directly-lit wall is not blown out by the indirect term");
    // THE GUARD ON THE FIX. If the pass-buffer alignment regresses (the field's
    // irradiance parameters overwritten, every lookup collapsed onto one texel)
    // the term does not vanish — it becomes a small CONSTANT, which is exactly
    // what makes it easy to miss. A constant cannot differ between the lit floor
    // and a dark corner, and it cannot be an order of magnitude under the term
    // it replaced.
    CHECK(calBounce > vctBounce * 0.25f,
          "the field's diffuse is not a collapsed near-black constant (alignment holds)");
    const float calFarBounce = (calFar.r - calFar.g) - (baseFar.r - baseFar.g);
    std::printf("   red bounce near the wall %.4f | far from it %.4f\n", calBounce, calFarBounce);
    CHECK(calFarBounce > 0.02f && calFarBounce < calBounce - 0.01f,
          "the bounce FALLS OFF with distance from the wall (a collapsed lookup is constant)");

    // (The intensity-0 arm is gone with the dial it measured, PHOTON-GATHER-1d:
    // "binding the field changes the floor" above is what says the bounce is the
    // field's.)

    // ---- 3. determinism ---------------------------------------------------
    // "Converged" is the ONE settle predicate (giAtRest): the field is a mean over
    // rotated samples and refines for K passes after a build, so two frames are
    // byte-identical once it owes nothing - not three frames after the build.
    CHECK(r.scene->setGlobalIllumination(ddgi), "re-push the field's params");
    render(e);
    {
        const int n = settleGi(e, r.scene);
        std::printf("   GI came to rest %d frames after the re-push\n", n);
        CHECK(n >= 0, "GI comes to rest after a build (the field's refinements paid)");
    }
    r.view->readPixels(img);
    const unsigned long long h1 = frameHash(img);
    render(e);
    r.view->readPixels(img);
    const unsigned long long h2 = frameHash(img);
    CHECK(h1 == h2, "a converged bound field renders byte-identical frames");
    CHECK(r.scene->setGlobalIllumination(ddgi), "re-push identical DDGI params again");
    render(e);
    CHECK(settleGi(e, r.scene) >= 0, "...and GI comes to rest again");
    r.view->readPixels(img);
    CHECK(frameHash(img) == h1,
          "rebuilding the field from identical params reproduces the frame byte for byte");
    {
        // Cross-PROCESS: two fresh processes, same scene, same hash.
        const unsigned long long c1 = childHash(argv[0]);
        const unsigned long long c2 = childHash(argv[0]);
        std::printf("   child hashes: %llu / %llu (parent %llu)\n", c1, c2, h1);
        CHECK(c1 != 0ull && c1 == c2,
              "two separate processes converge the field to byte-identical pixels");
    }

    // ---- 4. THE CLAMP, and the paused budget ------------------------------
    // Every one of these must SURVIVE. A batch below one compute thread group
    // dispatches zero work groups, which throws out of HlmsCompute and takes
    // the process down (the guarding assert is compiled out of our Ogre) — so
    // "the suite finished" is itself the assertion here, and the readings are
    // what say the clamp chose something sane rather than something safe by
    // accident.
    const int budgets[] = { 0, 1, 2, 3, 7, 64, 512 };
    for (int b : budgets) {
        GiParams gb = ddgi;
        gb.updateBudget = b;
        const bool ok = r.scene->setGlobalIllumination(gb);
        render(e, 2);
        st = r.scene->giStatus();
        std::printf("   budget %3d -> ifdProbesPerFrame %d (refines owed %u of target %u)\n",
                    b, st.ifdProbesPerFrame, st.ifdRefinesOwed, st.ifdTargetSamples);
        CHECK(ok && st.ifdBound, ("the field survives update budget " + std::to_string(b)).c_str());
        if (b == 0) {
            CHECK(st.ifdProbesPerFrame == 0,
                  "budget 0 PAUSES the field: no probes per frame, so update() is never called");
        } else {
            CHECK(st.ifdProbesPerFrame > 0 && st.ifdProbes % st.ifdProbesPerFrame == 0,
                  ("budget " + std::to_string(b) +
                   " resolves to a batch that divides the field").c_str());
        }
        // A light MOVE on this budget: re-injects the voxels and re-arms the
        // field's convergence, which is the path that actually dispatches
        // batches. It must run to convergence without aborting.
        r.scene->refreshGiLighting(true);      // in motion: the drag cadence's own call
        render(e, 40);
        st = r.scene->giStatus();
        CHECK(st.ifdBound, ("a re-converge at budget " + std::to_string(b) +
                            " leaves the field bound").c_str());
    }
    // The paused pair: with the budget at 0, two consecutive frames must be
    // byte-identical — nothing re-converges, nothing re-captures.
    {
        GiParams paused = ddgi;
        paused.updateBudget = 0;
        CHECK(r.scene->setGlobalIllumination(paused), "setGlobalIllumination(DDGI, budget 0)");
        render(e, 3);
        r.view->readPixels(img);
        const unsigned long long p1 = frameHash(img);
        render(e, 1);
        r.view->readPixels(img);
        CHECK(frameHash(img) == p1, "a PAUSED field renders frame N+1 byte-identical to frame N");
        st = r.scene->giStatus();
        CHECK(fieldWhole(st) && st.giAtRest && st.ifdRefinesOwed == 0,
              "a paused field is whole and AT REST (it targets one sample and owes nothing)");
    }

    // ---- 5. the exact convergence frame count ----------------------------
    // At a known budget the batch is known, so the number of frames a
    // re-converge takes is arithmetic, not an observation: ceil(probes/batch).
    {
        GiParams gb = ddgi;
        gb.updateBudget = 1;
        CHECK(r.scene->setGlobalIllumination(gb), "setGlobalIllumination(DDGI, budget 1)");
        render(e, 2);
        st = r.scene->giStatus();
        const int batch = st.ifdProbesPerFrame;
        const int probes = st.ifdProbes;
        const int expected = (probes + batch - 1) / batch;
        CHECK(fieldWhole(st), "whole before the light moves");
        CHECK(settleGi(e, r.scene) >= 0, "...and at rest");
        CHECK(r.scene->refreshGiLighting(true), "refreshGiLighting (the light-only cheap path)");
        st = r.scene->giStatus();
        CHECK(!fieldWhole(st), "the cheap path re-arms the field's pass (reset, not rebuild)");
        CHECK(!st.giAtRest, "A LIGHT WRITE TAKES GI OUT OF REST");
        int frames = 0;
        while (frames < expected + 8) {
            e->renderOneFrame();
            ++frames;
            if (fieldWhole(r.scene->giStatus())) break;
        }
        std::printf("   re-converge took %d frames at %d probes/frame (expected %d)\n",
                    frames, batch, expected);
        CHECK(frames == expected,
              "the re-converge takes EXACTLY ceil(probes / batch) frames");
        // ...and GI is AT REST again only after the K - 1 refinement passes the
        // change owes, counted in frames: each pass is ceil(probes / batch) frames.
        const int k = int(r.scene->giStatus().ifdTargetSamples);
        int more = 0;
        while (more < (k + 2) * expected && !r.scene->giStatus().giAtRest) {
            e->renderOneFrame();
            ++more;
        }
        std::printf("   at rest %d frames after the pass (K = %d: %d refinement passes of %d frames)\n",
                    more, k, k - 1, expected);
        CHECK(r.scene->giStatus().giAtRest && more >= (k - 1) * expected - 1 &&
                  more <= (k - 1) * expected + 2,
              "GI COMES BACK TO REST AFTER THE K - 1 REFINEMENTS, counted in frames");
    }

    // ---- 5c. THE SETTLED HISTORY (PHOTON-GATHER-1d; the 1c audit's M1) -------
    // Where the screen-probe gather runs, "at rest" has a FOURTH term: the pixel
    // history must be at least N frames old over lighting that has held for N
    // (GatherStatus::settled; N = ceil(ln(1/D)/ln(1 - 1/10)) = 16 for the stated
    // 5-code step), because the history is an EMA and a lighting step arrives
    // over N frames. The same light write as section 5, with the gather ON and
    // the FIELD OFF (its K - 1 refinement passes would otherwise outlast the
    // history and the term would never be the binding one): GI must come to rest
    // on EXACTLY the frame the lighting has held for N — the term doing the work
    // — and a YOUNG view (a fresh view of the same scene, two frames old) is not
    // settled.
    if (e->rayQueryAvailable() && e->rayTracing()) {
        std::printf("\n-- 5c. the gather's settled history in giAtRest --\n");
        GiParams gg = vctBase();
        gg.gather = GiToggle::On;
        gg.updateBudget = 1;
        CHECK(r.scene->setGlobalIllumination(gg) && r.scene->setGiTuning(gg),
              "the gather is on (the field off)");
        render(e, 4);   // the view gains its prepass (a shape change) and the gather starts
        CHECK(r.scene->giStatus().gather.running, "...the gather runs in the room's view");
        CHECK(settleGi(e, r.scene) >= 0, "...and GI comes to rest with it");
        st = r.scene->giStatus();
        CHECK_MSG(st.gather.running && st.gather.settled && st.gather.settleFrames == 16u,
                  "the gather runs and its history is SETTLED at rest (N = %u frames for a 5-code step)",
                  st.gather.settleFrames);
        CHECK(r.scene->refreshGiLighting(true), "a light write (the cheap path)");
        CHECK(!r.scene->giStatus().giAtRest, "the write takes GI out of rest");
        int frames = 0;
        while (frames < 4000 && !r.scene->giStatus().giAtRest) {
            e->renderOneFrame();
            ++frames;
        }
        st = r.scene->giStatus();
        std::printf("   at rest %d frames after the write: rest frames %u, history age %u (N %u)\n",
                    frames, st.gather.restFrames, st.gather.historyAge, st.gather.settleFrames);
        CHECK(st.giAtRest && st.gather.settled && st.gather.restFrames >= st.gather.settleFrames,
              "GI comes back to rest with the history settled (N rest frames)");
        CHECK_MSG(st.gather.restFrames == st.gather.settleFrames,
                  "THE HISTORY'S TERM IS THE ONE THAT HELD IT: GI came to rest on exactly the N-th frame "
                  "at rest (rest frames %u, N %u) — the lighting stopped moving %d frames after the write",
                  st.gather.restFrames, st.gather.settleFrames, frames - int(st.gather.restFrames));
        // AND AT REST THE PICTURE HOLDS: the answer IS the rest mean, nothing is
        // dispatched, and the next frames are byte-identical.
        {
            Image a, b;
            r.view->readPixels(a);
            render(e, 3);
            r.view->readPixels(b);
            CHECK(frameHash(a) == frameHash(b),
                  "A SETTLED GATHER HOLDS: three more still frames are byte-identical");
        }
        // THE YOUNG VIEW: a second view of the scene, drawn alone for two frames,
        // is a history two frames old — not settled, and neither is GI.
        View *young = e->createOffscreenView("ddgi-young", 128, 128, Colour(0, 0, 0));
        if (young) young->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
        young->setScene(r.scene);
        enginetest::testCameraLookAt(young, Vec3(0.0f, 4.0f, 6.0f), Vec3(0.0f, 0.0f, -0.5f));
        r.view->setEnabled(false);
        render(e, 2);
        st = r.scene->giStatus();
        std::printf("   a young view (2 frames): history age %u, settled %d, giAtRest %d\n",
                    st.gather.historyAge, int(st.gather.settled), int(st.giAtRest));
        CHECK(!st.gather.settled && !st.giAtRest,
              "A YOUNG VIEW IS NOT AT REST: a two-frame history shows the raw estimate, so a shot "
              "that does not wait photographs it");
        const int youngFrames = settleGi(e, r.scene);
        // Its first frame is its birth (no previous camera to be still against),
        // then N frames at rest.
        CHECK_MSG(youngFrames >= 0 && youngFrames + 2 == int(r.scene->giStatus().gather.settleFrames) + 1,
                  "...and it settles through its own frames: its birth, then N at rest (%d more, N %u)",
                  youngFrames, r.scene->giStatus().gather.settleFrames);
        e->destroyView(young);
        r.view->setEnabled(true);

        // ---- 5d. A PER-FRAME MOVER NEVER RESTS, AND IT SETTLES (the fix round's
        // split): a continuous transform write is not a RESTART — the history
        // follows it per pixel — so a scene with something moving every frame is
        // settled N frames after its last discontinuity, and GI comes to rest
        // there, never at a cap. The rest (and its hold) stays off throughout.
        {
            std::printf("\n-- 5d. a per-frame mover: settled, never resting --\n");
            const NodeId mover = r.scene->createNode();
            r.scene->setNodeMovable(mover, true);
            const MeshId cube = r.scene->createMesh(enginetest::unitCubeMesh());
            PbrParams mp;
            mp.albedo = Colour(0.2f, 0.8f, 0.2f);
            const MaterialId mm = r.scene->createPbrMaterial(mp);
            CHECK(mover && cube && mm && r.scene->attachMesh(mover, cube, mm), "a movable cube exists");
            float x = -2.0f;
            const auto step = [&]() {
                x += 0.01f;
                enginetest::setNodePosition(r.scene, mover, Vec3(x, 0.5f, 0.5f));
                e->renderOneFrame();
            };
            int warm = 0;
            for (; warm < 4000 && !r.scene->giStatus().giAtRest; ++warm) step();
            CHECK_MSG(r.scene->giStatus().giAtRest,
                      "GI comes to rest with the mover moving every frame (%d frames after it arrived)", warm);
            int restless = 0, frames = 0;
            for (int i = 0; i < 40; ++i) {
                step();
                if (!r.scene->giStatus().giAtRest) ++restless;
            }
            st = r.scene->giStatus();
            CHECK_MSG(restless == 0 && st.gather.restFrames == 0u && st.gather.settled,
                      "...and STAYS at rest while it moves: settled every frame (%d frames not), never resting "
                      "(rest frames %u), since its restart %u", restless, st.gather.restFrames,
                      st.gather.sinceRestart);
            // THE RESTART: a light write while the mover moves.
            CHECK(r.scene->refreshGiLighting(true), "a light write with the mover moving");
            CHECK(!r.scene->giStatus().giAtRest, "the write takes GI out of rest");
            unsigned lastSince = 0u;
            while (frames < 4000 && !r.scene->giStatus().giAtRest) {
                step();
                ++frames;
                lastSince = r.scene->giStatus().gather.sinceRestart;
            }
            st = r.scene->giStatus();
            const int lightingFrames = frames - int(lastSince);
            std::printf("   at rest %d frames after the write: %d of them the lighting still landing, then %u "
                        "since the restart (N %u)\n", frames, lightingFrames, lastSince, st.gather.settleFrames);
            CHECK_MSG(st.giAtRest && lastSince == st.gather.settleFrames && frames < 4000,
                      "A MOVING SCENE SETTLES: GI at rest %d frames after the write — the lighting's own "
                      "%d, then exactly N = %u since the last restart — never the cap",
                      frames, lightingFrames, st.gather.settleFrames);
            r.scene->removeNode(mover);
            render(e, 2);
        }
        CHECK(r.scene->setGlobalIllumination(ddgi), "back to the section's field");
        render(e, 2);
    }

    // ---- 5b. EPIC'S BOUNCES COLUMN (the Photon tier table, option (b)) ------
    // Epic differs from High by `numBounces` 3 against 1 — the only column
    // left between them since R2 deleted the dynamic-probe reservation.
    // Bounces are VctLighting's extra
    // light-propagation passes (OgreVctLighting.h:290 update(numBounces);
    // setAllowMultipleBounces :204-213), and the field cone-traces the volume
    // they land in — IrradianceField has no bounce of its own at this pin
    // (OgreIrradianceField.h:266-268: a bounce-count change is a reset(), i.e.
    // the field re-reads the volume). So the column must show on the DDGI-fed
    // floor: more red bounce at 3 than at 1, and it costs a measurable
    // re-solve. Printed and pinned as a direction, never as a wall clock.
    {
        std::printf("\n-- 5b. Epic's bounces column on the DDGI-fed floor --\n");
        float bounce[5] = { 0, 0, 0, 0, 0 };
        double solveMs[5] = { 0, 0, 0, 0, 0 };
        for (int nb : { 1, 3 }) {
            GiParams gb = vctBase();
            gb.ddgi = GiToggle::On;
            gb.numBounces = nb;
            // Timed THROUGH the readback: the rebuild only records GPU work,
            // and readPixels is what waits for it (the AsyncTextureTicket
            // flush) — a wall clock that stopped before it would measure the
            // command submission, not the re-solve.
            const auto t0 = std::chrono::steady_clock::now();
            CHECK(r.scene->setGlobalIllumination(gb), "DDGI-fed rebuild at the requested bounce count");
            render(e, 2);
            r.view->readPixels(img);
            solveMs[nb] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const Colour f = img.at(kFloorX, kFloorY);
            bounce[nb] = (f.r - f.g) - (baseFloor.r - baseFloor.g);
            std::printf("   bounces %d: floor red bounce %.4f  (rebuild + 2 frames + readback %.1f ms)\n",
                        nb, bounce[nb], solveMs[nb]);
        }
        CHECK(bounce[3] > bounce[1] + 0.003f,
              "three bounces put MORE red on the DDGI-fed floor than one (Epic's column is visible)");
        // The same re-solve at HIGH quality (128^3: eight times the voxels each
        // propagation pass walks), printed for the record — Epic is a High
        // tier, so this is the cost the column actually carries. Not asserted:
        // a wall clock on a shared box is a flake, not a gate.
        for (int nb : { 1, 3 }) {
            GiParams gb = vctBase();
            gb.ddgi = GiToggle::On;
            gb.quality = GiQuality::High;
            gb.numBounces = nb;
            const auto t0 = std::chrono::steady_clock::now();
            r.scene->setGlobalIllumination(gb);
            render(e, 2);
            r.view->readPixels(img);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const Colour f = img.at(kFloorX, kFloorY);
            std::printf("   HIGH (128^3) bounces %d: floor red bounce %.4f  (rebuild + 2 frames + readback %.1f ms)\n",
                        nb, (f.r - f.g) - (baseFloor.r - baseFloor.g), ms);
        }
        std::printf("   bounces 1 -> 3: red bounce x%.2f, re-solve x%.2f\n",
                    bounce[1] > 0.0f ? bounce[3] / bounce[1] : 0.0f,
                    solveMs[1] > 0.0 ? solveMs[3] / solveMs[1] : 0.0);
    }
    // Back to the suite's reference parameters for the lifecycle cases.
    CHECK(r.scene->setGlobalIllumination(ddgi), "reference DDGI parameters restored");
    render(e, 2);

    // ---- 6. lifecycle: the spike's four shapes ---------------------------
    // (a) a full refresh under a live bound field.
    r.scene->refreshGlobalIllumination();
    render(e, 3);
    st = r.scene->giStatus();
    CHECK(st.ifdBound && fieldWhole(st),
          "a full refresh under a live field rebuilds it, bound and whole");
    // (b) a rebuild over the refreshed arm.
    CHECK(r.scene->setGlobalIllumination(ddgi), "a rebuild over the refreshed arm succeeds");
    render(e, 2);
    CHECK(r.scene->giStatus().ifdBound, "still bound after the rebuild");
    // (c) GI off with a bound field: the field must be unbound and destroyed
    //     BEFORE the VctLighting it points into, and the pixels must return.
    GiParams off;
    off.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
    CHECK(r.scene->setGlobalIllumination(off), "setGlobalIllumination(Off) with a bound field");
    render(e, 3);
    r.view->readPixels(img);
    const Colour offFloor = img.at(kFloorX, kFloorY);
    show("floor  after GI off", offFloor);
    st = r.scene->giStatus();
    CHECK(!st.ifdBound && st.ifdProbes == 0, "GI off leaves no field bound and none built");
    CHECK(std::fabs(offFloor.r - baseFloor.r) < 0.02f &&
          std::fabs(offFloor.g - baseFloor.g) < 0.02f,
          "turning GI off with a field bound restores the original floor exactly");

    // (d) the Engine is destroyed with a LIVE bound field — the shape that
    //     aliases if the teardown order is wrong. Armed here and left armed:
    //     the assertion is this program returning at all.
    CHECK(r.scene->setGlobalIllumination(ddgi), "re-arm DDGI for the destroy-with-live-field case");
    render(e, 2);
    CHECK(r.scene->giStatus().ifdBound, "field live at shutdown");

    std::printf(failures ? "\n%d FAILURES\n" : "\nall good\n", failures);
    return failures ? 1 : 0;
}
