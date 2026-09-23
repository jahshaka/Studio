// gi.chain_converge — ONE AT-REST SWEEP IS THE CHAIN'S FIXED POINT, BYTE FOR
// BYTE (PHOTON_SPEC §7; lanes LAMPREST-2, FENCE-1, PHOTON-WRITER-1 SWEEPS-3; the
// engine-level twin of scripting.e2e.movable_lamp_rest).
//
// THE CLAIM, DERIVED (EnginePrivate.h, beside kAtRestSweeps). A chain's radiance
// is the fixed point of L_i = D_i + rho * G_i(L_i, L_{i+1..N}): each cascade's
// bounce reads its OWN volume and the ones OUTSIDE it, never the ones inside
// (TRIANGULAR). `VctLighting::update` rebuilds a cascade's light from its direct
// term every time (the bounce passes are new = direct + rho * G(total), started
// from the direct term the same dispatch wrote), so one injection of cascade i
// is a function of its voxels, the lights, the environment and the CURRENT
// light of cascades i+1..N-1 — of nothing it held before. Swept OUTERMOST FIRST,
// every cascade reads outer cascades that are already final: ONE sweep is the
// fixed point, and a second sweep can change no byte unless a hidden INWARD
// coupling exists.
//
// THE PROOF IS A MEASUREMENT OF BYTES, not of a picture: from a perturbed
// history (a lamp walked with the moving tick), one at-rest tick, then a
// second — every cascade's light volumes (the total and the three anisotropic
// axes) hashed raw (GiVoxelStats::lightDigest) after each. They must be EQUAL,
// per cascade, at Medium and at High. And the history test in bytes: a lamp
// that travelled and came to rest leaves the SAME bytes as the same lamp jumped
// there. (The default scene and Showroom 2 take the same proof through the
// verbs: gi.chain_converge_scenes.)
//
// THE FIELD IS DELIBERATELY OFF for the history cases (an integral of the chain,
// with its own re-convergence rule — case 5 covers it).
//
// Its own binary like every GI suite: the voxel lighting binds process-wide to
// HlmsPbs, so this scene must not share a process with another arm's.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    std::printf("== gi.chain_converge: one at-rest sweep is the chain's fixed point, byte for byte\n");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-chain-converge-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // ---- 0. A BOOT PAYS NO ENVIRONMENT SETTLE (PHOTON-ENV-1 audit F7) -----
    // The bounce injection reads the environment where its cones escape, so a
    // chain built BEFORE its scene's sky has landed is built over no sky and is
    // owed a settle the frame the cube arrives (noteEnvironmentChanged) — on
    // EVERY boot of a sky + bounce + chain scene. The first build therefore
    // waits for the sky's capture and convolution (OgreScene::
    // giEnvironmentPending, the same shape as the albedo wait), and this case
    // is the boot the way the mirror drives it: the sky and the arm pushed
    // before the first frame, the sky's own SH pushed as the ambient on every
    // frame it is valid. Measured before the wait: 1 settle per boot; after: 0.
    // Its own scene, torn down before the room below is built.
    {
        View *bv = e->createOffscreenView("boot", kSize, kSize, Colour(0, 0, 0));
        Scene *bs = e->createScene("boot");
        bv->setScene(bs);
        addSlab(bs, Colour(0.8f, 0.8f, 0.8f), Vec3(0.0f, -0.25f, 0.0f), Vec3(12.0f, 0.5f, 12.0f));
        addSlab(bs, Colour(0.8f, 0.8f, 0.8f), Vec3(0.0f, 1.0f, -3.0f), Vec3(4.0f, 2.0f, 0.5f));
        const unsigned char skyPx[4] = { 150, 170, 200, 255 };
        SkyDesc sky;
        sky.mode = SkyMode::Equirectangular;
        sky.equirect = bs->createTexture(1, 1, skyPx, true);
        CHECK(sky.equirect && bs->setSky(sky), "boot: the sky binds");
        bs->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f, 1.0f));
        enginetest::testCameraLookAt(bv, Vec3(0.0f, 1.6f, 5.2f), Vec3(0.0f, 0.8f, -5.0f));
        GiParams bg;
        bg.mode = GiMode::Vct;
        bg.quality = GiQuality::Medium;
        bg.numBounces = 4;
        bg.ddgi = GiToggle::Off;
        bg.updateBudget = 0;
        bg.cascades = true;
        CHECK(bs->setGlobalIllumination(bg), "boot: the arm is armed before the first frame");
        int builtAt = -1;
        for (int f = 0; f < 40; ++f) {
            float sh[27];
            if (bs->skyAmbientSh(sh)) bs->setAmbientSh(sh);     // the mirror's push
            e->renderOneFrame();
            if (builtAt < 0 && !bs->giStatus().cascades.empty()) builtAt = f;
        }
        const GiStatus bst = bs->giStatus();
        std::printf("   boot: the chain built on frame %d; settles after 40 frames: %lld\n",
                    builtAt, bst.chainSettles);
        CHECK(builtAt >= 0 && bst.cascades.size() >= 2, "boot: the chain is built");
        CHECK(bst.chainSettles == 0,
              "A BOOT PAYS NO ENVIRONMENT SETTLE: the first chain is built over the "
              "sky it will read, not before it (audit F7)");
        GiParams off; off.mode = GiMode::Off;
        bs->setGlobalIllumination(off);
        e->renderOneFrame();
        e->destroyView(bv);
        e->destroyScene(bs);
    }

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

    // THE CHAIN'S BYTES, per cascade (the one-sweep proof's instrument).
    const auto digests = [&]() {
        std::vector<std::string> d;
        const size_t n = scene->giStatus().cascades.size();
        for (size_t i = 0; i < n; ++i) d.push_back(scene->giVoxelStats(int(i)).lightDigest);
        return d;
    };
    const auto join = [](const std::vector<std::string> &d) {
        std::string s;
        for (const std::string &x : d) s += (s.empty() ? "" : " ") + x.substr(0, 8);
        return s;
    };
    // THE TWO HISTORIES. `jumped` is the lamp put at its pose and re-injected
    // once at rest — what a re-solve leaves. `travelled` is the same pose
    // reached in steps, each step injected with the MOVING tick the engine
    // spends while something is moving, and then ONE at-rest tick: the mirror's
    // O2 cadence, exactly. (Under a chain the host's tick is owed and runs at the
    // frame's writer point, so every tick below renders the frame that runs it.)
    const auto rest = [&]() { scene->refreshGiLighting(false); render(e, 1); };
    const auto jumped = [&](float x, Image &img) {
        lampAt(x);
        rest();
        shot(img);
    };
    const auto travelled = [&](float from, float to, Image &img) {
        lampAt(from);
        rest();
        render(e, 2);
        for (int i = 1; i <= 6; ++i) {
            lampAt(from + (to - from) * float(i) / 6.0f);
            scene->refreshGiLighting(true);      // the moving tick
            render(e, 1);
        }
        rest();                                  // the one at-rest tick it owes
        shot(img);
    };

    // ---- 1. THE PROOF: SWEEP 1 == SWEEP 2, BYTE FOR BYTE, PER CASCADE -----
    // From a PERTURBED history (one iteration taken from the fixed point stays
    // there on any renderer, so the proof must start somewhere else): the walk,
    // one at-rest tick, the bytes; a second at-rest tick, the bytes again.
    const auto proof = [&](const char *tier) {
        Image scratch;
        travelled(-3.0f, 3.0f, scratch);
        const std::vector<std::string> one = digests();
        rest();
        const std::vector<std::string> two = digests();
        bool readable = !one.empty() && one.size() == two.size();
        size_t differ = 0;
        for (size_t i = 0; i < one.size(); ++i) {
            if (one[i].empty() || two[i].empty()) readable = false;
            else if (one[i] != two[i]) ++differ;
        }
        std::printf("   %s: sweep 1 [%s]\n   %s: sweep 2 [%s] -- %zu of %zu cascades differ\n",
                    tier, join(one).c_str(), tier, join(two).c_str(), differ, one.size());
        CHECK(readable, (std::string(tier) + ": every cascade's light volumes read back").c_str());
        CHECK(readable && differ == 0,
              (std::string(tier) + ": ONE AT-REST SWEEP IS THE FIXED POINT -- a second sweep "
                                   "changes no byte of any cascade's light").c_str());
        return one;
    };
    const std::vector<std::string> medium = proof("Medium");

    // ---- 2. THE HISTORY TEST, IN BYTES ------------------------------------
    // A lamp that travelled and came to rest leaves the SAME bytes as the same
    // lamp jumped there (and so the same picture).
    Image jump, travel;
    travelled(-3.0f, 3.0f, travel);
    const std::vector<std::string> travelBytes = digests();
    jumped(-3.0f, jump);
    jumped(3.0f, jump);
    const std::vector<std::string> jumpBytes = digests();
    const float delta = worstDiff(jump, travel);
    std::printf("   history: jumped [%s], travelled [%s]; picture worst %.2f/255\n",
                join(jumpBytes).c_str(), join(travelBytes).c_str(), delta);
    CHECK(!jumpBytes.empty() && jumpBytes == travelBytes,
          "A LAMP THAT TRAVELLED AND CAME TO REST LEAVES THE BYTES THE SAME LAMP JUMPED THERE "
          "LEAVES (every cascade)");
    CHECK(delta == 0.0f, "...and so the same picture");

    // ---- 3. THE SAME PROOF AT HIGH -----------------------------------------
    {
        GiParams high = gi;
        high.quality = GiQuality::High;
        CHECK(scene->setGlobalIllumination(high), "the High chain builds");
        render(e, 6);
        CHECK(scene->giStatus().cascades.size() >= 2, "...and it is a chain");
        proof("High");
        CHECK(scene->setGlobalIllumination(gi), "back to the Medium chain");
        render(e, 6);
    }

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
