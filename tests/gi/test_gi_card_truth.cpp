// gi.card_truth (PHOTON-7 stage 10-3, R12: the row re-pointed at physics) - THE TWO WORLD CACHES AGAINST THE ANALYTIC
// DIRECT AND A BRUTE-FORCE HEMISPHERE. A lamp-lit wall in a black world, read by a small reader patch: what a gather ray
// returns from the wall, whichever cache answers it (its CARD, or the CASCADES with the hit's own direct), against the
// wall's exact radiance toward the ray; and what the patch's card GATHERS of the wall, against the hemisphere's exact
// integral. (It replaced a table that held both caches against the card estimator itself at 128 rays: measured against
// physics here, the voxel route's "-8/-6/-3 % bias" of that table and R10's "+3 to +17 % at grazing" were the
// references' - the card's 8-bit kD and a reference missing the lobe's view term and the specular integral.)
// THE FIXTURE: a wall 4 x 8 m (its +x face at x = 0.2: albedo 0.6, roughness 1, the default dielectric's lobe) lit by a
// point lamp (I 4 at (1.5, 2, 0), range 12, source 0.05 m); the reader, a 0.2 m matte patch (albedo 0.1) at x = 3.2
// facing the wall, shaded from the lamp by a matte black blocker (0.24 m square at x = 2.0-2.1); the sky black. The
// wall's hemisphere sees the void, the blocker's dark side and the patch (its feedback onto the wall 0.004 % of the
// wall's radiance: under the store's quantum), so the wall's radiance is the lamp's direct alone, in closed form:
//     L(P -> V) = I mu atten(d) [ rho Dd(L, V) + F0 A(mu) + w B(mu) ]
// - HlmsPbs's BRDF_Default diffuse lobe toward V (Disney at N.L, N.V, V.H, its energy factor) and, for a GATHER ray,
// the specular lobe's integral (the lead's ruling: jah_rq_hit_direct.glsl's JAH_HIT_LOBE_INTEGRAL), A and B integrated
// here from the GGX lobe (enginetesthelpers.h), never read from the engine's tables. The ray tier at High.
// THE BAR IS THE STORE'S QUANTUM q (the lead's ruling, R12): one half-float step at the reference R,
// 2^(floor(log2 R) - 10) - every store and readback here is RGBA16F.
// THE LAWS:
//   (T1) THE DIRECT A GATHER RAY RETURNS: the screen gather frozen with its ray readback (gather.freezeFrame +
//        gather.hitsReadback), every ray from the patch that hit the wall's +x face, its V known to the patch's size
//        (the ray's origin on the patch's plane at the hit's distance, the circle's point nearest the patch's centre);
//        in 0.5 m bands of the distance from the lamp's foot (N.V 0.99 down to 0.62), each band's mean against the
//        mean of L(P -> V) within q of it. (a) THE CASCADES' ANSWER (a scene of its own, the wall uncarded: the hit's
//        own direct, jahHitDirect) - measured -0.03 % in every band (0.0-0.5 q). (b) THE CARD'S ANSWER, against L at
//        the TEXEL's centre (the read is texel-exact: 1/64 m texels on this face) - DECLARED RED (the lead's ruling,
//        2026-10-27): -0.06 to -0.18 % in the bands (1-2 q; 0.4 of an 8-bit code) inside the card read path - its
//        texels hold the analytic within q (-0.03 %), the 8-bit normal layer and its stored tilt measured out; on the
//        owner's review list, its next step the card lights' lamp terms against jahHitDirect's. RED before R12's
//        format fix: +0.6 % in every band - the albedo layer stored kD = albedo / pi in a byte (0.6 read +0.61 %, a
//        0.03 albedo -18 %).
//   (T2) WHAT THE PATCH'S CARD GATHERS (the Gather layer at its centre texel: the mean of its N fixed cosine rays,
//        jah_ray_set.glsl's Vogel set in jahConeBasisWorld - the cards' indirect gather through the TLAS) against the
//        SAME N directions traced on the CPU through the fixture's analytic boxes with L(P -> V) at each hit: within q,
//        at the tier's 16 rays and at the truth door's 128 (gi.truthRays, set before its scene is built: a cache at
//        rest re-gathers nothing). (a) the cascades answering (the uncarded wall) - measured 0.13 q and 0.29 q; (b) the
//        wall's card answering - DECLARED RED with (T1b), its one cause: 0.72 q at 16 rays, 1.18 q at 128. The
//        patch's cards held resident (cards.force: nothing else reads them - the door now offers its node whatever its
//        demand). PRINTED beside it: the same quantity by a BRUTE-FORCE HEMISPHERE (512 x 512 stratified cosine
//        directions on the CPU), the fixed set's own quadrature error - +9.0 % at 16 rays, -1.3 % at 128 here (the
//        one set every surface shares: the optimisation list's stratified-sides item). THE DEVIATION, the lead's
//        acceptance: the brute-force hemisphere is traced on the CPU against the fixture's analytic geometry, not
//        through the TLAS - no engine code is in the truth.
// A fixture (the engine alone, one 192 x 192 offscreen view; 10 s budget). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/lightfalloff.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
const double kAlb = 0.6, kI = 4.0, kRange = 12.0, kSrc = 0.05, kHalf = 0.1, kWallX = 0.2, kPatchX = 3.2;
const double kLamp[3] = { 1.5, 2.0, 0.0 };
const double kTexel = 1.0 / 64.0;   // the wall face's card texel (a premise, checked)
double gF0 = 0.04, gW = 1.0;        // the wall's stored lobe (read back, checked)

double quantumOf(double R) { return R > 0.0 ? std::ldexp(1.0, int(std::floor(std::log2(R))) - 10) : 0.0; }
double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void norm3(double a[3])
{
    const double l = std::sqrt(dot3(a, a));
    for (int i = 0; i < 3; ++i) a[i] /= l;
}

/// HlmsPbs's diffuse lobe (JahBrdf_piece_all.any's jahDisneyDiffuse), restated
double disney(double NdotL, double NdotV, double VdotH, double r)
{
    const double ef = 1.0 + (1.0 / 1.51 - 1.0) * r, fd90 = 0.5 * r + 2.0 * VdotH * VdotH * r;
    return (1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotL, 5.0)) * (1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotV, 5.0)) * ef;
}

/// THE WALL'S EXACT RADIANCE from P (on its +x face) toward the unit V, as a gather ray takes it
double wallRadiance(const double P[3], const double V[3])
{
    double L[3] = { kLamp[0] - P[0], kLamp[1] - P[1], kLamp[2] - P[2] };
    const double d = std::sqrt(dot3(L, L));
    norm3(L);
    const double mu = std::max(0.0, L[0]);
    if (!(mu > 0.0) || !(V[0] > 0.0)) return 0.0;
    double H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
    norm3(H);
    const double E = kI * mu * lightFalloff(d, kRange, kSrc);
    // the lobe's A and B at r = 1, integrated once over 2048 cosines (linear between: the table's error ~1e-8)
    static std::vector<double> tabA, tabB;
    if (tabA.empty()) {
        tabA.resize(2049);
        tabB.resize(2049);
        for (int k = 0; k <= 2048; ++k) enginetest::ggxSpecularAlbedo(double(k) / 2048.0, 1.0, tabA[k], tabB[k], 96);
    }
    const double x = mu * 2048.0;
    const int k = std::min(2047, int(x));
    const double f = x - double(k);
    const double A = tabA[k] + (tabA[k + 1] - tabA[k]) * f, B = tabB[k] + (tabB[k + 1] - tabB[k]) * f;
    return E * (kAlb * disney(mu, V[0], std::max(0.0, dot3(V, H)), 1.0) + gF0 * A + gW * B);
}

/// a ray against an axis-aligned box: its entry distance, or -1
double rayBox(const double o[3], const double d[3], const double mn[3], const double mx[3])
{
    double t0 = 0.0, t1 = 1e30;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-15) {
            if (o[a] < mn[a] || o[a] > mx[a]) return -1.0;
            continue;
        }
        double ta = (mn[a] - o[a]) / d[a], tb = (mx[a] - o[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }
    return t0 <= t1 ? t0 : -1.0;
}

const double kBlockMn[3] = { 2.0, 1.88, -0.12 }, kBlockMx[3] = { 2.1, 2.12, 0.12 };

/// THE WALL CARD'S TEXEL CENTRE under a point of its +x face: 1/64 m texels, u from z = -4, v from y = 4 down (the
/// premise reads the grid back)
void texelCentre(const double P[3], double Q[3])
{
    Q[0] = P[0];
    Q[1] = 4.0 - (std::floor((4.0 - P[1]) / kTexel) + 0.5) * kTexel;
    Q[2] = -4.0 + (std::floor((P[2] + 4.0) / kTexel) + 0.5) * kTexel;
}

/// what a ray from the patch point O along the unit D returns: the wall's +x face at its exact point (or, `snap`, at
/// its card texel's centre - the card read's), 0 for the blocker, the wall's unlit sides and the black sky
double traceFromPatch(const double O[3], const double D[3], bool snap)
{
    if (!(D[0] < 0.0)) return 0.0;
    const double t = (O[0] - kWallX) / -D[0];
    const double tb = rayBox(O, D, kBlockMn, kBlockMx);
    if (tb >= 0.0 && tb < t) return 0.0;
    double P[3] = { kWallX, O[1] + D[1] * t, O[2] + D[2] * t };
    if (P[1] < 0.0 || P[1] > 4.0 || P[2] < -4.0 || P[2] > 4.0) return 0.0;
    double V[3] = { -D[0], -D[1], -D[2] };
    if (snap) {
        texelCentre(P, P);
        V[0] = O[0] - P[0]; V[1] = O[1] - P[1]; V[2] = O[2] - P[2];
        norm3(V);
    }
    return wallRadiance(P, V);
}

/// jah_ray_set.glsl: jahRayCosineDir in jahConeBasisWorld's frame, for the patch's world normal (-1, 0, 0)
void cacheRay(int i, int n, double D[3])
{
    const double u = (double(i) + 0.5) / double(n), rr = std::sqrt(u);
    const double phi = 6.2831853 * (double(i) * 0.38196601 - std::floor(double(i) * 0.38196601));
    const double l[3] = { rr * std::cos(phi), rr * std::sin(phi), std::sqrt(std::max(1.0 - u, 0.0)) };
    // the cubemap frame (z negated): n_c = (-1, 0, 0), Frisvad's t = (0, 0, 1), b = (0, 1, 0); z negated back
    const double t[3] = { 0.0, 0.0, -1.0 }, b[3] = { 0.0, 1.0, 0.0 }, nn[3] = { -1.0, 0.0, 0.0 };
    for (int k = 0; k < 3; ++k) D[k] = t[k] * l[0] + b[k] * l[1] + nn[k] * l[2];
    norm3(D);
}

double cacheEstimate(const double O[3], int n, bool snap)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        double D[3];
        cacheRay(i, n, D);
        sum += traceFromPatch(O, D, snap);
    }
    return sum / double(n);
}

/// THE BRUTE-FORCE HEMISPHERE: the cosine-weighted mean radiance over m x m stratified directions (Malley)
double hemisphere(const double O[3], int m)
{
    double sum = 0.0;
    for (int a = 0; a < m; ++a)
        for (int c = 0; c < m; ++c) {
            const double u = (double(a) + 0.5) / double(m), ph = 6.283185307179586 * (double(c) + 0.5) / double(m);
            const double rr = std::sqrt(u);
            const double D[3] = { -std::sqrt(std::max(1.0 - u, 0.0)), rr * std::sin(ph), rr * std::cos(ph) };
            sum += traceFromPatch(O, D, false);
        }
    return sum / double(m) / double(m);
}

NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, float alb, bool matte, bool carded = true)
{
    PbrParams p;
    p.albedo = Colour(alb, alb, alb);
    p.roughness = 1.0f;
    if (matte) {
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    }
    const NodeId n = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    if (carded) d.cards = enginetest::boxCards(0.5f);
    s->attachMesh(n, s->createMesh(d), s->createPbrMaterial(p));
    enginetest::poseRegistry()[s][n] = enginetest::NodePose{};
    enginetest::setNodePosition(s, n, at);
    enginetest::setNodeScale(s, n, size);
    return n;
}

struct Band { double got = 0.0, ref = 0.0; unsigned n = 0; };
}   // namespace

int main(int, char **)
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-card-truth-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    bool premise = true, t1Cascades = true, t1Card = true, t2Cascades = true, t2Card = true;
    // FOUR ARMS, each its own scene (a cache at rest re-gathers nothing, so the ray count is set before it is built): the
    // wall CARDED (its card answers) or UNCARDED (the cascades answer, the hit's own direct), at the tier's 16 rays and
    // at the truth door's 128; in each the patch's cards held resident (cards.force: nothing else reads the patch)
    for (int arm = 0; arm < 4; ++arm) {
        const bool card = (arm & 1) == 0;
        const int rays = arm < 2 ? 16 : 128;
        e->setArm("gi.truthRays", rays == 16 ? 0.0 : double(rays));
        View *view = e->createOffscreenView(card ? "card_truth_card" : "card_truth_cascades", 192, 192, Colour(0, 0, 0),
                                            OffscreenContract::StillPicture);
        Scene *s = e->createScene(card ? "card_truth_card" : "card_truth_cascades");
        if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
        view->setScene(s);
        if (!e->rayQueryAvailable() || !e->rayTracing()) {
            std::printf("SKIP: no ray-query device here - the gather and the cards' gather are traced\n");
            return 77;
        }
        {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = 1;
            fx.hdrReadback = true;
            view->setPostFx(fx);
        }
        float sh[27] = { 0 };
        s->setAmbientSh(sh);   // a black world
        const NodeId wall = box(s, Vec3(0.0f, 2.0f, 0.0f), Vec3(0.4f, 4.0f, 8.0f), float(kAlb), false, card);
        const NodeId patch =
            box(s, Vec3(3.4f, 2.0f, 0.0f), Vec3(0.4f, 2.0f * float(kHalf), 2.0f * float(kHalf)), 0.1f, true);
        box(s, Vec3(2.05f, 2.0f, 0.0f), Vec3(0.1f, 0.24f, 0.24f), 0.0f, true);   // the blocker (kBlockMn / kBlockMx)
        {
            const NodeId lamp = s->createNode();
            LightDesc l;
            l.type = LightType::Point;
            l.intensity = float(kI);
            l.range = float(kRange);
            l.sourceRadius = float(kSrc);
            l.colour = Colour(1, 1, 1);
            l.castShadows = true;
            s->setNodeTransform(lamp, Vec3(float(kLamp[0]), float(kLamp[1]), float(kLamp[2])), Quat(), Vec3(1, 1, 1));
            s->setLight(lamp, l);
        }
        enginetest::testCameraLookAt(view, Vec3(2.75f, 2.0f, 0.0f), Vec3(3.2f, 2.0f, 0.0f));
        e->setArm("cards.force", double(unsigned(patch) * 64u + 63u));
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::On;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: GI\n"); return 1; }
        const auto settle = [&]() {
            unsigned long long rl = ~0ull, ad = ~0ull;
            int q = 0, f = 0;
            for (; f < 400 && q < 30; ++f) {
                run(1);
                const GiStatus g = s->published().gi;
                const bool still = g.cards.relights == rl && g.cards.admitted == ad && g.cards.queueLength == 0u;
                rl = g.cards.relights;
                ad = g.cards.admitted;
                q = (!g.cascades.empty() && !g.chainOwed && !g.feedbackOwed && !g.ifdWorking && still) ? q + 1 : 0;
            }
            return q >= 30 ? f - q : -1;
        };
        {
            const int rest = settle();
            if (rays == 16) {
                // THE PREMISES: the wall's stored lobe and its card's texel grid (the card arm), the patch lit by the
                // bounce alone
                CardSample pc;
                const bool okPatch = s->readCardAt(Vec3(3.2f, 2.0f, 0.0f), Vec3(-1, 0, 0), pc, patch) && pc.ok;
                // (the Indirect layer is R11G11B10F: its green a 6-bit mantissa, so the bounce-lit texel's radiance - the
                // fresh indirect, RGBA16F - and its cached half agree to that layer's step)
                const double qInd = pc.indirect[1] > 0.0f
                                        ? std::ldexp(1.0, int(std::floor(std::log2(double(pc.indirect[1])))) - 6) : 0.0;
                bool ok = rest >= 0 && okPatch && std::fabs(pc.radiance[1] - pc.indirect[1]) <= qInd;
                std::printf("   %s: at rest after %d frames; the patch's centre texel: radiance %.6f, its indirect "
                            "alone %.6f\n", card ? "THE CARDED WALL" : "THE UNCARDED WALL", rest,
                            okPatch ? pc.radiance[1] : -1.0, okPatch ? pc.indirect[1] : -1.0);
                if (card) {
                    CardSample cs;
                    const bool okLobe = s->readCardAt(Vec3(0.2f, 2.0f, 0.0f), Vec3(1, 0, 0), cs, wall) && cs.ok;
                    if (okLobe) { gF0 = cs.specularF0[1]; gW = cs.specularWeight; }
                    double gridMiss = 0.0;
                    unsigned gridN = 0;
                    for (const float y : { 0.71f, 1.333f, 2.5f, 3.31f })
                        for (const float z : { -3.1f, -0.77f, 0.42f, 2.9f }) {
                            CardSample c;
                            if (!s->readCardAt(Vec3(0.2f, y, z), Vec3(1, 0, 0), c, wall) || !c.ok) continue;
                            ++gridN;
                            const double P[3] = { kWallX, y, z };
                            double Q[3];
                            texelCentre(P, Q);
                            gridMiss = std::max(gridMiss, std::max(std::fabs(c.position[1] - Q[1]),
                                                                   std::fabs(c.position[2] - Q[2])));
                        }
                    std::printf("   the wall's lobe F0 %.4f w %.4f; %u of 16 texel centres on the 1/64 m grid within "
                                "%.6f m\n", gF0, gW, gridN, gridMiss);
                    ok = ok && okLobe && gridN == 16u && gridMiss < 1e-4;
                }
                premise = premise && ok;
                // (T1) THE DIRECT A GATHER RAY RETURNS
                e->setArm("gather.freezeFrame", 1.0);
                e->setArm("gather.hitsReadback", 1.0);
                run(12);
                const GatherStatus &gs = s->published().gi.gather;
                Band band[8];
                unsigned hits = 0, wrong = 0, off = 0;
                double worst = 0.0;
                for (const GatherRay &r : gs.rays) {
                    if (r.distance < 0.0f || std::fabs(r.hitPos[0] - float(kWallX)) > 0.03f || r.hitNormal[0] < 0.9f) continue;
                    if (r.source != (card ? GatherRay::Card : GatherRay::Voxel)) { ++wrong; continue; }
                    double P[3] = { kWallX, r.hitPos[1], r.hitPos[2] };
                    // the origin: on the patch's plane at the hit's distance, nearest the patch's centre
                    const double dx = kPatchX - kWallX, d = r.distance;
                    const double rho = std::sqrt(std::max(0.0, d * d - dx * dx));
                    double cy = 2.0 - P[1], cz = -P[2];
                    const double cl = std::sqrt(cy * cy + cz * cz);
                    if (std::fabs(rho - cl) > 1.5 * kHalf) { ++off; continue; }   // not the patch's ray
                    if (cl > 1e-9) { cy /= cl; cz /= cl; }
                    const double O[3] = { kPatchX, P[1] + cy * rho, P[2] + cz * rho };
                    if (card) texelCentre(P, P);
                    double V[3] = { O[0] - P[0], O[1] - P[1], O[2] - P[2] };
                    norm3(V);
                    const double a = wallRadiance(P, V);
                    if (!(a > 1e-3)) continue;
                    const int b = std::min(7, int(std::hypot(P[1] - 2.0, P[2]) / 0.5));
                    band[b].got += r.radiance[1];
                    band[b].ref += a;
                    ++band[b].n;
                    ++hits;
                    worst = std::max(worst, std::fabs(r.radiance[1] / a - 1.0));
                }
                e->setArm("gather.hitsReadback", 0.0);
                e->setArm("gather.freezeFrame", 0.0);
                std::printf("   (T1) %s: %u wall hits (%u answered by another source, %u not the patch's); the worst "
                            "single hit %.4f (V known to the patch's size)\n",
                            card ? "THE CARD'S ANSWER" : "THE CASCADES' ANSWER", hits, wrong, off, worst);
                bool within = wrong == 0u && hits >= 800u;
                for (int b = 0; b < 8; ++b) {
                    if (!band[b].n) continue;
                    const double got = band[b].got / band[b].n, ref = band[b].ref / band[b].n, q = quantumOf(ref);
                    std::printf("        rho %.1f-%.1f m: %4u hits, mean %.6f against the analytic %.6f (%+.4f %%, %.2f q)\n",
                                0.5 * b, 0.5 * b + 0.5, band[b].n, got, ref, 100.0 * (got / ref - 1.0),
                                std::fabs(got - ref) / q);
                    within = within && std::fabs(got - ref) <= q;
                }
                (card ? t1Card : t1Cascades) = within;
            }
            // (T2) the patch's centre texel: its Gather layer against the same rays on the CPU and the hemisphere
            CardSample pc;
            const bool ok = s->readCardAt(Vec3(3.2f, 2.0f, 0.0f), Vec3(-1, 0, 0), pc, patch) && pc.ok;
            const double O[3] = { pc.position[0], pc.position[1], pc.position[2] };
            const double same = ok ? cacheEstimate(O, rays, card) : -1.0, truth = ok ? hemisphere(O, 512) : -1.0;
            const double q = quantumOf(same), g = ok ? pc.gather[1] : -1.0;
            std::printf("   (T2) %s, %d rays (rest after %d frames): the patch's gather %.6f; the same %d rays on the CPU "
                        "%.6f (%.2f q); the brute-force hemisphere %.6f - the set's quadrature error %+.3f %%\n",
                        card ? "card-answered" : "cascade-answered", rays, rest, g, rays, same,
                        q > 0.0 ? std::fabs(g - same) / q : -1.0, truth, truth > 0.0 ? 100.0 * (same / truth - 1.0) : 0.0);
            bool &t2 = card ? t2Card : t2Cascades;
            t2 = t2 && ok && rest >= 0 && same > 0.0 && std::fabs(g - same) <= q;
        }
        e->setArm("gi.truthRays", 0.0);
        e->setArm("cards.force", 0.0);
        GiParams offGi;
        offGi.mode = GiMode::Off;
        s->setGlobalIllumination(offGi);
        run(2);
        view->setScene(nullptr);
        e->destroyView(view);
        e->destroyScene(s);
        run(2);
    }
    CHECK(premise, "premise: at rest; the wall's lobe read; its card's texels on the 1/64 m grid; the patch lit by the "
                   "bounce alone");
    CHECK(t1Cascades, "(T1a) the cascades' answer at a lamp-lit wall: every band's mean within q of the analytic direct "
                      "toward V");
    // (DECLARED RED - the lead's ruling, 2026-10-27: 1-3 q low in the card read path, the owner's review list)
    CHECK(t1Card, "(T1b) the card's answer at a lamp-lit wall: every band's mean within q of the analytic direct toward V "
                  "at its texel's centre");
    CHECK(t2Cascades, "(T2a) the patch's card gathers what its rays see, the cascades answering: within q of the same rays "
                      "traced through the analytic fixture, at 16 and 128 rays");
    // (DECLARED RED with (T1b), its one cause: the card read's 1-3 q)
    CHECK(t2Card, "(T2b) ...the wall's card answering: within q of the same rays, at 16 and 128 rays");
    std::printf("\ngi.card_truth: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
