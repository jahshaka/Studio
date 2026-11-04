// gi.share_law (BISTRO-REST-1, the lead's ruling (B)) - THE HIT READ's BOUNCE SHARE IS LINEAR IN THE STORE IT READS.
// A ray's hit read hands back the store's bounce part (rayquery/include/jah_rq_hit.glsl jahVoxelRadiance's
// jahVoxelBounceOut): since BISTRO-REST-1's fix, the read of the total less the SAME read of the direct chain. The shipped share was weighed by a precision TRUST that read
// T itself (clamp( 2 - 100 q / T ), q the store's quantum): over the dim range (premultiplied T about 6e-6 .. 1.2e-5) its
// slope amplified the answer up to 3-4x, and on Bistro Exterior at Epic two halves of one coarse cell that read each other
// through millimetre geometry orbited at period 2 for good (spikes/bistro-rest-1: 3.4e-6 <-> 5.4e-5, the answer 21x apart)
// - the feedback never rested, nor any reader after it.
// THIS ROW RUNS THE SHIPPED READ (Engine::giLaw(GiLaw::Share): rq_share_law.comp includes the one jah_rq_hit.glsl and calls
// jahVoxelRadiance over a uniform fixture store - the half store, the chains, the coverages) and holds:
//   (i)  THE SHARE IS CONTINUOUS AND MONOTONE in T, over 1e-7 .. 1e-2 sampled every 2^(1/256) (finer than a binade by
//        256): no step between neighbours past 0.01, never falling as T rises (D = 0 and D = T / 2);
//   (ii) THE READ IS LINEAR: the bounce part's relative slope T d(bounce) / dT is 1 within 1e-3 over the whole range (D
//        a fixed share of T - the bounce then scales with T exactly), at the hit's level-0 half, so a loop through it
//        has the operator's gain, never more;
//   (iii) TWO HALVES OF ONE CELL reading each other (returning fraction F, albedo rho, source S, coverage k), under the
//        Jacobi door from out of phase: NO PERIOD-2 (the last two pairs equal, not alternating) and the fixed point
//        D / ( 1 - rho F ) - here S / ( 1 - rho F ) - within one half-float step, for fixed points swept across the dim
//        range (1e-7 .. 1e-3) at rho F = 0.19 (Bistro's: 0.6 x 5/16), 0.30 and 0.48.
// On the base shader (the trust, its share at the pick texel) this row was RED, run once (BISTRO-REST-1): (i) a 0.0108
// step at T 5.98e-6, (ii) the slope off by 562, (iii) 2 period-2 orbits and 138 fixed points dark by up to 86.6 steps.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static float halfStep(float v)
{
    const float a = std::fabs(v);
    return a < 6.1035156e-5f ? 5.9604645e-8f : std::ldexp(1.0f, int(std::floor(std::log2(a))) - 10);
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-share-law-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    View *view = e->createOffscreenView("share", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("share");
    view->setScene(scene);
    enginetest::frames(e, 2);   // (the ray tier opens in a frame)

    // ---- (i) and (ii): THE SWEEP ------------------------------------------------------------------------------------
    const float k = 0.33f;
    std::vector<GiLawQuestion> qs;
    std::vector<float> Ts;
    for (double lt = std::log2(1e-7); lt <= std::log2(1e-2); lt += 1.0 / 256.0) Ts.push_back(float(std::exp2(lt)));
    // two sweeps: D = 0 and D = T/2, at the hit's level-0 half (the only level a hit reads since BISTRO-REST-1's
    // sealed-room fix)
    constexpr int kSweeps = 2;
    for (int sw = 0; sw < kSweeps; ++sw)
        for (float T : Ts) {
            GiLawQuestion q;
            q.q[0] = T; q.q[1] = (sw & 1) ? 0.5f * T : 0.0f; q.q[2] = k;
            q.u[0] = 0u;
            qs.push_back(q);
        }
    // ---- (iii): THE LOOP ---------------------------------------------------------------------------------------------
    struct Loop { float S, F, rho, star; };
    std::vector<Loop> loops;
    const float pairs[3][2] = { { 0.6f, 0.3125f }, { 0.95f, 0.3158f }, { 0.8f, 0.6f } };   // rho, F: rho F 0.19, 0.30, 0.48
    for (const auto &p : pairs)
        for (double lx = std::log2(1e-7); lx <= std::log2(1e-3); lx += 0.125) {
            const float star = float(std::exp2(lx)), S = star * (1.0f - p[0] * p[1]);
            loops.push_back({ S, p[1], p[0], star });
            GiLawQuestion q;
            q.q[0] = S; q.q[1] = p[1]; q.q[2] = p[0]; q.q[3] = k;
            q.u[0] = 1u;
            q.u[1] = 600u;
            qs.push_back(q);
        }
    std::vector<GiLawAnswer> as;
    if (!e->giLaw(GiLaw::Share, qs, as) || as.size() != qs.size()) {
        std::printf("FAIL: giLaw(Share): %s\n", e->takeLastError().c_str());
        return 1;
    }
    int fails = 0;
    // (i) continuous and monotone; (ii) linear
    for (int sw = 0; sw < kSweeps; ++sw) {
        const int d = sw & 1;
        double worstStep = 0.0, worstFall = 0.0, worstSlope = 0.0;
        float atStep = 0.f, atFall = 0.f, atSlope = 0.f;
        const size_t base = size_t(sw) * Ts.size();
        for (size_t j = 1; j < Ts.size(); ++j) {
            const GiLawAnswer &a0 = as[base + j - 1], &a1 = as[base + j];
            const double step = std::fabs(double(a1.a[0]) - a0.a[0]);
            if (step > worstStep) { worstStep = step; atStep = Ts[j]; }
            const double fall = double(a0.a[0]) - a1.a[0];
            if (fall > worstFall) { worstFall = fall; atFall = Ts[j]; }
            if (a0.a[1] > 0.0f && a1.a[1] > 0.0f) {
                const double slope = std::log(double(a1.a[1]) / a0.a[1]) / std::log(double(Ts[j]) / Ts[j - 1]);
                if (std::fabs(slope - 1.0) > worstSlope) { worstSlope = std::fabs(slope - 1.0); atSlope = Ts[j]; }
            }
        }
        const bool okI = worstStep <= 0.01 && worstFall <= 1e-6;
        const bool okII = worstSlope <= 1e-3;
        std::printf("%s (i)  D = %s: the share's largest step between neighbours %.6f (at T %.3g), its largest "
                    "fall %.3g (at T %.3g) - bars 0.01 / 0\n", okI ? "ok:" : "FAIL:", d ? "T/2" : "0",
                    worstStep, atStep, worstFall, atFall);
        std::printf("%s (ii) D = %s: the bounce part's relative slope T d(bounce)/dT off 1 by at most %.6f (at T "
                    "%.3g) - bar 1e-3\n", okII ? "ok:" : "FAIL:", d ? "T/2" : "0", worstSlope, atSlope);
        fails += !okI + !okII;
    }
    // (iii) no period-2, the closed form
    {
        int orbits = 0, offs = 0;
        double worstOff = 0.0;
        const Loop *worst = nullptr;
        for (size_t j = 0; j < loops.size(); ++j) {
            const GiLawAnswer &a = as[kSweeps * Ts.size() + j];
            const Loop &L = loops[j];
            const float x0 = a.a[2], x1 = a.a[3], y0 = a.b[2], y1 = a.b[3];
            const float step = halfStep(L.star);
            const bool orbit = std::fabs(x1 - x0) > step || std::fabs(y1 - y0) > step;
            const double off = std::max(std::fabs(double(x1) - L.star), std::fabs(double(y1) - L.star)) / step;
            orbits += orbit;
            if (off > 1.0) ++offs;
            if (off > worstOff) { worstOff = off; worst = &L; }
            if (orbit && orbits <= 3)
                std::printf("  ORBIT: rho %.2f F %.4f fixed point %.4g - X %.6g / %.6g, Y %.6g / %.6g\n", L.rho, L.F, L.star,
                            x0, x1, y0, y1);
        }
        const bool okIII = orbits == 0 && offs == 0;
        std::printf("%s (iii) %zu two-half loops: %d at period 2, %d off S/(1 - rho F) past one store step (the worst %.1f "
                    "steps: rho %.2f F %.4f fixed point %.4g) - bars 0 / 0\n", okIII ? "ok:" : "FAIL:", loops.size(), orbits,
                    offs, worstOff, worst ? worst->rho : 0.f, worst ? worst->F : 0.f, worst ? worst->star : 0.f);
        fails += !okIII;
    }
    std::printf("gi.share_law: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
