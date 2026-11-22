// engine.cascade_resolution_law (NONDET-1 D1) - A CASCADE IS BUILT AT A MULTIPLE OF 8 (GiParams.h giCascadeResolution, the
// one rule PhotonComponent::resolveCascadeTable applies to a pinned row). No device, no Ogre.
// LAW: every voxel pass runs in whole 8^3 bricks and 4^3 groups with no bound check, so a built resolution is a
// multiple of 8 in 16..256, never coarser than the row asked for (rounded UP), and a multiple of 8 is kept exactly.
// RED before: the rule was the clamp alone, so world.gi({cascadeSet}) built a 100 or a 50 as asked.
#include <cstdio>
#include "jahshaka/engine/GiModes.h"
#include "jahshaka/engine/GiParams.h"

using namespace jahshaka::engine;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    std::printf("giCascadeResolution: 100 -> %d, 50 -> %d, 129 -> %d, 7 -> %d, 300 -> %d\n", giCascadeResolution(100),
                giCascadeResolution(50), giCascadeResolution(129), giCascadeResolution(7), giCascadeResolution(300));
    check(giCascadeResolution(100) == 104, "a row of 100 builds at 104 (the next multiple of 8, never coarser)");
    check(giCascadeResolution(50) == 56, "a row of 50 builds at 56");
    bool all = true, kept = true;
    for (int r = -4; r <= 300; ++r) {
        const int b = giCascadeResolution(r);
        all = all && b % 8 == 0 && b >= 16 && b <= 256 && (r < 16 || r > 256 || (b >= r && b - r < 8));
        if (r >= 16 && r <= 256 && r % 8 == 0) kept = kept && b == r;
    }
    check(all, "every request -4..300 builds at a multiple of 8 in 16..256, the smallest one not under the request");
    check(kept, "a multiple of 8 in 16..256 is built as asked (every tier row: 64, 128)");
    // THE TIER TABLES: already multiples of 8 (the rule moves none of them)
    bool tiers = true;
    for (GiQuality q : { GiQuality::Low, GiQuality::Medium, GiQuality::High, GiQuality::Epic })
        for (GiViewProfile v : { GiViewProfile::Desktop, GiViewProfile::Vr }) {
            const GiQualityFacts f = giQualityFacts(q, v);
            for (int i = 0; i < f.cascadeCount; ++i)
                tiers = tiers && giCascadeResolution(f.cascades[i].resolution) == f.cascades[i].resolution;
        }
    check(tiers, "every tier row (both view profiles) is its own built resolution");
    // NEGATIVE CONTROL: the base's rule (PhotonGi.cpp's clamp alone) breaks the law, so the check above can fail
    const auto base = [](int r) { return r < 16 ? 16 : (r > 256 ? 256 : r); };
    check(base(100) % 8 != 0 && base(50) % 8 != 0, "negative control: the base's clamp built 100 and 50 as asked");
    std::printf("%s\n", gFail ? "engine.cascade_resolution_law: FAILED" : "engine.cascade_resolution_law: PASSED");
    return gFail ? 1 : 0;
}
