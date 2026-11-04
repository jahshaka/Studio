// engine.chain_gi_routes - THE CHAIN'S DIFFUSE-GI ROUTE as a pure function (ROUTE-ROWS-1 stage 0;
// irisgl/engine/src/GiChainRules.h). No device, no Ogre. THE LAW: ONE WORLD, ONE DIFFUSE PRODUCER PER VIEW (CODING_RULES §1,
// physics is the rule of the render engine) - over the whole truth table of the view's kind (a window, the headless
// host's presented stand-in, a mono offscreen picture of either old contract, a stereo pair) x the scene's GI on/off x
// the scene traces or not x the integrate's measuring arm:
//   (a) the gather runs exactly where the scene's GI is on and it traces - whatever the view's kind;
//   (b) the screen integrate runs exactly where the GI is on, the scene does not trace, the view is mono and the arm
//       is off - never on a ray scene;
//   (c) never both; GI off = neither; a stereo view of a scene that does not trace = neither (it keeps its cones);
//   (d) every mono kind gets the SAME answer (the kind is no input: the deleted Live/StillPicture contract made a live
//       offscreen picture of a ray scene take the integrate while the window gathered - two producers of one world).
// NEGATIVE CONTROL: the deleted rule (gather = wanted AND (presents OR a still picture)) differs from this one on a mono
// Live offscreen view of a ray scene - the check can fail.
#include <cstdio>
#include "GiChainRules.h"

using namespace jahshaka::engine::detail;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

namespace {
// the view's kinds the old rule told apart; the new rule reads only `stereo` of them
struct Kind { const char *name; bool presents; bool stillPicture; bool stereo; };
const Kind kKinds[] = {
    { "window", true, false, false },
    { "presented stand-in", true, false, false },
    { "offscreen still picture", false, true, false },
    { "offscreen live view", false, false, false },
    { "stereo pair", true, false, true },
};
// THE DELETED RULE (OgreView.cpp before ROUTE-ROWS-1): contractAllows = presents || StillPicture
ChainGiRoutes oldRule(const Kind &k, bool gatherWanted, bool giOn, bool armOff)
{
    ChainGiRoutes r;
    r.probeGather = gatherWanted && (k.presents || k.stillPicture);
    r.screenIntegrate = !r.probeGather && !k.stereo && giOn && !armOff;
    return r;
}
}   // namespace

int main()
{
    int combos = 0, wrongGather = 0, wrongIntegrate = 0, both = 0, kindDiffers = 0;
    for (int gi = 0; gi < 2; ++gi)
        for (int traces = 0; traces < 2; ++traces)
            for (int arm = 0; arm < 2; ++arm) {
                const bool giOn = gi != 0, sceneTraces = traces != 0, armOff = arm != 0;
                const bool gatherWanted = giOn && sceneTraces;   // OgreScene::probeGatherWanted
                const ChainGiRoutes mono = chainGiRoutes(gatherWanted, giOn, false, armOff);
                for (const Kind &k : kKinds) {
                    ++combos;
                    const ChainGiRoutes r = chainGiRoutes(gatherWanted, giOn, k.stereo, armOff);
                    if (r.probeGather != (giOn && sceneTraces)) ++wrongGather;                               // (a)
                    if (r.screenIntegrate != (giOn && !sceneTraces && !k.stereo && !armOff)) ++wrongIntegrate;  // (b)
                    if (r.probeGather && r.screenIntegrate) ++both;                                           // (c)
                    if (!k.stereo && (r.probeGather != mono.probeGather || r.screenIntegrate != mono.screenIntegrate))
                        ++kindDiffers;                                                                        // (d)
                }
            }
    std::printf("the GI route over %d combinations (5 kinds x GI x traces x arm): gather wrong %d, integrate wrong %d, "
                "both %d, a mono kind differing %d\n", combos, wrongGather, wrongIntegrate, both, kindDiffers);
    check(combos == 40 && wrongGather == 0,
          "(a) the gather runs exactly where the GI is on and the scene traces, in every kind of view");
    check(wrongIntegrate == 0,
          "(b) the screen integrate runs exactly where the GI is on, the scene does not trace, the view is mono and the "
          "arm is off - never on a ray scene");
    check(both == 0, "(c) one producer: never the gather and the integrate on one chain");
    {
        const ChainGiRoutes off = chainGiRoutes(false, false, false, false);
        const ChainGiRoutes stereoNoRays = chainGiRoutes(false, true, true, false);
        check(!off.probeGather && !off.screenIntegrate && !stereoNoRays.probeGather && !stereoNoRays.screenIntegrate,
              "(c) GI off: neither; a stereo view of a scene that does not trace: neither (it keeps the pixel's cones)");
    }
    check(kindDiffers == 0, "(d) every mono kind - window, stand-in, still picture, live offscreen view - gets one answer");
    // NEGATIVE CONTROL: the deleted rule gave a live offscreen picture of a ray scene the integrate (a second producer)
    {
        const Kind &live = kKinds[3];
        const ChainGiRoutes was = oldRule(live, true, true, false);
        const ChainGiRoutes now = chainGiRoutes(true, true, live.stereo, false);
        check(was.screenIntegrate && !was.probeGather && now.probeGather && !now.screenIntegrate,
              "negative control: the deleted Live contract's rule took the integrate on a live offscreen view of a ray "
              "scene; the rule gathers there");
    }
    std::printf("%s\n", gFail ? "engine.chain_gi_routes: FAILED" : "engine.chain_gi_routes: PASSED");
    return gFail ? 1 : 0;
}
