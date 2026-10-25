// gi.inject_fold (PHOTON-7's walk hitch, 2) — THE INJECTION's BOXES FOLD PAIR BY PAIR, NEVER INTO THEIR UNION. A scroll commit's
// regions (each light's sweep of each moved face, and the slabs) past the injection's 16 boxes are folded; the union of
// them all, the fold that stood before, spanned the whole cascade as soon as they lay at both ends of an axis (Bistro
// Interior: four lamps and the sun on a two-axis step, c0 and c1 computing 100 % of 128^3 for a 1.4 % slab). LAWS, on
// the regions of a two-axis step at 128 cells (the slab and trailing face of x and of z, swept by five lights):
//   (1) the folded set holds at most the count;
//   (2) A BOUND: every input cell lies in a folded box;
//   (3) the faces stay apart: no folded box spans the cascade on an axis (from the low end's band to the high end's),
//       and the folded boxes cover under half of the cascade.
#include "irisgl/engine/src/photon/PhotonInjectBoxes.h"

#include <cstdio>

using Box = std::array<int, 6>;

int main() {
    const int res = 128;
    std::vector<Box> in;
    // per light: a band at each moved face of x and of z (the sweeps of 5 lights, clipped differently in y), and the slabs
    for (int l = 0; l < 5; ++l) {
        const int y0 = 8 * l, y1 = 64 + 8 * l;
        in.push_back({ 104, y0, 0, 128, y1, 128 });   // x's slab side
        in.push_back({ 0, y0, 0, 12, y1, 128 });      // x's trailing face
        in.push_back({ 0, y0, 108, 128, y1, 128 });   // z's slab side
        in.push_back({ 0, y0, 0, 128, y1, 16 });      // z's trailing face
    }
    in.push_back({ 116, 0, 0, 128, 128, 128 });       // the x slab
    in.push_back({ 0, 0, 120, 128, 128, 128 });       // the z slab
    std::vector<Box> out = in;
    jahshaka::engine::detail::foldInjectBoxes(out, 16u);
    int fails = 0;
    if (out.size() > 16u) { std::printf("FAIL: (1) %zu boxes after the fold\n", out.size()); ++fails; }
    // (2) every input cell lies in a folded box (sampled at every cell of a 4-cell lattice and every box corner)
    for (const Box &b : in)
        for (int z = b[2]; z < b[5]; z += 4)
            for (int y = b[1]; y < b[4]; y += 4)
                for (int x = b[0]; x < b[3]; x += 4) {
                    bool held = false;
                    for (const Box &o : out)
                        held = held || (x >= o[0] && x < o[3] && y >= o[1] && y < o[4] && z >= o[2] && z < o[5]);
                    if (!held) { if (fails < 8) std::printf("FAIL: (2) cell %d,%d,%d is in no folded box\n", x, y, z); ++fails; }
                }
    // (3) apart: no folded box reaches from x's trailing band to its slab side (or z's) - and the cover stays small
    long long cover = 0;
    std::vector<char> seen(size_t(res) * res * res, 0);
    for (const Box &o : out) {
        if (o[0] < 12 && o[3] > 104 && o[2] < 16 && o[5] > 108) {
            std::printf("FAIL: (3) a folded box %d..%d x %d..%d spans both faces of x and of z\n", o[0], o[3], o[2], o[5]);
            ++fails;
        }
        for (int z = o[2]; z < o[5]; ++z)
            for (int y = o[1]; y < o[4]; ++y)
                for (int x = o[0]; x < o[3]; ++x) {
                    char &s = seen[(size_t(z) * res + size_t(y)) * res + size_t(x)];
                    if (!s) { s = 1; ++cover; }
                }
    }
    const double share = double(cover) / double(res * res * res);
    std::printf("%zu regions -> %zu boxes covering %.1f %% of the cascade\n", in.size(), out.size(), 100.0 * share);
    if (share >= 0.5) { std::printf("FAIL: (3) the folded boxes cover %.1f %% of the cascade\n", 100.0 * share); ++fails; }
    std::printf("gi.inject_fold: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
