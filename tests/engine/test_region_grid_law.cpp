// engine.region_grid_law (NONDET-1 H2) - A PASS OVER A REGION DISPATCHES ONLY THE TEXELS THE REGION HOLDS
// (irisgl/engine/src/photon/voxel/PhotonRegionGrid.h, the arithmetic PhotonVoxelLighting's setRegionGrid sets its job
// from). No device, no Ogre.
// LAW: [lo, hi) of level 0 rounded OUT to a level's texels, clamped to the level; a box with no texel on some axis (empty,
// or past the level's end) is NO dispatch. RED before: the group count was max(h, l + 1) - l, so such a box dispatched
// one group, which lit the cells [res, res + 4) past the edge (the half store puts them in the next half's block).
#include <cstdint>
#include <cstdio>
#include "photon/voxel/PhotonRegionGrid.h"

using jahshaka::engine::detail::regionGridOf;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    const std::uint32_t res[3] = { 128u, 128u, 128u };
    std::uint32_t lo[3], n[3];
    // the whole level
    check(regionGridOf(nullptr, nullptr, 1u, res, lo, n) && lo[0] == 0u && n[0] == 128u && n[1] == 128u && n[2] == 128u,
          "no region: the whole level, 128 texels an axis from 0");
    // a box inside
    const std::uint32_t a0[3] = { 8u, 16u, 4u }, a1[3] = { 24u, 20u, 12u };
    check(regionGridOf(a0, a1, 1u, res, lo, n) && lo[0] == 8u && n[0] == 16u && lo[1] == 16u && n[1] == 4u && n[2] == 8u,
          "a box inside level 0: its own cells");
    // rounded out at a coarser level (2 cells a texel)
    const std::uint32_t b0[3] = { 5u, 5u, 5u }, b1[3] = { 9u, 9u, 9u };
    check(regionGridOf(b0, b1, 2u, res, lo, n) && lo[0] == 2u && n[0] == 3u, "cells [5, 9) at 2 cells a texel: texels [2, 5)");
    // clamped at the level's end
    const std::uint32_t c0[3] = { 120u, 0u, 0u }, c1[3] = { 140u, 4u, 4u };
    check(regionGridOf(c0, c1, 1u, res, lo, n) && lo[0] == 120u && n[0] == 8u, "a box over the edge: clamped at 128");
    // EMPTY: no dispatch
    const std::uint32_t e0[3] = { 16u, 16u, 16u }, e1[3] = { 16u, 32u, 32u };
    check(!regionGridOf(e0, e1, 1u, res, lo, n) && n[0] == 0u, "an empty box (lo = hi on x): NO texel, no dispatch");
    const std::uint32_t p0[3] = { 128u, 0u, 0u }, p1[3] = { 136u, 8u, 8u };
    check(!regionGridOf(p0, p1, 1u, res, lo, n) && n[0] == 0u, "a box past the level's end (x from 128): NO texel, no dispatch");
    const std::uint32_t q0[3] = { 0u, 0u, 200u }, q1[3] = { 8u, 8u, 210u };
    check(!regionGridOf(q0, q1, 1u, res, lo, n), "past the end on z alone: no dispatch");
    // NEGATIVE CONTROL: the base's count, max(h, l + 1) - l, dispatches one texel for the past-the-end box
    const std::uint32_t l = 128u, h = 128u;
    check((h > l + 1u ? h : l + 1u) - l == 1u, "negative control: the base's rule gave the past-the-end box one texel at x = 128");
    std::printf("%s\n", gFail ? "engine.region_grid_law: FAILED" : "engine.region_grid_law: PASSED");
    return gFail ? 1 : 0;
}
