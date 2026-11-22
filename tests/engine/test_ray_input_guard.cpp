// engine.ray_input_guard — A RAY JOB WITH A NULL INPUT VIEW REFUSES ITS DISPATCH AND SAYS SO ONCE (GATHER-HANG-1 stage 2;
// irisgl/engine/src/RayInputGuard.h says the law, RayQueryViewCache.h the views). No device, no Ogre.
// LAW: every view made = admitted, nothing counted or said; any view null = refused, counted every time, said the FIRST
// time for that job only (with the job's name and how many were null); jobs are said apart. And THE FORCED NON-RESIDENT
// INPUT: RayQueryTier::textureView hands the cache image 0 for a texture that is not resident; the cache answers a null
// view and caches and registers nothing, and the job that binds it refuses - before the guard it was viewed on the
// manager's blank image and traced as the input.
#include <cstdio>
#include <string>

#ifndef JAH_RAY_INPUT_GUARD_UNDER_TEST   // (spikes/gather-hang-1/stage2 runs these cases on the base's silent decline as the red)
#include "RayInputGuard.h"
#define JAH_RAY_INPUT_GUARD_UNDER_TEST jahshaka::engine::detail::RayInputGuard
#endif
#include "RayQueryViewCache.h"

using Guard = JAH_RAY_INPUT_GUARD_UNDER_TEST;
using jahshaka::engine::detail::RayQueryViewCache;
using jahshaka::engine::detail::RayViewKey;
using jahshaka::engine::detail::RayViewKind;

static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    {   // 1. every view made: admitted, nothing counted, nothing said
        Guard g;
        std::string say = "x";
        check(g.admit("reflect", { 0x11u, 0x12u, 0x13u }, &say) && g.refused() == 0u && say.empty(),
              "1. every view made: admitted, nothing counted, nothing said");
        check(g.admit("reflect", {}, &say) && g.refused() == 0u, "1. a job with no views is admitted");
    }
    {   // 2. a null view: refused and said the first time; the second refusal counted, not said
        Guard g;
        std::string first, second;
        const bool a = g.admit("probe gather", { 0x11u, 0u, 0x13u, 0u }, &first);
        const bool b = g.admit("probe gather", { 0u }, &second);
        check(!a && !b && g.refused() == 2u && g.refused("probe gather") == 2u,
              "2. a null view refuses the dispatch, every time, each refusal counted");
        check(first.find("probe gather") != std::string::npos && first.find("2 of 4") != std::string::npos &&
                  first.find("first at 1") != std::string::npos,
              "2. the first refusal is said, naming the job and the null views (2 of 4, the first at 1)");
        check(second.empty(), "2. the second refusal of the same job is not said again");
    }
    {   // 3. jobs are said apart; an admitted job in between changes nothing
        Guard g;
        std::string a, b, c;
        g.admit("reflect", { 0u }, &a);
        g.admit("area vis", { 0x21u }, &b);
        g.admit("area vis", { 0u }, &c);
        check(!a.empty() && b.empty() && !c.empty() && g.refused() == 2u && g.refused("reflect") == 1u &&
                  g.refused("area vis") == 1u,
              "3. each job's first refusal is said once, apart from the others'");
    }
    {   // 4. THE FORCED NON-RESIDENT INPUT: image 0 from the tier's rule -> a null view -> the job refuses, said once
        int makes = 0, watches = 0;
        RayQueryViewCache cache{ [&](const RayViewKey &, uint64_t image) -> uint64_t { ++makes; return image + 0x1000u; },
                                 [](uint64_t) {}, [&](const void *, bool on) { if (on) ++watches; } };
        int volume = 0, sky = 0;
        const auto viewOf = [&](const void *t, bool resident, uint64_t image) {
            RayViewKey k;
            k.texture = t;
            k.kind = RayViewKind::Sampled;
            return cache.view(k, resident ? image : 0u);   // RayQueryTier::textureView's rule: not resident -> image 0
        };
        Guard g;
        std::string say;
        const uint64_t vSky = viewOf(&sky, true, 0xB1u);
        const uint64_t vVolume = viewOf(&volume, false, 0xA1u);   // the voxel volume forced OnStorage
        const bool admitted = g.admit("probe gather", { vSky, vVolume }, &say);
        check(vSky != 0u && vVolume == 0u && makes == 1 && watches == 1 && cache.live() == 1u,
              "4. a non-resident texture gets no view: nothing made, cached or registered for it");
        check(!admitted && !say.empty() && g.refused("probe gather") == 1u,
              "4. the job binding it refuses its dispatch and says so");
        const uint64_t again = viewOf(&volume, true, 0xA1u);   // resident again
        check(again != 0u && g.admit("probe gather", { vSky, again }, &say) && say.empty(),
              "4. resident again: the view is made and the job is admitted");
    }
    std::printf("%s\n", gFail ? "engine.ray_input_guard: FAILED" : "engine.ray_input_guard: PASSED");
    return gFail ? 1 : 0;
}
