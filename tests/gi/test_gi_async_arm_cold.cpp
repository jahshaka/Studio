// gi.async_arm_cold — THE LIGHTING ARM OF AN ASYNCHRONOUS SCENE NEVER COMPILES IN A FRAME (SHADER-SYSTEM-1 7a, fixes B,
// C and D). A scene that compiles in the background runs its lighting arm's BUILD under the fork's deferred dispatch
// (PhotonComponent::giDeferrable); its PER-FRAME record - the feedback set (runFeedback: the bounce injection, the
// anisotropic mips, the cards' indirect accumulation) and the field's generation (recordField) - ran outside it, so on an
// empty cache each of their permutations was parsed and its pipeline built inside a frame (the startup gate's frames:
// spikes/shader-system/ss7/gate-measure, VCT/LightVctBounceInject x9-10, the AnisotropicMipStep jobs, Photon/BrickCopy,
// IrradianceField/Gen, Jahshaka/FieldAccumulate). And a permutation whose CODE was already built under another job (a
// clone, a record) had its pipeline built in the frame even under the deferral (fork HlmsCompute::dispatch's "known"
// branch, fix D).
//
// TWO ARMS, ONE ROW EACH: `cold` (gi.async_arm_cold) - NO persistent shader cache, every compute permutation cold in
// this process; `warm` (gi.async_arm_warm) - the rig's shared warm cache, every compute record a hit: the arm's codes are
// KNOWN, so before fix D each deferred dispatch of one built its pipeline in the frame (law 2's pipeline count), and with
// it the pipeline is requested on the background compiler like any other permutation (law 1's deferrals then > 0).
// THE SCRIPT: one engine on the arm's cache, an offscreen
// Live view in asynchronous mode, every frame on the Streaming pace; one scene - a floor, a box, a shadowing sun - with
// the voxel GI on (High, bounces on). Frames until the arm is bound, the background compiler holds nothing and no frame
// owes a deferred dispatch, or the cap.
// THE LAWS:
//  (1) the deferred path ran: the fork's deferred dispatches (AsyncShaderStats::computeDeferred) > 0 (the cold arm, F7:
//      else the row proves nothing);
//  (2) NOTHING COMPILED IN A FRAME: 0 compute templates parsed and 0 compute pipelines built inside any frame of the run
//      (the published computeParsedInFrame / computePsoInFrame, process totals). RED before fixes B+C (the feedback and
//      the field's dispatches parsed in frames) and before fix D (a known code's pipeline built in the frame);
//  (3) THE ARM LANDS: once the compiler holds nothing, the arm is bound (GiStatus::vctBound) and the frame reads no
//      deferred dispatch (PublishedFrame::Completeness term 64) within kSettleFrames - the deferred work was retried
//      whole (the arm rebuilt once nothing is in flight), never dropped.
//  (4) THE COUNTER's NEGATIVE CONTROL (the cold arm; it replaces shadercache.app.cold's F7, which a cold APP launch no
//      longer feeds since fixes A-C: SPECS/OPEN_REDS.md): A COMPLETE FRAME OF A SYNCHRONOUS VIEW PARSES WHAT IT LACKS, in
//      the frame - the same process, a second scene on a synchronous view at another GI quality (other cascade counts:
//      permutations the run above never built) - so the counter law (2) and shadercache.app.warm bar at 0 is shown fed
//      (> 0), never a constant.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/GiParams.h"
#include "jahshaka/engine/Monitor.h"
#include "jahshaka/engine/Published.h"
#include "jahshaka/engine/Shaders.h"
#include "../support/enginetesthelpers.h"
#include <algorithm>
#include <cstdio>
#include <string>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

/// Law 3's bound once the compiler is empty: a publish lands at the next frame's top, the arm's retry rebuilds it whole
/// in that frame's pendings (PhotonComponent::applyPendingGiDeferrable) on the Streaming pace one stage a frame - the
/// staged build's stages (GiBuildStage: the chain, the voxelisation, the probes' three, the field) - and the published
/// status is a frame late: 1 + 8 + 1 = 10 frames bound it.
static constexpr int kSettleFrames = 10;
/// The hang guard of the wait for the compiler (never the law's bar: law 3 is counted from the compiler's empty state).
static constexpr int kFrameCap = 3000;
/// Law 4's frames: a synchronous view's Complete frames build the arm whole, one GiBuildStage a frame (the law-3 count).
static constexpr int kControlFrames = 10;

int main(int argc, char **argv)
{
    const bool warm = argc > 1 && std::string(argv[1]) == "warm";
    const char *row = warm ? "gi.async_arm_warm" : "gi.async_arm_cold";
    std::printf("== %s: the lighting arm of an asynchronous scene never compiles in a frame\n", row);
    EngineConfig cfg = warm ? enginetest::testEngineConfig("test-gi-async-arm-warm-ogre.log")
                            : enginetest::testEngineConfig("test-gi-async-arm-cold-ogre.log",
                                                           enginetest::EngineOverrides().shaderCacheOff());
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    View *view = e->createOffscreenView("arm", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("arm");
    if (!view || !scene) { std::printf("FAIL: view/scene (%s)\n", e->lastError().c_str()); return 1; }
    view->setAsyncShaders(true);
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 5.0f), Vec3(0.0f, 0.5f, 0.0f));
    const NodeId floor = enginetest::addTestCube(scene, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
    const NodeId box = enginetest::addTestCube(scene, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.6f);
    bool ok = floor && box;
    if (ok) {
        enginetest::setNodePosition(scene, floor, Vec3(0.0f, -0.1f, 0.0f));
        enginetest::setNodeScale(scene, floor, Vec3(8.0f, 0.2f, 8.0f));
        enginetest::setNodePosition(scene, box, Vec3(0.0f, 0.5f, 0.0f));
    }
    ok = ok && enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, 0.2f), 2.0f) != 0;
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    ok = ok && scene->setGlobalIllumination(gi);
    CHECK(ok, "the scene built: a floor, a box, a sun, the voxel GI (%s)", e->lastError().c_str());
    if (!ok) return 1;
    // THE DECLARED ROUTE (CODING_RULES § 6): a Live offscreen view runs the screen integrate; the scene's cards and store
    enginetest::declareRoutes(view, scene, enginetest::Routes().integrate().cards().store(),
                              (std::string(row) + "'s view").c_str());

    const auto frame = [&]() {
        e->setFramePace(FramePace::Streaming);   // consumed by the next frame (the engine resets it to Complete after it)
        enginetest::frames(e, 1);
    };
    int f = 0, emptyFrames = 0, termFrames = 0;
    bool settled = false;
    for (; f < kFrameCap; ++f) {
        frame();
        const AsyncShaderStats as = e->published().shaders;
        const unsigned outstanding = as.pending - std::min(as.pending, as.rayBuildsInFlight);
        const PublishedFrame &pub = scene->published();
        const bool term = (pub.completeness.terms & 64u) != 0u;
        termFrames += term ? 1 : 0;
        if (outstanding == 0u && pub.gi.vctBound && !term) { settled = true; break; }
        emptyFrames = outstanding == 0u ? emptyFrames + 1 : 0;
        if (emptyFrames > kSettleFrames) break;   // law 3 has failed: nothing on the compiler, the arm still owed
    }
    const AsyncShaderStats as = e->published().shaders;
    std::printf("    %d frames: settled %d, %llu dispatch(es) deferred, %d frame(s) read a deferred dispatch, "
                "frames with the compiler empty and the arm owed %d\n",
                f + 1, settled ? 1 : 0, as.computeDeferred, termFrames, emptyFrames);
    CHECK(as.computeDeferred > 0ull, "(1) the deferred path ran: %llu compute dispatch(es) deferred (the cold "
                                     "permutations were on the background compiler)", as.computeDeferred);
    CHECK(as.computeParsedInFrame == 0ull && as.computePsoInFrame == 0ull,
          "(2) nothing compiled in a frame: %llu compute template(s) parsed and %llu compute pipeline(s) built inside "
          "a frame (bar 0 each)", as.computeParsedInFrame, as.computePsoInFrame);
    CHECK(settled, "(3) the arm landed: bound and no deferred dispatch owed within %d frames of the compiler's queue "
                   "emptying (%d frames empty, %d frames in all)", kSettleFrames, emptyFrames, f + 1);
    if (!warm) {
        View *sv = e->createOffscreenView("arm-sync", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
        Scene *ss = e->createScene("arm-sync");
        bool built = sv && ss;
        if (built) {
            sv->setScene(ss);   // synchronous: a fixture's view compiles in its frames unless it asks
            enginetest::testCameraLookAt(sv, Vec3(0.0f, 2.0f, 5.0f), Vec3(0.0f, 0.5f, 0.0f));
            const NodeId sfloor = enginetest::addTestCube(ss, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
            built = sfloor && enginetest::addDirectionalLight(ss, Vec3(0.3f, -1.0f, 0.2f), 2.0f) != 0;
            GiParams lo = gi;
            lo.quality = GiQuality::Low;
            built = built && ss->setGlobalIllumination(lo);
            enginetest::declareRoutes(sv, ss, enginetest::Routes().integrate().cards().store(),
                                      "gi.async_arm_cold's control view");
        }
        const AsyncShaderStats c0 = e->published().shaders;
        for (int i = 0; built && i < kControlFrames; ++i) enginetest::frames(e, 1);   // the default pace: Complete
        const AsyncShaderStats c1 = e->published().shaders;
        const unsigned long long parsed = c1.computeParsedInFrame - c0.computeParsedInFrame;
        std::printf("    the control: a synchronous view at Low quality, %d Complete frames: %llu compute template(s) "
                    "parsed and %llu pipeline(s) built in its frames\n", kControlFrames, parsed,
                    c1.computePsoInFrame - c0.computePsoInFrame);
        CHECK(built && parsed > 0ull, "(4) CONTROL: a synchronous view's Complete frames parsed %llu compute template(s) "
                                      "in the frame (above 0: law 2's counter is fed) (%s)", parsed,
              e->lastError().c_str());
        if (ss) enginetest::poseRegistry().erase(ss);
    }
    enginetest::poseRegistry().erase(scene);
    if (failures) std::printf("%s: FAIL (%d)\n", row, failures);
    else std::printf("%s: PASS\n", row);
    return failures ? 1 : 0;
}
