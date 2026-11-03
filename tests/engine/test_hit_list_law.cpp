// engine.hit_list_law — THE REFLECTION REST's RECORD ALLOWANCE, as a pure function (URGENT #157;
// irisgl/engine/src/HitListLaw.h). No device, no Ogre.
// LAW: the allowance is a function of the view's TARGET (its creation capacity) and the rest's own batching count -
// a hit list that grew (another scene's overflow, a larger target before) moves no rest's rays a texel.
#include <cstdio>
#include <initializer_list>
#include "HitListLaw.h"

using jahshaka::engine::detail::restRecordAllowance;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    // the self-test's editor view: 748x291 creates ceil(0.5625 x 748 x 291 / 1024) = 120 rows of 1024 records
    const unsigned long long created = 120ull * 1024ull;
    const unsigned share = 4u;
    // 1. growth is invisible: the same target and batching give the same allowance whatever the list grew to
    bool same = true;
    for (unsigned long long batching : { 1ull, 1000ull, 29196ull, 30720ull, 40000ull, 122880ull, 200000ull })
        for (unsigned long long grown = created; grown <= created * 16ull; grown *= 2ull)
            same = same && restRecordAllowance(share, grown, created, batching) ==
                               restRecordAllowance(share, created, created, batching);
    check(same, "a list grown 1x..16x past its creation size gives every batching count the creation size's allowance");
    // 2. the law's value: min(share, created / batching)
    check(restRecordAllowance(share, created, created, 29196ull) == 4u, "29196 texels under 122880 records: the share, 4");
    check(restRecordAllowance(share, created, created, 40000ull) == 3u, "40000 texels: 122880 / 40000 = 3 a texel");
    check(restRecordAllowance(share, created * 8ull, created, 40000ull) == 3u,
          "40000 texels on a list grown 8x: still 3 (the base read the grown list: 24, capped to the share 4)");
    // 3. a list SMALLER than its creation size (built before its target grew; the rq.hitListCap arm) caps it
    check(restRecordAllowance(share, 1000ull, created, 500ull) == 2u, "a 1000-record list, 500 texels: 2 a texel");
    // 4. nothing batching: no allowance (no read-back has counted the texels)
    check(restRecordAllowance(share, created, created, 0ull) == 0u, "no batching texel: 0");
    // NEGATIVE CONTROL: the base's rule (the grown list's capacity) DOES move with growth - the check above can fail
    const auto base = [](unsigned s, unsigned long long list, unsigned long long b) {
        return unsigned(list / b < s ? list / b : s);
    };
    check(base(share, created, 40000ull) != base(share, created * 8ull, 40000ull),
          "negative control: the base's grown-capacity rule moves 3 -> 4 when the list grows 8x");
    std::printf("%s\n", gFail ? "engine.hit_list_law: FAILED" : "engine.hit_list_law: PASSED");
    return gFail ? 1 : 0;
}
