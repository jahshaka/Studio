// gi.field_oracle (PHOTON-7 stage 5, 5-4; SPECS/PHOTON_7_SPEC.md stage 5's oracle) - THE FIELD REFINES ONLY WHAT
// CHANGED, AND WHAT IT KEEPS IS STILL RIGHT: a lamp step in the leak room, every probe of the irradiance field checked
// against an ORACLE - a whole re-solve of the same final state (the GI torn down and built again: every voxel injected
// and converged from scratch, every probe integrated fresh).
//
// THE FIXTURE: the leak room (tests/support leakroom::build, 0.3 m walls, its green lamp inside and its red lamp outside),
// the chain at High, the field ON, the rays, the gather and the cards off, the variance stop OFF (gi.fieldStopOff: every
// probe to the target, so a probe's estimate is the mean of the same 25 rotations - a function of the light it read -
// and two estimates of the same light are bit-equal).
// THE STEP (A): the inside lamp 3 -> 6 at the field's rest (it moves every probe: the whole room is its light), run to
// the rest again (GiStatus::giAtRest held 30 frames).
// THE LAWS:
//   (1) EVERY PROBE MATCHES THE ORACLE: every interior irradiance texel of every probe within ONE CODE of the oracle's
//       (1 % of the oracle's value per channel - a code is per texel, the world caches' one definition - over the
//       store's floor) after ONE STEP of the RGBA16F atlas (half floats: 2^-10 of the value) - the probes the step
//       restarted and the probes it kept alike (what they kept is still the answer: the marks missed nothing that
//       moved a code);
//   (2) THE STEP RESTARTED PROBES and every probe ends at the target, in the step's field and the oracle's; the
//       re-marks (restarts past one a probe: what a probe read moved a code again after its own restart) printed;
//   (3) TWO SEALED ROOMS (a second fixture): 2 m apart inside the field (each 1.6 m, 0.2 m walls, its own lamp), the
//       first's lamp stepped 1 -> 2; (4) THE OPEN STEP (a third): a floor lit by a wide lamp, a small lamp (range 0.5 m,
//       0.05 -> 0.1) near one face of the field. THE TRUTH FIRST, per probe: moved over a code (before against the
//       oracle; the cones leak through walls at coarse levels, VOXEL-LEAK-1). A probe is RESTARTED when its tile is not
//       bit for bit as before the step (the stop off: a probe at the target is never touched otherwise). THE BARS (a
//       conservative bound cannot promise to keep what held, the lead's ruling): (i) SAFETY - every probe the truth moved
//       is restarted, every probe within a code of its oracle; (ii) NO MARK BEYOND THE LAW - every restart of the step
//       came from the moves (none from a region or a whole-field mark). PRINTED: THE LAW'S SLACK - the restarts of
//       probes the truth held, over the probes it held (the second room's: A's leak moving B's own bricks, VOXEL-LEAK-1;
//       the open field's: the accumulation sums every small move of the bounce's convergence).
//   (5) A PROBE CASCADE 0 DOES NOT HOLD STARTS ITS MARCH IN CASCADE 1 (PHOTON-7 stage 5, 5-5: the field's window steps
//       with the eye, decoupled from cascade 0's commits, so it may lead cascade 0's box; a fourth fixture): a lamp-lit
//       floor with no sky and no ambient - the floor's light is the only light - the eye at the origin (cascade 0's box
//       [-5, 5] m on x at High) and the field PINNED (GiPlacementPin) over x [6, 16] m: its probes over the floor at
//       x 5.5 .. 9.5 m stand outside cascade 0's box and inside cascade 1's. Each of them reads the floor's light (a
//       march that did not start in cascade 1 reads nothing: there is nothing else to read); PRINTED: their mean
//       against the same probes integrated once the eye's move to x = 8 m has placed cascade 0 over them.
// THE MARKS' VISIBILITY (Jahshaka/FieldAccumulate: the probe's depth moments - each ray's march's end, the support of
// its radiance - bound the share of its rays a moved box can reach) is what (3) and (4) read.
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
float halfToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16, exp = (h >> 10) & 0x1Fu, man = h & 0x3FFu;
    uint32_t bits;
    if (exp == 0u) {
        if (!man) bits = sign;
        else {   // subnormal
            float v = std::ldexp(float(man), -24);
            return sign ? -v : v;
        }
    } else if (exp == 31u) bits = sign | 0x7F800000u | (man << 13);
    else bits = sign | ((exp + 112u) << 23) | (man << 13);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
/// Every probe slot's interior texels (rgb) and its count (alpha), from an RGBA16F atlas.
struct Probes {
    unsigned count = 0, res = 0;
    unsigned dims[3] = { 0u, 0u, 0u }, offset[3] = { 0u, 0u, 0u };   // the probes per axis, the window's slot offset
    std::vector<float> rgb;      // count x res^2 x 3
    std::vector<float> samples;  // count
};
bool readProbes(Scene *s, Probes &out) {
    GiFieldAtlas a;
    if (!s->giFieldAtlas(a) || !a.available || a.irradBytesPerTexel != 8u) return false;
    out.count = a.probes[0] * a.probes[1] * a.probes[2];
    for (int k = 0; k < 3; ++k) { out.dims[k] = a.probes[k]; out.offset[k] = a.windowOffset[k]; }
    out.res = a.irradBordered - 2u;
    out.rgb.assign(size_t(out.count) * out.res * out.res * 3u, 0.0f);
    out.samples.assign(out.count, 0.0f);
    for (unsigned p = 0; p < out.count; ++p) {
        // the generation job's tile rule (jahIfdTileOrigin): bordered tiles in probe order, row-major over the atlas
        const unsigned x0 = (p * a.irradBordered) % a.irradWidth + 1u;
        const unsigned y0 = ((p * a.irradBordered) / a.irradWidth) * a.irradBordered + 1u;
        for (unsigned t = 0; t < out.res * out.res; ++t) {
            const unsigned x = x0 + t % out.res, y = y0 + t / out.res;
            const uint16_t *h = reinterpret_cast<const uint16_t *>(&a.irradiance[(size_t(y) * a.irradWidth + x) * 8u]);
            for (int c = 0; c < 3; ++c) out.rgb[(size_t(p) * out.res * out.res + t) * 3u + c] = halfToFloat(h[c]);
            if (t == 0u) out.samples[p] = halfToFloat(h[3]);
        }
    }
    return true;
}
/// A probe's worst texel, in codes of b, after one step of the half-float store.
double probeCodes(const Probes &a, const Probes &b, unsigned p) {
    double worst = 0.0;
    for (unsigned t = 0; t < a.res * a.res; ++t)
        for (int c = 0; c < 3; ++c) {
            const size_t i = (size_t(p) * a.res * a.res + t) * 3u + c;
            const double o = std::max(double(b.rgb[i]), 5.9604645e-6);
            const double step = std::ldexp(o, -10);
            worst = std::max(worst, std::max(0.0, std::fabs(double(a.rgb[i]) - double(b.rgb[i])) - step) / (0.01 * o));
        }
    return worst;
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-field-oracle-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("field_oracle", 128, 128, Colour(0, 0, 0));
    Scene *scene = e->createScene("field_oracle");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    enginetest::leakroom::Room room = enginetest::leakroom::build(scene, view, 0.3f);
    e->setArm("gi.fieldStopOff", 1.0);   // every probe to the target (the header)
    run(1);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::On;
    gi.gather = GiToggle::Off;
    gi.cards = false;
    gi.bounces = true;
    const auto settle = [&]() {
        int quiet = 0, f = 0;
        for (; f < 1500 && quiet < 30; ++f) {
            run(1);
            quiet = scene->published().gi.giAtRest ? quiet + 1 : 0;
        }
        return quiet >= 30 ? f - quiet : -1;
    };
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    const int rest0 = settle();
    Probes before, step, oracle;
    const bool r0 = readProbes(scene, before);
    const GiStatus g0 = scene->published().gi;
    // THE STEP
    LightDesc li;
    li.type = LightType::Point;
    li.colour = Colour(0.0f, 1.0f, 0.0f);
    li.intensity = 6.0f;
    li.range = 14.0f;
    li.castShadows = true;
    scene->setLight(room.inside, li);
    const int rest1 = settle();
    const bool r1 = readProbes(scene, step);
    const GiStatus g1 = scene->published().gi;
    // THE ORACLE
    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    scene->setGlobalIllumination(gi);
    e->waitForAsyncShaders();
    const int rest2 = settle();
    const bool r2 = readProbes(scene, oracle);
    CHECK(rest0 >= 0 && rest1 >= 0 && rest2 >= 0 && r0 && r1 && r2 && step.count == oracle.count &&
              before.count == oracle.count && oracle.count > 0,
          "the field comes to rest three times (before, the step, the oracle) and its atlas reads back");
    if (failures) return 1;
    size_t over = 0, moved = 0, atTarget = 0, oracleAtTarget = 0;
    double worst = 0.0, worstMoved = 0.0;
    for (unsigned p = 0; p < oracle.count; ++p) {
        const double c = probeCodes(step, oracle, p);
        worst = std::max(worst, c);
        over += c > 1.0 ? 1u : 0u;
        const double truth = probeCodes(before, oracle, p);
        if (truth > 1.0) ++moved;
        worstMoved = std::max(worstMoved, truth);
        atTarget += step.samples[p] >= float(g1.ifdTargetSamples) ? 1u : 0u;
        oracleAtTarget += oracle.samples[p] >= float(g1.ifdTargetSamples) ? 1u : 0u;
    }
    const unsigned long long restarts = g1.ifdRestarts - g0.ifdRestarts;
    const unsigned long long byMoves = g1.ifdMarkedByMoves - g0.ifdMarkedByMoves;
    std::printf("   %u probes (target %u): at rest in %d / %d / %d frames | the step restarted %llu (marked by the "
                "moves %llu, by regions %llu, whole-field marks %llu) | the truth: %zu probes moved over a code (worst "
                "%.2f codes) | the step against the oracle: worst %.3f codes, %zu probes over a code | at the target: "
                "the step %zu, the oracle %zu\n",
                oracle.count, g1.ifdTargetSamples, rest0, rest1, rest2, restarts, byMoves,
                g1.ifdMarkedByRegion - g0.ifdMarkedByRegion, g1.ifdMarkAlls - g0.ifdMarkAlls, moved, worstMoved, worst,
                over, atTarget, oracleAtTarget);
    CHECK(over == 0u, "(1) EVERY PROBE MATCHES THE ORACLE: within one code after the atlas's step");
    CHECK(restarts > 0u && atTarget == oracle.count && oracleAtTarget == oracle.count,
          "(2) THE STEP RESTARTED PROBES, and every probe ends at the target in both fields");

    std::printf("   the re-marks: %llu restarts past one a probe (%.0f %% of the field)\n",
                restarts > oracle.count ? restarts - oracle.count : 0ull,
                100.0 * double(restarts > oracle.count ? restarts - oracle.count : 0ull) / double(oracle.count));
    scene->setGlobalIllumination(off);
    run(2);
    e->destroyView(view);
    e->destroyScene(scene);
    run(1);

    // (3) TWO SEALED ROOMS 2 m APART INSIDE THE FIELD, the first's lamp stepped
    View *v2 = e->createOffscreenView("field_oracle_rooms", 128, 128, Colour(0, 0, 0));
    Scene *s2 = e->createScene("field_oracle_rooms");
    if (!v2 || !s2) { std::printf("FAIL: the rooms' view/scene\n"); return 1; }
    v2->setScene(s2);
    s2->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const float kSide = 1.6f, kWall = 0.2f, kGap = 2.0f;
    const float cx[2] = { -(kGap * 0.5f + kSide * 0.5f), kGap * 0.5f + kSide * 0.5f };
    NodeId lampN[2] = { 0, 0 };
    LightDesc rl[2];
    for (int k = 0; k < 2; ++k) {
        const Colour white(0.8f, 0.8f, 0.8f);
        const float c = cx[k], h = kSide * 0.5f + kWall * 0.5f, span = kSide + 2.0f * kWall, y = 1.0f;
        enginetest::leakroom::addSlab(s2, white, Vec3(c, y - h, 0), Vec3(span, kWall, span));
        enginetest::leakroom::addSlab(s2, white, Vec3(c, y + h, 0), Vec3(span, kWall, span));
        enginetest::leakroom::addSlab(s2, white, Vec3(c - h, y, 0), Vec3(kWall, kSide, span));
        enginetest::leakroom::addSlab(s2, white, Vec3(c + h, y, 0), Vec3(kWall, kSide, span));
        enginetest::leakroom::addSlab(s2, white, Vec3(c, y, -h), Vec3(kSide, kSide, kWall));
        enginetest::leakroom::addSlab(s2, white, Vec3(c, y, h), Vec3(kSide, kSide, kWall));
        lampN[k] = s2->createNode();
        s2->setNodeTransform(lampN[k], Vec3(c, y + 0.4f, 0.0f), Quat(), Vec3(1, 1, 1));
        rl[k].type = LightType::Point;
        rl[k].colour = Colour(1.0f, 1.0f, 1.0f);
        rl[k].intensity = 1.0f;
        rl[k].range = 1.5f;
        rl[k].castShadows = true;
        s2->setLight(lampN[k], rl[k]);
    }
    enginetest::testCameraLookAt(v2, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, -1.0f));
    const auto settle2 = [&]() {
        int quiet = 0, f = 0;
        for (; f < 1500 && quiet < 30; ++f) {
            run(1);
            quiet = s2->published().gi.giAtRest ? quiet + 1 : 0;
        }
        return quiet >= 30 ? f - quiet : -1;
    };
    if (!s2->setGlobalIllumination(gi)) { std::printf("FAIL: the rooms' GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    const int rest3 = settle2();
    Probes rb, rs, ro;
    const bool q0 = readProbes(s2, rb);
    const GiStatus k0 = s2->published().gi;
    rl[0].intensity = 2.0f;
    s2->setLight(lampN[0], rl[0]);
    const int rest4 = settle2();
    const bool q1 = readProbes(s2, rs);
    const GiStatus k1 = s2->published().gi;
    s2->setGlobalIllumination(off);
    run(2);
    s2->setGlobalIllumination(gi);
    e->waitForAsyncShaders();
    const int rest5 = settle2();
    const bool q2 = readProbes(s2, ro);
    // each slot's probe in the world: the field's volume (ifdMin..ifdMax, as asked) enlarged by a probe block a side
    size_t inB = 0, keptB = 0, over3 = 0, movedB = 0, movedKeptB = 0, held3 = 0, heldRestarted3 = 0, movedKept3 = 0;
    double worst3 = 0.0;
    GiFieldAtlas fa;
    const bool fok = s2->giFieldAtlas(fa);
    if (q0 && q1 && q2 && fok) {
        const Vec3 lo = k1.ifdMin, hi = k1.ifdMax;
        const float sz[3] = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z }, o0[3] = { lo.x, lo.y, lo.z };
        for (unsigned p = 0; p < ro.count; ++p) {
            const double c = probeCodes(rs, ro, p);
            worst3 = std::max(worst3, c);
            over3 += c > 1.0 ? 1u : 0u;
            {
                bool same = true;
                for (unsigned t = 0; t < rb.res * rb.res * 3u && same; ++t)
                    same = rb.rgb[size_t(p) * rb.res * rb.res * 3u + t] == rs.rgb[size_t(p) * rb.res * rb.res * 3u + t];
                const bool moved = probeCodes(rb, ro, p) > 1.0;
                held3 += moved ? 0u : 1u;
                heldRestarted3 += !moved && !same ? 1u : 0u;
                movedKept3 += moved && same ? 1u : 0u;
            }
            const unsigned sl[3] = { p % fa.probes[0], (p / fa.probes[0]) % fa.probes[1], p / (fa.probes[0] * fa.probes[1]) };
            float wp[3];
            for (int a = 0; a < 3; ++a) {
                const unsigned n = fa.probes[a];
                const unsigned local = (sl[a] + n - fa.windowOffset[a] % n) & (n - 1u);
                const float block = sz[a] / float(n), spacing = (sz[a] + 2.0f * block) / float(n);
                wp[a] = o0[a] - block + float(local) * spacing;
            }
            const float bx = cx[1], half = kSide * 0.5f;
            if (wp[0] > bx - half && wp[0] < bx + half && wp[1] > 1.0f - half && wp[1] < 1.0f + half && wp[2] > -half &&
                wp[2] < half) {
                ++inB;
                bool same = true;
                for (unsigned t = 0; t < rb.res * rb.res * 3u && same; ++t)
                    same = rb.rgb[size_t(p) * rb.res * rb.res * 3u + t] == rs.rgb[size_t(p) * rb.res * rb.res * 3u + t];
                keptB += same ? 1u : 0u;
                const bool moved = probeCodes(rb, ro, p) > 1.0;   // THE TRUTH: before the step against the oracle
                movedB += moved ? 1u : 0u;
                movedKeptB += moved && same ? 1u : 0u;
            }
        }
    }
    const unsigned long long restarts3 = k1.ifdRestarts - k0.ifdRestarts, byMoves3 = k1.ifdMarkedByMoves - k0.ifdMarkedByMoves;
    std::printf("   (3) two rooms %.1f m apart: at rest in %d / %d / %d frames | THE TRUTH: %zu of the second room's %zu probes "
                "moved over a code (%zu of the field's %u held) | the step restarted %llu: by the moves %llu, by regions %llu, "
                "whole-field marks %llu | the second room kept %zu | THE LAW'S SLACK: %zu of the %zu held probes restarted "
                "(the second room's %zu of %zu) | the truth's moved left unrestarted %zu | against the oracle: worst %.3f "
                "codes, %zu over a code\n",
                kGap, rest3, rest4, rest5, movedB, inB, held3, ro.count, restarts3, byMoves3,
                k1.ifdMarkedByRegion - k0.ifdMarkedByRegion, k1.ifdMarkAlls - k0.ifdMarkAlls, keptB, heldRestarted3, held3,
                inB - keptB - std::min(inB - keptB, movedB), inB - movedB, movedKept3, worst3, over3);
    CHECK(rest3 >= 0 && rest4 >= 0 && rest5 >= 0 && q0 && q1 && q2 && inB > 0 && over3 == 0u && movedKept3 == 0u,
          "(3)(i) SAFETY: every probe the truth moved restarted, every probe within its oracle");
    CHECK(restarts3 > 0u && k1.ifdMarkedByRegion == k0.ifdMarkedByRegion && k1.ifdMarkAlls == k0.ifdMarkAlls &&
              byMoves3 == restarts3,
          "(3)(ii) NO MARK BEYOND THE LAW: every restart from the moves");
    s2->setGlobalIllumination(off);
    run(2);
    e->destroyView(v2);
    e->destroyScene(s2);
    run(1);

    // (4) THE OPEN KEEP
    View *v3 = e->createOffscreenView("field_oracle_open", 128, 128, Colour(0, 0, 0));
    Scene *s3 = e->createScene("field_oracle_open");
    if (!v3 || !s3) { std::printf("FAIL: the open view/scene\n"); return 1; }
    v3->setScene(s3);
    s3->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    enginetest::leakroom::addSlab(s3, Colour(0.6f, 0.6f, 0.6f), Vec3(0, -0.1f, 0), Vec3(12.0f, 0.2f, 12.0f));
    const NodeId wide = s3->createNode(), small = s3->createNode();
    s3->setNodeTransform(wide, Vec3(0.0f, 4.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    s3->setNodeTransform(small, Vec3(-2.2f, 0.3f, 0.0f), Quat(), Vec3(1, 1, 1));
    LightDesc wl, sl;
    wl.type = sl.type = LightType::Point;
    wl.colour = sl.colour = Colour(1.0f, 1.0f, 1.0f);
    wl.intensity = 20.0f;
    wl.range = 20.0f;
    sl.intensity = 0.05f;
    sl.range = 0.5f;
    wl.castShadows = sl.castShadows = true;
    s3->setLight(wide, wl);
    s3->setLight(small, sl);
    enginetest::testCameraLookAt(v3, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.5f, -1.0f));
    const auto settle3 = [&]() {
        int quiet = 0, f = 0;
        for (; f < 1500 && quiet < 30; ++f) {
            run(1);
            quiet = s3->published().gi.giAtRest ? quiet + 1 : 0;
        }
        return quiet >= 30 ? f - quiet : -1;
    };
    if (!s3->setGlobalIllumination(gi)) { std::printf("FAIL: the open GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    const int rest6 = settle3();
    Probes ob, os, oo;
    const bool u0 = readProbes(s3, ob);
    const GiStatus m0 = s3->published().gi;
    sl.intensity = 0.1f;
    s3->setLight(small, sl);
    const int rest7 = settle3();
    const bool u1 = readProbes(s3, os);
    const GiStatus m1 = s3->published().gi;
    s3->setGlobalIllumination(off);
    run(2);
    s3->setGlobalIllumination(gi);
    e->waitForAsyncShaders();
    const int rest8 = settle3();
    const bool u2 = readProbes(s3, oo);
    size_t far = 0, farKept = 0, over4 = 0, farMoved = 0, held4 = 0, heldRestarted4 = 0, movedKept4 = 0;
    double worst4 = 0.0;
    GiFieldAtlas oa;
    if (u0 && u1 && u2 && s3->giFieldAtlas(oa)) {
        const Vec3 lo = m1.ifdMin, hi = m1.ifdMax;
        const float sz[3] = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z }, o0[3] = { lo.x, lo.y, lo.z };
        for (unsigned p = 0; p < oo.count; ++p) {
            const double c = probeCodes(os, oo, p);
            worst4 = std::max(worst4, c);
            over4 += c > 1.0 ? 1u : 0u;
            {
                bool same = true;
                for (unsigned t = 0; t < ob.res * ob.res * 3u && same; ++t)
                    same = ob.rgb[size_t(p) * ob.res * ob.res * 3u + t] == os.rgb[size_t(p) * ob.res * ob.res * 3u + t];
                const bool moved = probeCodes(ob, oo, p) > 1.0;
                held4 += moved ? 0u : 1u;
                heldRestarted4 += !moved && !same ? 1u : 0u;
                movedKept4 += moved && same ? 1u : 0u;
            }
            const unsigned sl3[3] = { p % oa.probes[0], (p / oa.probes[0]) % oa.probes[1], p / (oa.probes[0] * oa.probes[1]) };
            float wp[3];
            for (int a = 0; a < 3; ++a) {
                const unsigned n = oa.probes[a];
                const unsigned local = (sl3[a] + n - oa.windowOffset[a] % n) & (n - 1u);
                const float block = sz[a] / float(n), spacing = (sz[a] + 2.0f * block) / float(n);
                wp[a] = o0[a] - block + float(local) * spacing;
            }
            const float dx = wp[0] + 2.2f, dy = wp[1] - 0.3f, dz = wp[2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) < 4.0f || wp[1] < 0.05f) continue;   // (above the floor's top)
            ++far;
            bool same = true;
            for (unsigned t = 0; t < ob.res * ob.res * 3u && same; ++t)
                same = ob.rgb[size_t(p) * ob.res * ob.res * 3u + t] == os.rgb[size_t(p) * ob.res * ob.res * 3u + t];
            farKept += same ? 1u : 0u;
            farMoved += probeCodes(ob, oo, p) > 1.0 ? 1u : 0u;
        }
    }
    const unsigned long long restarts4 = m1.ifdRestarts - m0.ifdRestarts, byMoves4 = m1.ifdMarkedByMoves - m0.ifdMarkedByMoves;
    std::printf("   (4) the open step: at rest in %d / %d / %d frames | THE TRUTH: %zu of the %zu probes 4 m and more from the "
                "small lamp moved over a code (%zu of the field's %u held) | the step restarted %llu: by the moves %llu, by "
                "regions %llu, whole-field marks %llu | THE LAW'S SLACK: %zu of the %zu held probes restarted (4 m and more: "
                "%zu kept of %zu) | the truth's moved left unrestarted %zu | against the oracle: worst %.3f codes, %zu over a "
                "code\n", rest6, rest7, rest8, farMoved, far, held4, oo.count, restarts4, byMoves4,
                m1.ifdMarkedByRegion - m0.ifdMarkedByRegion, m1.ifdMarkAlls - m0.ifdMarkAlls, heldRestarted4, held4, farKept,
                far, movedKept4, worst4, over4);
    CHECK(rest6 >= 0 && rest7 >= 0 && rest8 >= 0 && u0 && u1 && u2 && far > 0 && over4 == 0u && movedKept4 == 0u,
          "(4)(i) SAFETY: every probe the truth moved restarted, every probe within its oracle");
    CHECK(restarts4 > 0u && m1.ifdMarkedByRegion == m0.ifdMarkedByRegion && m1.ifdMarkAlls == m0.ifdMarkAlls &&
              byMoves4 == restarts4,
          "(4)(ii) NO MARK BEYOND THE LAW: every restart from the moves");
    s3->setGlobalIllumination(off);
    run(2);

    // (5) THE PROBES OUTSIDE CASCADE 0
    {
        View *v5 = e->createOffscreenView("field_oracle_c1", 128, 128, Colour(0, 0, 0));
        Scene *s5 = e->createScene("field_oracle_c1");
        if (!v5 || !s5) { std::printf("FAIL: the c1 view/scene\n"); return 1; }
        v5->setScene(s5);
        s5->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        enginetest::leakroom::addSlab(s5, Colour(0.6f, 0.6f, 0.6f), Vec3(0, -0.1f, 0), Vec3(40.0f, 0.2f, 40.0f));
        const NodeId lamp5 = s5->createNode();
        s5->setNodeTransform(lamp5, Vec3(6.0f, 4.0f, 0.0f), Quat(), Vec3(1, 1, 1));
        LightDesc l5;
        l5.type = LightType::Point;
        l5.colour = Colour(1.0f, 1.0f, 1.0f);
        l5.intensity = 20.0f;
        l5.range = 30.0f;
        l5.castShadows = true;
        s5->setLight(lamp5, l5);
        enginetest::testCameraLookAt(v5, Vec3(0.0f, 1.0f, 0.0f), Vec3(1.0f, 0.8f, 0.0f));
        GiPlacementPin pin;
        pin.on = true;
        pin.field = true;
        pin.fieldOrigin = Vec3(6.0f, -2.0f, -5.0f);
        s5->setGiPlacementPin(pin);
        const auto settle5 = [&]() {
            int quiet = 0, f = 0;
            for (; f < 1500 && quiet < 30; ++f) {
                run(1);
                quiet = s5->published().gi.giAtRest ? quiet + 1 : 0;
            }
            return quiet >= 30 ? f - quiet : -1;
        };
        if (!s5->setGlobalIllumination(gi)) { std::printf("FAIL: the c1 GI builds\n"); return 1; }
        e->waitForAsyncShaders();
        const int rest8 = settle5();
        Probes c1, c0;
        const bool w0 = readProbes(s5, c1);
        const GiStatus n0 = s5->published().gi;
        // the probes over the floor outside cascade 0 and inside cascade 1 (the window's offset is 0: pinned, built there)
        std::vector<unsigned> outside;
        bool placed = n0.cascades.size() >= 2u && c1.dims[0] > 0u;
        double c0Hi = 0.0;
        if (placed) {
            const GiStatus::CascadeStatus &k0 = n0.cascades[0], &k1 = n0.cascades[1];
            c0Hi = double(k0.centre.x) + double(k0.halfSize);
            const double sz[3] = { double(n0.ifdMax.x - n0.ifdMin.x), double(n0.ifdMax.y - n0.ifdMin.y),
                                   double(n0.ifdMax.z - n0.ifdMin.z) };
            const double lo[3] = { double(n0.ifdMin.x), double(n0.ifdMin.y), double(n0.ifdMin.z) };
            for (unsigned p = 0; p < c1.count; ++p) {
                const unsigned slot[3] = { p % c1.dims[0], (p / c1.dims[0]) % c1.dims[1], p / (c1.dims[0] * c1.dims[1]) };
                double w[3];
                for (int a = 0; a < 3; ++a) {   // window-local i at the enlarged corner plus i spacings (setFieldVolume)
                    const unsigned i = (slot[a] + c1.dims[a] - c1.offset[a]) % c1.dims[a];
                    const double n = double(c1.dims[a]), block = sz[a] / n, sp = (sz[a] + 2.0 * block) / n;
                    w[a] = lo[a] - block + double(i) * sp;
                }
                if (w[0] > c0Hi + 0.5 && w[0] < double(k1.centre.x) + double(k1.halfSize) - 0.5 && w[1] > 0.3 &&
                    w[1] < 3.0 && std::fabs(w[2]) < 3.0)
                    outside.push_back(p);
            }
        }
        size_t dark = 0;
        double sum1 = 0.0;
        for (unsigned p : outside) {
            double brightest = 0.0;
            for (unsigned t = 0; t < c1.res * c1.res; ++t)
                for (int c = 0; c < 3; ++c) brightest = std::max(brightest, double(c1.rgb[(size_t(p) * c1.res * c1.res + t) * 3u + c]));
            dark += brightest > 0.0 && c1.samples[p] >= 1.0f ? 0u : 1u;
            for (unsigned t = 0; t < c1.res * c1.res; ++t) sum1 += c1.rgb[(size_t(p) * c1.res * c1.res + t) * 3u + 1u];
        }
        // the same probes once cascade 0 holds them (the eye at x = 8: cascade 0 re-placed over them; the field pinned)
        enginetest::testCameraLookAt(v5, Vec3(8.0f, 1.0f, 0.0f), Vec3(9.0f, 0.8f, 0.0f));
        const int rest9 = settle5();
        const bool w1 = readProbes(s5, c0);
        const GiStatus n1 = s5->published().gi;
        double sum0 = 0.0;
        for (unsigned p : outside)
            for (unsigned t = 0; t < c0.res * c0.res; ++t) sum0 += c0.rgb[(size_t(p) * c0.res * c0.res + t) * 3u + 1u];
        std::printf("   (5) probes outside cascade 0 (its box to x %.2f m): at rest in %d frames | %zu probes over the floor at "
                    "x %.1f .. 9.5 m, %zu reading no light | their mean against cascade 0 holding them (at rest in %d frames, "
                    "cascade 0 to x %.2f m): %.4f\n", c0Hi, rest8, outside.size(), c0Hi + 0.5, dark, rest9,
                    n1.cascades.empty() ? 0.0 : double(n1.cascades[0].centre.x + n1.cascades[0].halfSize),
                    sum0 > 0.0 ? sum1 / sum0 : 0.0);
        CHECK(rest8 >= 0 && rest9 >= 0 && w0 && w1 && placed && !outside.empty() && dark == 0u,
              "(5) A PROBE CASCADE 0 DOES NOT HOLD STARTS ITS MARCH IN CASCADE 1: every one over the lit floor reads its light");
        s5->setGiPlacementPin(GiPlacementPin());
        s5->setGlobalIllumination(off);
        run(2);
    }
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
