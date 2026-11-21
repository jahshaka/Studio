// engine.rough_lobe_transport — THE ROUGH LOBE's HISTORY IS CARRIED TO THE CURRENT VIEW BEFORE IT IS BLENDED
// (ROUGH-LOBE-HISTORY-1, V2 plan 1.8; irisgl engine/src/RoughLobeCarry.h, the shader's one-line update as a pure function -
// source.rough_lobe_transport pins rq_probe_integrate.comp's lines to it). No device, no Ogre: a CPU code row.
//
// THE MODEL (the preflight's): at one surface point the lobe moves linearly under a walk, L_t = L0 + t dL; a frame's
// estimate is L(V) + n_t, its noise n_t (sigma) THE FRAME's TAPS' - shared by both views evaluated on them, so the
// carry's delta is L_t - L_(t-1). The blend is the gather's: 1 / (n + 1) while young (a true running mean, the first frame
// whole), floored at alpha = 1 / N (N = 10, the history's frames).
// THE LAWS (the closed forms):
//   (a) THE CARRIED MEAN's EXPECTATION IS L_t, young and at the floor (e_t = (1 - a) e_(t-1) + a n_t telescopes for any
//       L) - within 3 SE of 8,192 walks; with no noise to float rounding. Its spread at the floor is the EMA's,
//       sigma sqrt(a / (2 - a)).
//   (b) NEGATIVE CONTROL - THE BARE BLEND LAGS, by n dL / 2 at count n while young and by dL (1 - a) / a = (N - 1) dL at
//       the floor (within 3 SE): it breaks (a)'s law, which the row therefore can see.
//   (c) AT REST NOTHING MOVES: without a carry (no history, or the eye's view unmoved: the camera's bits equal) the delta
//       is the literal +0.0, the carried history is the stored one bit for bit and the mean is the bare mix's bits; with a
//       carry and equal lobes the delta is +0.0 too (x - x).
// (Each eye's own previous camera and one call site a lobe evaluator are the shader's text: source.rough_lobe_transport.)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "RoughLobeCarry.h"

using namespace jahshaka::engine::detail;

namespace {
int gFail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        ++gFail;
}
uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

struct Pcg {   // a deterministic stream: the row's numbers are the code's
    uint64_t s;
    explicit Pcg(uint64_t seed) : s(seed * 2862933555777941757ull + 3037000493ull) {}
    float u()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return float((s >> 40) & 0xFFFFFFull) / 16777216.0f;
    }
    float n() { return (u() + u() + u() - 1.5f) * 2.0f; }   // zero mean, sigma 1
};

constexpr float kL0 = 0.5f, kDL = 0.004f, kSigma = 0.05f, kAlpha = 0.1f;   // N = 10
constexpr int kFrames = 200, kWalks = 8192;
float Lat(int t) { return kL0 + float(t) * kDL; }
float blendAt(int count) { return count == 0 ? 1.0f : std::fmax(1.0f / float(count + 1), kAlpha); }

// one walk: both histories from a first frame (count 0) on; each frame's estimate at the current and the previous view
// on the same taps
void walk(uint64_t seed, float noise, float *carriedOut, float *bareOut)
{
    Pcg rng(seed);
    float carried = 0.0f, bare = 0.0f;
    for (int t = 0; t <= kFrames; ++t) {
        const float n = noise * rng.n();
        const float lobeNow = Lat(t) + n, lobeAtPrev = Lat(t - 1) + n;
        const bool history = t > 0;
        const float b = blendAt(t);
        const float delta = roughLobeDelta(lobeNow, lobeAtPrev, history);
        carried = roughLobeMean(history ? roughLobeCarried(carried, delta) : 0.0f, lobeNow, b);
        bare = roughLobeMean(history ? bare : 0.0f, lobeNow, b);
        carriedOut[t] = carried;
        bareOut[t] = bare;
    }
}
} // namespace

int main()
{
    static float c[kFrames + 1], b[kFrames + 1];
    static double sc[kFrames + 1], sc2[kFrames + 1], sb[kFrames + 1], sb2[kFrames + 1];

    // (a) NO NOISE: the carried mean IS L_t to float rounding at every frame
    walk(1u, 0.0f, c, b);
    float worst = 0.0f;
    for (int t = 0; t <= kFrames; ++t)
        worst = std::fmax(worst, std::fabs(c[t] - Lat(t)));
    std::printf("no noise: |carried - L_t| worst %.3g over %d frames; the bare form at t %d: %+.6f (closed form %+.6f)\n",
                worst, kFrames + 1, kFrames, b[kFrames] - Lat(kFrames), -kDL * (1.0f - kAlpha) / kAlpha);
    check(worst < 4e-6f, "(a) no noise: the carried mean equals L_t to float rounding, young and at the floor");

    for (int w = 0; w < kWalks; ++w) {
        walk(uint64_t(w) + 2u, kSigma, c, b);
        for (int t = 0; t <= kFrames; ++t) {
            sc[t] += c[t]; sc2[t] += double(c[t]) * c[t];
            sb[t] += b[t]; sb2[t] += double(b[t]) * b[t];
        }
    }
    auto stats = [](const double *s, const double *s2, int t, double &mean, double &se, double &sd) {
        mean = s[t] / kWalks;
        sd = std::sqrt(std::fmax(s2[t] / kWalks - mean * mean, 0.0));
        se = sd / std::sqrt(double(kWalks));
    };
    // young frames (count n <= 9: blend 1 / (n + 1)) and the floor's (t 14, 28, 59 - the card rows' walk frames - and 200)
    for (int t : { 4, 9, 14, 28, 59, kFrames }) {
        double mc, sec, sdc, mb, seb, sdb;
        stats(sc, sc2, t, mc, sec, sdc);
        stats(sb, sb2, t, mb, seb, sdb);
        const double eC = mc - Lat(t), eB = mb - Lat(t);
        // the bare form's closed form: n dL / 2 while young; at the floor from t = 9 on, e_t = (1 - a)(e_(t-1) - dL) from
        // e_9 = -9 dL / 2, whose limit is -dL (1 - a) / a
        double lag = 0.0;
        if (t <= 9)
            lag = -double(t) * kDL / 2.0;
        else {
            lag = -9.0 * kDL / 2.0;
            for (int u = 10; u <= t; ++u)
                lag = (1.0 - kAlpha) * (lag - kDL);
        }
        std::printf("t %3d: L %.4f | carried E %+.6f (3 SE %.6f; sd %.5f, the floor's closed form %.5f) | bare E %+.6f "
                    "(closed form %+.6f, 3 SE %.6f)\n",
                    t, Lat(t), eC, 3.0 * sec, sdc, kSigma * std::sqrt(kAlpha / (2.0 - kAlpha)), eB, lag, 3.0 * seb);
        char what[200];
        std::snprintf(what, sizeof what, "(a) t %d: the carried mean's expectation is L_t within 3 SE", t);
        check(std::fabs(eC) <= 3.0 * sec + 1e-6, what);
        std::snprintf(what, sizeof what, "(b) t %d: the bare blend lags by its closed form within 3 SE", t);
        check(std::fabs(eB - lag) <= 3.0 * seb + 1e-6, what);
        if (t > 0) {
            std::snprintf(what, sizeof what, "(b) t %d: NEGATIVE CONTROL - the bare blend breaks (a)'s law", t);
            check(std::fabs(eB) > 3.0 * seb + 1e-6, what);
        }
    }
    {
        double mc, sec, sdc;
        stats(sc, sc2, kFrames, mc, sec, sdc);
        const double closed = kSigma * std::sqrt(kAlpha / (2.0 - kAlpha));
        check(std::fabs(sdc - closed) <= 0.05 * closed, "(a) the carried mean's spread at the floor is sigma sqrt(a / (2 - a)) within 5 %");
    }

    // (c) AT REST: no carry -> +0.0 literal, the stored history's bits, the bare mix's bits; equal lobes -> +0.0
    {
        const float stored = 0.4321f, lobe = 0.4567f;
        const float d0 = roughLobeDelta(0.7f, 0.3f, false);
        check(bits(d0) == 0u, "(c) no carry: the delta is +0.0 bit-exact (never evaluated)");
        check(bits(roughLobeCarried(stored, d0)) == bits(stored), "(c) no carry: the carried history IS the stored one, bit for bit");
        check(bits(roughLobeMean(roughLobeCarried(stored, d0), lobe, kAlpha)) == bits(roughLobeMean(stored, lobe, kAlpha)),
              "(c) no carry: the mean IS the bare mix's, bit for bit");
        check(bits(roughLobeDelta(lobe, lobe, true)) == 0u, "(c) a carry between equal lobes: +0.0 (x - x)");
        check(bits(roughLobeDelta(lobe, std::nextafter(lobe, 1.0f), true)) != 0u,
              "(c) CONTROL: a carry between lobes one ulp apart is not zero");
    }

    std::printf("%s\n", gFail ? "engine.rough_lobe_transport: FAILED" : "engine.rough_lobe_transport: PASSED");
    return gFail ? 1 : 0;
}
