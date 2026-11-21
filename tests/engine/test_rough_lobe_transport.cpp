// engine.rough_lobe_transport — THE ROUGH LOBE's HISTORY IS CARRIED TO THE CURRENT VIEW BEFORE IT IS BLENDED
// (ROUGH-LOBE-HISTORY-1, V2 plan 1.8). No device, no Ogre: a CPU code row.
//
// THE SHADER (irisgl engine/src/rayquery/rq_probe_integrate.comp, the rough specular): the lobe's running mean was blended
// in the view it was made in, mix( prevLobe, lobe, blend ), so on a walk it lagged the current view by (N - 1) dL (N =
// 1 / blend, dL the lobe's change a frame) - the soft reflection trails. Now
//     meanLobe = mix( prevLobe + delta, lobe, blend ),   delta = roughLobeNow( V ) - roughLobeNow( V_prev ),
// both evaluated on THIS frame's taps (a loop of two inside roughLobeNow, only with a history and only when the eye's
// view moved), V_prev from jahEyeBefore( eye ): the same quantity, carried - no clamp, no switch, no history rejected.
// The functions below transcribe the shader's (the names are its own); source.rough_lobe_transport holds the shader's
// text to the same form.
//
// THE LAWS:
//   (a) THE TRANSPORTED MEAN TRACKS THE CURRENT VIEW: under a walk, its expectation over the frames' tap noise equals
//       L(V_now) at every frame (telescoping: each frame carries the last frame's view to this one) - within 3 SE of a
//       4,096-walk Monte Carlo, and to float rounding with no noise. NEGATIVE CONTROL: the bare form lags by
//       (N - 1) dL (1 - (1 - b)^k) and breaks the same law.
//   (b) AT REST NOTHING MOVES: with the eye's view unchanged (the same camera bits, or a perspective eye that only
//       turned) delta is +0.0 BIT-EXACT and the carried mean is bit-identical to the bare one; a camera moved by one ulp
//       does evaluate (the control: the gate is the view's bits, not always shut).
//   (c) PER EYE (multiview): each eye's delta reads ONLY its own previous camera - eye 0's mean is bit-identical whatever
//       eye 1's previous camera was; eye 1's moves with it (the control). No stereo card row reaches the gather, so the
//       per-eye law rests on this row.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int gFail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        ++gFail;
}

struct V3 { float x, y, z; };
V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(V3 a) { return a * (1.0f / std::sqrt(dot(a, a))); }
V3 reflect(V3 i, V3 n) { return i - n * (2.0f * dot(n, i)); }
uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
bool sameBits(V3 a, V3 b) { return bits(a.x) == bits(b.x) && bits(a.y) == bits(b.y) && bits(a.z) == bits(b.z); }
float mix(float x, float y, float a) { return x * (1.0f - a) + y * a; }

// jah_probe_params.glsl's JahEye (the members the view direction reads)
struct JahEye { V3 camPos; float perspective; V3 fwd; };
// the shader's toEyeOf: the ray back to a perspective eye, -fwd for an orthographic one
V3 toEyeOf(const JahEye &e, V3 world) { return e.perspective < 0.5f ? V3{ -e.fwd.x, -e.fwd.y, -e.fwd.z } : normalize(e.camPos - world); }
// the shader's jahEyeViewMoved: whether the view direction at a point CAN differ - the bits toEyeOf reads
bool eyeViewMoved(const JahEye &a, const JahEye &b)
{
    if (bits(a.perspective) != bits(b.perspective))
        return true;
    return a.perspective < 0.5f ? !sameBits(a.fwd, b.fwd) : !sameBits(a.camPos, b.camPos);
}

// ONE FRAME's TAPS: the prefiltered environment the probes hold this frame - the true one plus this frame's noise field
// (a random plane in the direction: zero mean, so E[lobe(V)] = L(V)). Both evaluations of a frame read the SAME taps.
struct Taps { float a, bx, by, bz; };
float envTrue(V3 d) { return 0.35f + 0.25f * d.y + 0.30f * d.x - 0.10f * d.z; }
float probeLobe(V3 N, V3 V, const Taps &t)
{
    const V3 d = reflect(V3{ -V.x, -V.y, -V.z }, N);
    return std::fmax(envTrue(d) + t.a + t.bx * d.x + t.by * d.y + t.bz * d.z, 0.0f);
}
// THE SHADER's roughLobeNow: the lobe at V, and with `carry` the lobe at V_prev in a loop of two (one call site of the
// evaluator); delta = lobe(V) - lobe(V_prev), +0.0 when not carried
float roughLobeNow(V3 N, V3 V, V3 Vprev, bool carry, const Taps &t, float &delta)
{
    float now = 0.0f;
    delta = 0.0f;
    for (int k = 0; k < (carry ? 2 : 1); ++k) {
        const float lobe = probeLobe(N, k == 0 ? V : Vprev, t);
        if (k == 0)
            now = lobe;
        else
            delta = now - lobe;
    }
    return now;
}
// ONE PIXEL's FRAME of eye `eye`: the shader's rough history step (the blend's, no rest) - eyes[] now, before[] the
// previous cameras, the pixel's history its own eye's layer
float historyStep(int eye, const JahEye *now, const JahEye *before, V3 world, V3 N, float prevLobe, bool hasHistory,
                  float blend, const Taps &t)
{
    const V3 V = toEyeOf(now[eye], world);
    const V3 Vprev = toEyeOf(before[eye], world);
    float delta;
    const float lobe = roughLobeNow(N, V, Vprev, hasHistory && eyeViewMoved(now[eye], before[eye]), t, delta);
    const float carried = hasHistory ? prevLobe + delta : 0.0f;
    const float m = mix(carried, lobe, hasHistory ? blend : 1.0f);
    return std::fmax(m, 0.0f);   // (the shader's clamp at zero, :1398 - bias, measured by its arm on the card)
}
float bareStep(float prevLobe, float lobe, float blend) { return std::fmax(mix(prevLobe, lobe, blend), 0.0f); }

// a deterministic stream (no libc rand: the row's numbers are the code's)
struct Pcg {
    uint64_t s;
    explicit Pcg(uint64_t seed) : s(seed * 2862933555777941757ull + 3037000493ull) {}
    float u()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return float((s >> 40) & 0xFFFFFFull) / 16777216.0f;
    }
    float n() { return u() + u() + u() - 1.5f; }   // zero mean, sigma 0.5
};
} // namespace

int main()
{
    // THE WALK: rough_lag.sh's (0,1.6,2.4 -> 0,1.6,0.4 over 60 frames), a floor point at (0.6, 0, -1.2) seen from it;
    // the gather's floor blend 0.1 (the history's ten frames: N - 1 = 9)
    const V3 N{ 0.0f, 1.0f, 0.0f };
    const V3 world{ 0.6f, 0.0f, -1.2f };
    const float blend = 0.1f;
    const int frames = 60;
    auto eyeAt = [](int k) {
        const float s = float(k) / 60.0f;
        return JahEye{ V3{ 0.0f, 1.6f, 2.4f + (0.4f - 2.4f) * s }, 1.0f, V3{ 0.0f, -0.4f, -0.9f } };
    };
    auto L = [&](int k) { const V3 V = toEyeOf(eyeAt(k), world); return envTrue(reflect(V3{ -V.x, -V.y, -V.z }, N)); };
    const float dL = L(30) - L(29);
    std::printf("the walk: L %.4f -> %.4f over %d frames (dL ~ %.5f a frame; the bare form's lag (N - 1) dL ~ %.4f)\n",
                L(0), L(frames), frames, dL, 9.0f * dL);

    // (a) NO NOISE: the carried mean IS L(V_now) to float rounding at every frame; the bare one lags
    {
        const Taps zero{ 0, 0, 0, 0 };
        JahEye now[1], before[1];
        float carried = L(0), bare = L(0), worstCarried = 0.0f, bareAt59 = 0.0f;
        for (int k = 1; k <= frames; ++k) {
            before[0] = eyeAt(k - 1);
            now[0] = eyeAt(k);
            float d;
            const V3 V = toEyeOf(now[0], world);
            const float lobe = roughLobeNow(N, V, V, false, zero, d);
            carried = historyStep(0, now, before, world, N, carried, true, blend, zero);
            bare = bareStep(bare, lobe, blend);
            worstCarried = std::fmax(worstCarried, std::fabs(carried - L(k)));
            if (k == 59)
                bareAt59 = bare - L(k);
        }
        std::printf("no noise: |carried - L(V_now)| worst %.3g; the bare form at k 59: %+.5f (predicted %+.5f)\n",
                    worstCarried, bareAt59, -9.0f * dL * (1.0f - std::pow(0.9f, 59.0f)));
        check(worstCarried < 1e-5f, "(a) no noise: the carried mean equals L(V_now) to float rounding at every walk frame");
        check(std::fabs(bareAt59) > 100.0f * 1e-5f,
              "(a) NEGATIVE CONTROL: the bare form lags L(V_now) at k 59 by (N - 1) dL (1 - 0.9^59) - the law can fail");
    }

    // (a) WITH THE TAPS' NOISE: 4,096 walks; at k 14 / 28 / 59 the carried mean's expectation is within 3 SE of L(V_now),
    // the bare form's is not (the rows gi.walk_rough.k14 / k28 / k59 read these frames)
    {
        const int walks = 4096;
        const int ks[3] = { 14, 28, 59 };
        std::vector<double> sumC(frames + 1, 0.0), sumC2(frames + 1, 0.0), sumB(frames + 1, 0.0), sumB2(frames + 1, 0.0);
        for (int w = 0; w < walks; ++w) {
            Pcg rng(uint64_t(w) + 1u);
            float carried = L(0), bare = L(0);
            JahEye now[1], before[1];
            for (int k = 1; k <= frames; ++k) {
                const Taps t{ 0.06f * rng.n(), 0.06f * rng.n(), 0.06f * rng.n(), 0.06f * rng.n() };
                before[0] = eyeAt(k - 1);
                now[0] = eyeAt(k);
                float d;
                const V3 V = toEyeOf(now[0], world);
                const float lobe = roughLobeNow(N, V, V, false, t, d);
                carried = historyStep(0, now, before, world, N, carried, true, blend, t);
                bare = bareStep(bare, lobe, blend);
                sumC[k] += carried; sumC2[k] += double(carried) * carried;
                sumB[k] += bare; sumB2[k] += double(bare) * bare;
            }
        }
        for (int k : ks) {
            const double mc = sumC[k] / walks, mb = sumB[k] / walks;
            const double seC = std::sqrt(std::fmax(sumC2[k] / walks - mc * mc, 0.0) / walks);
            const double seB = std::sqrt(std::fmax(sumB2[k] / walks - mb * mb, 0.0) / walks);
            const double eC = mc - L(k), eB = mb - L(k);
            std::printf("k %d: L %.5f; carried E %+.6f (3 SE %.6f); bare E %+.6f (3 SE %.6f, predicted %+.6f)\n", k, L(k),
                        eC, 3.0 * seC, eB, 3.0 * seB, -9.0 * dL * (1.0 - std::pow(0.9, k)));
            char what[200];
            std::snprintf(what, sizeof what, "(a) k %d: the carried mean's expectation is L(V_now) within 3 SE", k);
            check(std::fabs(eC) <= 3.0 * seC + 1e-6, what);
            std::snprintf(what, sizeof what, "(a) k %d: NEGATIVE CONTROL - the bare form's lags past 3 SE", k);
            check(std::fabs(eB) > 3.0 * seB + 1e-6, what);
        }
    }

    // (b) AT REST: the same camera, and a perspective eye that only turned - delta +0.0 bit-exact, the mean the bare mix's
    {
        const Taps t{ 0.013f, -0.021f, 0.008f, 0.017f };
        const float prev = 0.4321f;
        JahEye now[1] = { eyeAt(17) }, before[1] = { eyeAt(17) };
        const V3 V = toEyeOf(now[0], world);
        float d;
        const float lobe = roughLobeNow(N, V, toEyeOf(before[0], world), eyeViewMoved(now[0], before[0]), t, d);
        const float carried = historyStep(0, now, before, world, N, prev, true, blend, t);
        check(bits(d) == 0u, "(b) the same camera: delta is +0.0 bit-exact");
        check(bits(carried) == bits(bareStep(prev, lobe, blend)), "(b) the same camera: the carried mean IS the bare mix, bit for bit");
        before[0].fwd = V3{ 0.3f, -0.2f, -0.93f };   // turned, not moved
        const float turned = historyStep(0, now, before, world, N, prev, true, blend, t);
        check(!eyeViewMoved(now[0], before[0]) && bits(turned) == bits(carried),
              "(b) a perspective eye that only turned: the view at the point is the same - nothing evaluated, the same bits");
        before[0] = now[0];
        before[0].camPos.x = std::nextafter(before[0].camPos.x, 1.0f);
        float d1;
        roughLobeNow(N, V, toEyeOf(before[0], world), eyeViewMoved(now[0], before[0]), t, d1);
        check(eyeViewMoved(now[0], before[0]),
              "(b) CONTROL: a camera moved by one ulp is a moved view (the gate reads the bits, it is not always shut)");
    }

    // (c) PER EYE: eye 0's step is bit-identical whatever eye 1's previous camera; eye 1's moves with its own
    {
        const Taps t{ -0.011f, 0.019f, 0.004f, -0.023f };
        const float prev = 0.3777f;
        JahEye now[2] = { eyeAt(20), eyeAt(20) }, before[2] = { eyeAt(19), eyeAt(19) };
        now[1].camPos.x += 0.064f;    // the right eye, an IPD across
        before[1].camPos.x += 0.064f;
        const float e0 = historyStep(0, now, before, world, N, prev, true, blend, t);
        const float e1 = historyStep(1, now, before, world, N, prev, true, blend, t);
        before[1] = eyeAt(0);         // eye 1's previous camera far away
        const float e0b = historyStep(0, now, before, world, N, prev, true, blend, t);
        const float e1b = historyStep(1, now, before, world, N, prev, true, blend, t);
        check(bits(e0) == bits(e0b), "(c) eye 0's carried mean is bit-identical whatever eye 1's previous camera was");
        check(bits(e1) != bits(e1b), "(c) CONTROL: eye 1's carried mean moves with ITS OWN previous camera");
        before[0] = eyeAt(0);
        const float e1c = historyStep(1, now, before, world, N, prev, true, blend, t);
        check(bits(e1c) == bits(e1b), "(c) eye 1's carried mean is bit-identical whatever eye 0's previous camera was");
    }

    std::printf("%s\n", gFail ? "engine.rough_lobe_transport: FAILED" : "engine.rough_lobe_transport: PASSED");
    return gFail ? 1 : 0;
}
