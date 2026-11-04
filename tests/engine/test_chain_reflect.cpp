// engine.chain_reflect_sources / engine.reflect_chain_shape — THE REFLECTION CHAIN's RULES, as pure functions
// (PHOTON-7-9A; irisgl/engine/src/ReflectChainRules.h). No device, no Ogre. One executable, one row per law (its argument):
//   sources  THE HYBRID IS THE ONE SHAPE: a mono chain with the SSR row on reads the screen (first, then the rays on a ray
//            chain; alone - the raster march - without rays); a STEREO chain reads no screen (VR's one explicit
//            difference); the SSR row off reads nothing. The screen trace reads the last LINEAR stage (the refractives'
//            copy, else the composite), never the distortion's warp nor the AO apply; the composed picture (the stop's
//            displayed pixel) is the LAST stage that runs, in build's order refraction -> distortion -> AO.
//   shape    THE STEREO SHAPE: a stereo view's target is two layers, a mono one's one; a recording whose chain holds
//            another count is refused (RayQueryTier::recordReflect counts it - a defect, never a silent frame).
#include <cstdio>
#include <cstring>
#include "ReflectChainRules.h"

using namespace jahshaka::engine::detail;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

static int sources()
{
    // every SSR row, both shapes, both ray states: 3 x 2 x 2
    int combos = 0, wrong = 0;
    for (int ssr = 0; ssr <= 2; ++ssr)
        for (int st = 0; st < 2; ++st)
            for (int rays = 0; rays < 2; ++rays) {
                ++combos;
                const bool stereo = st != 0, rayReflect = rays != 0;
                const bool screen = reflectScreenSources(ssr, stereo);
                const bool march = reflectRasterMarch(ssr, stereo, rayReflect);
                const bool wantScreen = ssr > 0 && !stereo;
                if (screen != wantScreen || march != (wantScreen && !rayReflect)) ++wrong;
            }
    std::printf("the source rules over %d combinations: %d wrong\n", combos, wrong);
    check(combos == 12 && wrong == 0, "screen sources = the SSR row on AND mono; the raster march = those without rays");
    check(!reflectScreenSources(2, true) && !reflectRasterMarch(2, true, false),
          "a stereo chain reads no screen, rays or not (VR's one explicit difference)");
    check(reflectScreenSources(1, false) && !reflectRasterMarch(1, false, true),
          "a mono ray chain is the hybrid: the screen first, then rays (no march)");
    // the screen trace's source and the composed picture over the 8 combinations of the three linear-or-not stages
    int bad = 0;
    for (int m = 0; m < 8; ++m) {
        const bool refr = (m & 1) != 0, dist = (m & 2) != 0, ao = (m & 4) != 0;
        const ReflectChainImage src = reflectScreenTraceSource(refr);
        const ReflectChainImage comp = reflectComposedImage(ao, dist, refr);
        const ReflectChainImage wantComp = ao ? ReflectChainImage::AoApplied
                                         : dist ? ReflectChainImage::Distorted
                                                : refr ? ReflectChainImage::RefractOut : ReflectChainImage::Rt0;
        if (src == ReflectChainImage::Distorted || src == ReflectChainImage::AoApplied) ++bad;   // never a warp / AO
        if (src != (refr ? ReflectChainImage::RefractOut : ReflectChainImage::Rt0)) ++bad;
        if (comp != wantComp) ++bad;
    }
    check(bad == 0, "the screen trace reads the refractives' copy or the composite (never the warp or the AO); the "
                    "composed picture is the last of refraction -> distortion -> AO that runs (8 combinations)");
    // NEGATIVE CONTROL: the deleted mode's 'Rays' (no screen on a mono ray chain) differs from the rule - the check can fail
    const auto raysMode = [](int ssr, bool stereo, bool rays) { return ssr > 0 && !stereo && !rays; };
    check(raysMode(2, false, true) != reflectScreenSources(2, false),
          "negative control: the deleted Rays mode's rule (no screen on a mono ray chain) is NOT the hybrid's");
    std::printf("%s\n", gFail ? "engine.chain_reflect_sources: FAILED" : "engine.chain_reflect_sources: PASSED");
    return gFail ? 1 : 0;
}

static int shape()
{
    check(stereoTargetLayers(true) == 2u && stereoTargetLayers(false) == 1u, "stereo = two layers, mono = one");
    check(reflectChainMatchesStereo(true, 2u) && reflectChainMatchesStereo(false, 1u), "a chain at its view's shape records");
    check(!reflectChainMatchesStereo(true, 1u) && !reflectChainMatchesStereo(false, 2u),
          "a chain at the other shape is refused (both directions: a session's begin and its end)");
    check(!reflectChainMatchesStereo(true, 0u) && !reflectChainMatchesStereo(false, 3u),
          "no other count passes (a zero or a three-layer image is refused, never read as mono)");
    // NEGATIVE CONTROL: the old inline test `stereo != (layers == 2)` accepted a mono view on a 3-layer image - the rule
    // here is the exact count, so this check can fail
    const auto oldRule = [](bool stereo, unsigned layers) { return stereo == (layers == 2u); };
    check(oldRule(false, 3u) != reflectChainMatchesStereo(false, 3u),
          "negative control: the old inline test passed a mono view on a 3-layer image; the exact count refuses it");
    std::printf("%s\n", gFail ? "engine.reflect_chain_shape: FAILED" : "engine.reflect_chain_shape: PASSED");
    return gFail ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "sources") == 0) return sources();
    if (argc == 2 && std::strcmp(argv[1], "shape") == 0) return shape();
    std::printf("FAIL: usage: test_chain_reflect sources|shape\n");
    return 2;
}
