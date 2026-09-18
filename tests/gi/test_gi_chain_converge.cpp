// gi.chain_converge — THE CHAIN'S AT-REST INJECTION LANDS ON ITS FIXED POINT
// (PHOTON_SPEC §7, lane LAMPREST-2; the engine-level twin of
// scripting.e2e.movable_lamp_rest).
//
// WHAT IS BEING PINNED, and why it is not obvious. Re-injecting a cascade
// chain's lights is not a function of the lights alone: each cascade's
// injection reads the cascades outside it AND the volume it is injecting into,
// so ONE pass is one Jacobi iteration from whatever the volumes happened to
// hold. A lamp that came to rest after travelling has a different history from
// the same lamp jumped to the same place, and a tick that stops short of the
// fixed point leaves that history in the picture — measured at 4/255 in this
// room with the two passes the engine used to spend, and at 0/255 with three.
// (Three, four and six passes all produce the same picture, which is what says
// three is the fixed point rather than a lucky number: spikes/lamprest-2.)
//
// So the observable is a HISTORY test: the same lamp, the same pose, reached two
// ways, must render the same picture. `JAHSHAKA_GI_SWEEPS` — the diagnostic the
// engine reads per tick — lets this suite prove the count is doing the work
// rather than asserting a constant: at ONE pass the two histories must visibly
// disagree, at the shipped count they must not.
//
// THE FIELD IS DELIBERATELY OFF for the history cases. The irradiance field is
// an INTEGRAL of the chain and amplifies whatever the chain held at the instant
// it integrated; its own re-integration has a defect of its own (LAMPREST-2's
// FINDINGS), and a suite about the chain must not be hostage to it. Case 4
// covers the field's own rule — that it re-converges when the chain's radiance
// changed — on the terms that are stable.
//
// Its own binary like every GI suite: the voxel lighting binds process-wide to
// HlmsPbs, so this scene must not share a process with another arm's.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void addSlab(Scene *s, const Colour &c, const Vec3 &pos, const Vec3 &size)
{
    const NodeId n = enginetest::addTestCube(s, c, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, size);
}

/// The worst per-channel difference between two pictures, in 0..255 units — the
/// same reading scripting.e2e.movable_lamp_rest takes through its probes.
static float worstDiff(const Image &a, const Image &b)
{
    float worst = 0.0f;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            worst = std::max(worst, std::fabs(ca.r - cb.r) * 255.0f);
            worst = std::max(worst, std::fabs(ca.g - cb.g) * 255.0f);
            worst = std::max(worst, std::fabs(ca.b - cb.b) * 255.0f);
        }
    return worst;
}

static float meanOf(const Image &img)
{
    double sum = 0.0;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour c = img.at(x, y);
            sum += (c.r + c.g + c.b) / 3.0;
        }
    return float(sum / double(kSize * kSize) * 255.0);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-chain-converge-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("converge", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("converge");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    // NO AMBIENT: every photon in this room has bounced off its walls, which is
    // what makes the chain's iteration measurable at all.
    scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // A SEALED WHITE ROOM, 12 m across — the shape the scripting suite uses,
    // because a closed box is where a chain's radiance really is a fixed point
    // over coupled volumes (an open scene's light leaves and never comes back).
    const Colour white(0.8f, 0.8f, 0.8f);
    addSlab(scene, white, Vec3(0.0f, -0.25f, 0.0f), Vec3(12.0f, 0.5f, 12.0f));
    addSlab(scene, white, Vec3(0.0f,  6.25f, 0.0f), Vec3(12.0f, 0.5f, 12.0f));
    addSlab(scene, white, Vec3(-6.0f, 3.0f, 0.0f), Vec3(0.5f, 6.0f, 12.0f));
    addSlab(scene, white, Vec3( 6.0f, 3.0f, 0.0f), Vec3(0.5f, 6.0f, 12.0f));
    addSlab(scene, white, Vec3(0.0f, 3.0f, -6.0f), Vec3(12.0f, 6.0f, 0.5f));
    addSlab(scene, white, Vec3(0.0f, 3.0f,  6.0f), Vec3(12.0f, 6.0f, 0.5f));

    // A BAFFLE ACROSS THE ROOM at shoulder height. The lamp lives ABOVE it and
    // the camera looks UNDER it, so every photon in the shot has bounced at
    // least once off the ceiling or the upper walls — which is the only way a
    // difference in the chain's BOUNCE term is legible at all (with the lamp in
    // open sight the direct term dominates the picture and the two histories
    // agree to 0/255 whatever the injection did, measured).
    addSlab(scene, white, Vec3(0.0f, 3.4f, -1.0f), Vec3(11.4f, 0.3f, 9.0f));

    const NodeId lamp = scene->createNode();
    const auto lampAt = [&](float x) {
        scene->setNodeTransform(lamp, Vec3(x, 4.5f, 0.0f), Quat(), Vec3(1, 1, 1));
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 0.5f;
        l.range = 24.0f;
        l.castShadows = true;
        scene->setLight(lamp, l);
    };
    lampAt(-3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 5.2f), Vec3(0.0f, 0.8f, -5.0f));

    const auto shot = [&](Image &img) { render(e, 2); view->readPixels(img); };

    // ---- the arm ---------------------------------------------------------
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 4;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    gi.cascades = true;
    CHECK(scene->setGlobalIllumination(gi), "the cascade chain builds");
    render(e, 6);
    GiStatus st = scene->giStatus();
    std::printf("   chain: %zu cascades, vctBound %d\n", st.cascades.size(), int(st.vctBound));
    CHECK(st.cascades.size() >= 2, "it is a chain (2+ cascades)");
    CHECK(st.vctBound, "...and the shader is sampling it");
    if (st.cascades.size() < 2 || !st.vctBound) { std::printf("FAILED: no chain\n"); return 1; }

    // THE TWO HISTORIES. `jumped` is the lamp put at its pose and re-injected
    // once at rest — what a re-solve leaves. `travelled` is the same pose
    // reached in steps, each step injected with the MOVING pass the engine
    // spends while something is moving (one pass, no bounces, coarse march),
    // and then ONE at-rest tick: the mirror's O2 cadence, exactly.
    const auto jumped = [&](float x, Image &img) {
        lampAt(x);
        scene->refreshGiLighting(false);
        shot(img);
    };
    const auto travelled = [&](float from, float to, Image &img) {
        lampAt(from);
        scene->refreshGiLighting(false);
        render(e, 2);
        for (int i = 1; i <= 6; ++i) {
            lampAt(from + (to - from) * float(i) / 6.0f);
            scene->refreshGiLighting(true);      // the moving pass
            render(e, 1);
        }
        scene->refreshGiLighting(false);         // the one at-rest tick it owes
        shot(img);
    };

    // ---- 1. THE COUNT THE TICK SPENDS ------------------------------------
    // The number itself, reported by the renderer (GiStatus::chainSweeps), so a
    // lane that quietly puts the at-rest tick back to one pass reds HERE as well
    // as in scripting.e2e.movable_lamp_rest — which is the suite that measures
    // the picture the count is FOR (that room reds at two passes and is green at
    // three, 8/8, measured). This one measures the rule.
    lampAt(-3.0f);
    scene->refreshGiLighting(true);
    const int movingSweeps = scene->giStatus().chainSweeps;
    scene->refreshGiLighting(false);
    const int restSweeps = scene->giStatus().chainSweeps;
    std::printf("   passes: moving %d, at rest %d\n", movingSweeps, restSweeps);
    CHECK(movingSweeps == 1,
          "a tick taken WHILE something is moving spends exactly one injection pass");
    CHECK(restSweeps >= 3,
          "AN AT-REST TICK ITERATES TO THE CHAIN'S FIXED POINT (three passes or "
          "more; two leaves the history it started from in the picture)");
    // ...and the diagnostic that re-measures it is wired to the same place.
    ::setenv("JAHSHAKA_GI_SWEEPS", "5", 1);
    scene->refreshGiLighting(false);
    const int forced = scene->giStatus().chainSweeps;
    ::unsetenv("JAHSHAKA_GI_SWEEPS");
    CHECK(forced == 5, "JAHSHAKA_GI_SWEEPS re-measures the count (the lane's instrument)");

    // ---- 2. THE SHIPPED COUNT REACHES THE FIXED POINT --------------------
    Image jump, travel;
    jumped(3.0f, jump);
    travelled(-3.0f, 3.0f, travel);
    const float delta = worstDiff(jump, travel);
    std::printf("   SHIPPED  : jumped mean %.2f, travelled mean %.2f, worst %.2f/255\n",
                meanOf(jump), meanOf(travel), delta);
    CHECK(delta <= 1.5f,
          "A LAMP THAT TRAVELLED AND CAME TO REST RENDERS WHAT THE SAME LAMP "
          "JUMPED THERE RENDERS (the at-rest injection is at its fixed point)");
    // NOTE, and it is the honest reading: in THIS room the two histories agree
    // even at one pass (measured 0.00/255) — a sealed box lit by a lamp in open
    // sight is dominated by its direct term, and how many passes a scene needs
    // is a property of the scene. The room that needs three is the movable-lamp
    // room of scripting.e2e.movable_lamp_rest, which is where the count is
    // measured; what this case pins is that the property HOLDS at the shipped
    // count, on a chain, through the engine's own re-injection path.

    // ---- 3. AND IT IS A FIXED POINT: another tick moves nothing -----------
    Image again;
    scene->refreshGiLighting(false);
    shot(again);
    const float idem = worstDiff(jump, again);
    std::printf("   another at-rest tick moves the picture by %.2f/255\n", idem);
    CHECK(idem <= 1.5f, "a second at-rest tick over an unchanged scene changes nothing");

    // ---- 4. THE SINGLE VOLUME IS NOT AN ITERATION AND STAYS BIT-EXACT ----
    GiParams single = gi;
    single.cascades = false;
    CHECK(scene->setGlobalIllumination(single), "the single-volume arm builds");
    render(e, 6);
    st = scene->giStatus();
    CHECK(st.cascades.empty(), "...with no chain at all");
    Image s1, s2;
    scene->refreshGiLighting(false);
    shot(s1);
    scene->refreshGiLighting(false);
    shot(s2);
    const float singleDelta = worstDiff(s1, s2);
    std::printf("   single volume: two at-rest re-injections differ by %.2f/255\n", singleDelta);
    CHECK(scene->giStatus().chainSweeps == 1,
          "...and it reports ONE pass, because it is not an iteration");
    CHECK(singleDelta == 0.0f,
          "THE SINGLE VOLUME IS BIT-EXACT across re-injections (one injection "
          "overwrites its light voxels and there is nothing outside it to read)");

    // ---- 5. THE FIELD RE-CONVERGES WHEN THE CHAIN'S RADIANCE CHANGED -----
    // The field is an integral of the chain: a light that moved must reach it,
    // and a tick that changed nothing must not leave it half-integrated. The
    // budget is 0 so the field converges INLINE and "converged" is a state this
    // case can read rather than wait for.
    GiParams withField = gi;
    withField.ddgi = GiToggle::On;
    CHECK(scene->setGlobalIllumination(withField), "the chain builds again with the field on");
    render(e, 6);
    st = scene->giStatus();
    std::printf("   field: bound %d, probes %d, converged %d\n",
                int(st.ifdBound), st.ifdProbes, int(st.ifdConverged));
    CHECK(st.ifdBound && st.ifdProbes > 0, "the irradiance field is bound");
    CHECK(st.ifdConverged, "...and converged on the frame it bound");
    Image fieldA, fieldB;
    jumped(-3.0f, fieldA);
    CHECK(scene->giStatus().ifdConverged, "the field is converged after an at-rest injection");
    jumped(3.0f, fieldB);
    const float moved = worstDiff(fieldA, fieldB);
    std::printf("   the lamp moved 6 m: the picture moved %.2f/255\n", moved);
    CHECK(moved > 4.0f, "A LIGHT THAT MOVED REACHES THE FIELD (it re-integrated)");
    CHECK(scene->giStatus().ifdConverged, "...and the field is converged again");

    // ---- 6. A CAMERA WALK THAT COMES BACK LEAVES THE CHAIN WHERE IT WAS ---
    // (LAMPREST-3.) A cascade rebuild ends with ONE injection of THAT cascade
    // over the radiance it held where it used to stand — one Jacobi pass of a
    // fixed point over coupled volumes — and a camera walk moves no light and
    // no geometry, so the mirror's cadence never ticks and nothing finished the
    // iteration. Measured before the fix, on the movable-lamp room: the camera
    // walked 35 m away and back to the SAME pose (the placements returned
    // exactly, the albedo voxels came back byte-identical) and cascade 0's
    // light came back 60,852 of its 82,176 lit bytes different, by up to
    // 55/255, for ever; one at-rest injection put every byte back.
    //
    // So the observable is the same history test the cases above take, with the
    // CAMERA as the history instead of the lamp: the picture from one pose must
    // not depend on where the camera has been. The scheduler pays the settle on
    // the first frame it owes no rebuild, which is why the counter must move
    // ONCE for the walk and then stand still while nothing happens.
    CHECK(scene->setGlobalIllumination(gi), "the chain builds again for the walk (field off)");
    render(e, 10);
    lampAt(-2.0f);
    scene->refreshGiLighting(false);
    Image beforeWalk;
    shot(beforeWalk);
    const long long settlesBefore = scene->giStatus().chainSettles;

    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 60.0f), Vec3(0.0f, 0.8f, 50.0f));
    render(e, 40);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 5.2f), Vec3(0.0f, 0.8f, -5.0f));
    render(e, 40);
    Image afterWalk;
    shot(afterWalk);
    const GiStatus walked = scene->giStatus();
    const float walkDelta = worstDiff(beforeWalk, afterWalk);
    std::printf("   walked away and back: %.2f/255, cascade 0 rebuilds %llu, settles %lld -> %lld\n",
                walkDelta, (unsigned long long)walked.cascades[0].rebuilds,
                settlesBefore, walked.chainSettles);
    CHECK(walked.cascades[0].rebuilds >= 2,
          "the walk really did re-voxelise cascade 0 (away, and back again)");
    CHECK(walked.chainSettles > settlesBefore,
          "A REBUILT CHAIN IS RUN TO ITS FIXED POINT — the scheduler paid the "
          "at-rest injection the rebuild owed, one cheap step per frame");
    CHECK(walkDelta <= 1.5f,
          "AND THE PICTURE FROM ONE POSE DOES NOT DEPEND ON WHERE THE CAMERA "
          "HAS BEEN (it was 5/255 and permanent before the settle existed)");
    // ...ONCE PER BURST, NOT ONCE PER FRAME: the cost of the rule is one at-rest
    // tick per camera step, and a still scene must pay nothing at all.
    const long long settledAt = walked.chainSettles;
    render(e, 30);
    CHECK(scene->giStatus().chainSettles == settledAt,
          "...and a still camera pays no further settle (the flag is spent, not standing)");

    // AND THE DEFECT ITSELF, so this case proves what it guards rather than
    // asserting a number that happens to pass: with the rule stood down the SAME
    // walk must visibly move the picture (measured 13.00/255 here).
    ::setenv("JAHSHAKA_GI_NO_REBUILD_SETTLE", "1", 1);
    Image beforeBare;
    scene->refreshGiLighting(false);
    shot(beforeBare);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 60.0f), Vec3(0.0f, 0.8f, 50.0f));
    render(e, 40);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 5.2f), Vec3(0.0f, 0.8f, -5.0f));
    render(e, 40);
    Image afterBare;
    shot(afterBare);
    const float bareDelta = worstDiff(beforeBare, afterBare);
    const long long bareSettles = scene->giStatus().chainSettles;
    ::unsetenv("JAHSHAKA_GI_NO_REBUILD_SETTLE");
    std::printf("   with the settle stood down: %.2f/255 (settles %lld -> %lld)\n",
                bareDelta, settledAt, bareSettles);
    CHECK(bareSettles == settledAt, "the diagnostic really does stand the settle down");
    CHECK(bareDelta > 2.0f,
          "...and WITHOUT it the same walk leaves the chain somewhere else — which "
          "is the defect this case exists for");
    // ...and the scene is put back where the cases after this one expect it.
    // (The stand-down left a debt armed — the scheduler raised it and the knob
    // refused to pay it — and this is what spends it: whoever pays the
    // injection pays the debt.)
    scene->refreshGiLighting(false);

    // ---- 7. A CAMERA THAT NEVER STANDS STILL STILL GETS THE SETTLE --------
    // (LAMPREST-3 fix round item 1, and it is the VR case.) In a headset the GI
    // driver is the session's view and its camera is the TRACKED HEAD, written
    // every frame: a rule that waited for two frames at one position would
    // never fire for a wearer, and the defect above would simply live in the
    // headset until a light or an object moved. So the settle has NO
    // camera-still gate — it rides the scheduler's own idle slot — and this
    // case is the proof: the camera jitters a millimetre every frame, for ever.
    Image jitBefore, jitAfter;
    scene->refreshGiLighting(false);
    shot(jitBefore);
    const long long jitSettles = scene->giStatus().chainSettles;
    const auto jitterAt = [&](float z, int frames) {
        for (int f = 0; f < frames; ++f) {
            enginetest::testCameraLookAt(view, Vec3(f % 2 ? 0.001f : 0.0f, 1.6f, z),
                                         Vec3(0.0f, 0.8f, z - 10.0f));
            render(e, 1);
        }
    };
    jitterAt(60.0f, 40);
    jitterAt(5.2f, 60);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 5.2f), Vec3(0.0f, 0.8f, -5.0f));
    render(e, 6);
    shot(jitAfter);
    const GiStatus jit = scene->giStatus();
    std::printf("   a camera jittering 1 mm a frame: %.2f/255 (settles %lld -> %lld)\n",
                worstDiff(jitBefore, jitAfter), jitSettles, jit.chainSettles);
    CHECK(jit.chainSettles > jitSettles,
          "A JITTERING CAMERA IS PAID TOO — the settle has no still gate, because "
          "a headset's head pose never holds one position (VR would never settle)");
    CHECK(worstDiff(jitBefore, jitAfter) <= 1.5f,
          "...and the picture comes back to where it was");

    // ---- 8. THE FIELD IS RE-INTEGRATED AFTER THE SETTLE, NOT BEFORE ------
    // (fix round item 6.) The field is an integral of the chain and it
    // integrates at the REBUILD frame (followCascade0Field) over a chain that
    // has not converged yet — so the settle's last step must re-integrate it,
    // and a converged field is the observable.
    GiParams walkField = gi;
    walkField.ddgi = GiToggle::On;
    CHECK(scene->setGlobalIllumination(walkField), "the chain builds with the field on for the walk");
    render(e, 8);
    scene->refreshGiLighting(false);
    Image fieldBefore, fieldAfter;
    shot(fieldBefore);
    const long long fieldSettles = scene->giStatus().chainSettles;
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 60.0f), Vec3(0.0f, 0.8f, 50.0f));
    render(e, 40);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 5.2f), Vec3(0.0f, 0.8f, -5.0f));
    render(e, 60);
    shot(fieldAfter);
    const GiStatus fieldSt = scene->giStatus();
    std::printf("   with the field on: %.2f/255, converged %d (settles %lld -> %lld)\n",
                worstDiff(fieldBefore, fieldAfter), int(fieldSt.ifdConverged),
                fieldSettles, fieldSt.chainSettles);
    CHECK(fieldSt.chainSettles > fieldSettles, "the walk owed a settle with the field on too");
    CHECK(fieldSt.ifdConverged,
          "THE FIELD IS CONVERGED AFTER THE SETTLE — its last step re-integrates "
          "the field over a chain that has finished moving");
    CHECK(worstDiff(fieldBefore, fieldAfter) <= 1.5f,
          "...and the picture with the field on comes back to where it was");

    GiParams off; off.mode = GiMode::Off;
    CHECK(scene->setGlobalIllumination(off), "GI comes down");
    render(e, 2);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
