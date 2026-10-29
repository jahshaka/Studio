// gi.mirror_closed_form (PHOTON-7 11-2 (7): A MIRROR WALL CONSERVES ENERGY IN THE STORE) - bounce_series' sealed emitting
// room (card_closed_form's: 8 x 4 x 8 m, 0.3 m slabs, every wall emitting E and carded, a black world, the ray tier at High,
// the cards on) built TWICE in one process: ALL MATTE (rho 0.5, roughness 1), and with its -z wall a NEAR-MIRROR (metal,
// F0 0.95, roughness 0) still emitting E. A card texel is a view-independent cache, so a near-mirror's holds no specular
// (11-2 (7)); the light the mirror reflects reaches the room only through its readers' CONTINUATION - one ray along the
// mirror direction, its hit read by the store (jah_rq_hit_radiance.glsl jahHitMirrorContinue). If a reader went black at
// the mirror the matte walls would lose the room the mirror shows them; if the mirror's card re-emitted its reflection
// alike in every direction the energy would land where the mirror does not send it.
// THE REFERENCE, by a SEEDED PATH TRACE of the same box in this test (no sphere assumption - the lead's E / (1 - rho-bar)
// is the integrating sphere's form, exact only for equal reflectances: in a cube with one f = 0.95 mirror and a = 0.5 the
// matte walls hold 2.178 E, not 2.353 E), with the walls' BRDF as the engine holds them:
//   * a MATTE wall sends E + a' G, G the cosine-weighted mean incoming radiance and a' = rho DA_hemi(1) + 0.04 A-bar(1) +
//     B-bar(1) - the material's whole lobe by direct integration, card_closed_form's a' (the dielectric's 4 % specular
//     share included, re-emitted with the diffuse as the store re-emits it: its area mean is the radiosity's);
//   * the MIRROR sends E (its emission, Lambertian) plus R( mu ) times the radiance along its mirror direction,
//     R( mu ) = F0 A( mu, 0 ) + B( mu, 0 ) - BRDF_Default's split DFG at the near-delta lobe by direct integration
//     (enginetesthelpers.h ggxSpecularAlbedo, the alpha floor 1e-3), a function of the angle, never F0 alone.
// THE LAW IS THE RATIO (the lead's ruling): (the matte faces' area mean with the mirror wall) / (the same faces' area mean
// in the all-matte room, the same build, the same frames) within q of the reference's ratio - the room's own residues
// (bounce_series and card_closed_form are declared at 1 - 3.7 q: the lobe tables, the gather's deficit) cancel in it to
// first order. q is the store's quantum at the mirror room's mean (one half-float step) as a fraction of it. The
// reference's Monte Carlo error is printed, held under q / 4.
// A fixture (the engine alone, one 128 x 128 offscreen view; 10 s budget). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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
const float kE = 1.0f, kRho = 0.5f, kF0 = 0.95f;
const float kHalfX = 4.0f, kHalfZ = 4.0f, kHeight = 4.0f, kThick = 0.3f;
const int kMirrorWall = 4;   // the -z wall (walls[] below)

double quantumOf(double R) { return R > 0.0 ? std::ldexp(1.0, int(std::floor(std::log2(R))) - 10) : 0.0; }

NodeId slab(Scene *s, const Vec3 &centre, const Vec3 &size, bool mirror)
{
    PbrParams p;
    p.albedo = mirror ? Colour(kF0, kF0, kF0) : Colour(kRho, kRho, kRho);
    p.metalness = mirror ? 1.0f : 0.0f;
    p.roughness = mirror ? 0.0f : 1.0f;
    p.emissive = Colour(kE, kE, kE);
    const NodeId n = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    s->attachMesh(n, s->createMesh(d), s->createPbrMaterial(p));
    enginetest::poseRegistry()[s][n] = enginetest::NodePose{};
    enginetest::setNodePosition(s, n, centre);
    enginetest::setNodeScale(s, n, size);
    return n;
}

// ---- THE REFERENCE: a seeded path trace of the room's interior box ----------------------------------------------------
struct Rng {
    uint64_t s;
    double next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return double(s >> 11) * (1.0 / 9007199254740992.0); }
};
struct Ref {
    double aP;                 // the matte walls' a'
    bool mirror;               // the -z wall a near-mirror
    std::vector<double> R;     // R( mu ) at 256 cosines (the mirror's split DFG)
    double reflect(double mu) const
    {
        const double x = std::min(0.999999, std::max(0.0, mu)) * double(R.size()) - 0.5;
        const int i = std::max(0, std::min(int(R.size()) - 2, int(std::floor(x))));
        const double t = std::min(1.0, std::max(0.0, x - double(i)));
        return R[size_t(i)] * (1.0 - t) + R[size_t(i) + 1] * t;
    }
    // the box's exit along d from o: the face (axis, +1/-1 side) and the point
    static void exitOf(const double o[3], const double d[3], double p[3], int &ax, int &side)
    {
        const double lo[3] = { -kHalfX, 0.0, -kHalfZ }, hi[3] = { kHalfX, kHeight, kHalfZ };
        double tBest = 1e30;
        ax = 0;
        side = 1;
        for (int k = 0; k < 3; ++k) {
            if (d[k] > 1e-12) { const double t = (hi[k] - o[k]) / d[k]; if (t < tBest) { tBest = t; ax = k; side = 1; } }
            else if (d[k] < -1e-12) { const double t = (lo[k] - o[k]) / d[k]; if (t < tBest) { tBest = t; ax = k; side = -1; } }
        }
        for (int k = 0; k < 3; ++k) p[k] = o[k] + d[k] * tBest;
        p[ax] = side > 0 ? hi[ax] : lo[ax];
    }
    static void cosineDir(const double n[3], double u1, double u2, double d[3])
    {
        const double r = std::sqrt(u1), ph = 6.283185307179586 * u2;
        const double lx = r * std::cos(ph), ly = r * std::sin(ph), lz = std::sqrt(std::max(0.0, 1.0 - u1));
        double t[3] = { 1, 0, 0 };
        if (std::fabs(n[0]) > 0.9) { t[0] = 0; t[1] = 1; }
        double b[3] = { n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0] };
        const double bl = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
        for (double &x : b) x /= bl;
        const double tt[3] = { b[1] * n[2] - b[2] * n[1], b[2] * n[0] - b[0] * n[2], b[0] * n[1] - b[1] * n[0] };
        for (int k = 0; k < 3; ++k) d[k] = lx * tt[k] + ly * b[k] + lz * n[k];
    }
    // the radiance arriving at o along d: E at every hit plus, by one collision sample, the hit's reflection
    double incoming(const double o0[3], const double d0[3], Rng &g) const
    {
        double o[3] = { o0[0], o0[1], o0[2] }, d[3] = { d0[0], d0[1], d0[2] };
        double L = 0.0, w = 1.0;
        for (int depth = 0; depth < 400 && w > 1e-7; ++depth) {
            double p[3];
            int ax, side;
            exitOf(o, d, p, ax, side);
            L += w * double(kE);
            double n[3] = { 0, 0, 0 };
            n[ax] = -double(side);   // the inner face's normal, into the room
            for (int k = 0; k < 3; ++k) o[k] = p[k];
            if (mirror && ax == 2 && side < 0) {
                const double mu = -(d[0] * n[0] + d[1] * n[1] + d[2] * n[2]);
                w *= reflect(mu);
                for (int k = 0; k < 3; ++k) d[k] += 2.0 * mu * n[k];
            } else {
                w *= aP;
                cosineDir(n, g.next(), g.next(), d);
            }
        }
        return L;
    }
    // a matte point's stored radiance E + a' G, G by `n` cosine samples
    double matteAt(const double p[3], const double n[3], int samples, Rng &g) const
    {
        double G = 0.0;
        for (int i = 0; i < samples; ++i) {
            double d[3];
            cosineDir(n, g.next(), g.next(), d);
            G += incoming(p, d, g);
        }
        return double(kE) + aP * G / double(samples);
    }
};

// the five matte faces' points (card_closed_form's 0.5 m midpoint grid, in proportion to the area), the mirror's excluded
template <typename F> void matteGrid(F &&each)
{
    for (float a = -3.75f; a < 4.0f; a += 0.5f)
        for (float b = -3.75f; b < 4.0f; b += 0.5f) {
            each(0, Vec3(a, 0.0f, b), Vec3(0, 1, 0));
            each(1, Vec3(a, kHeight, b), Vec3(0, -1, 0));
        }
    for (float a = -3.75f; a < 4.0f; a += 0.5f)
        for (float y = 0.25f; y < kHeight; y += 0.5f) {
            each(2, Vec3(-kHalfX, y, a), Vec3(1, 0, 0));
            each(3, Vec3(kHalfX, y, a), Vec3(-1, 0, 0));
            each(5, Vec3(a, y, kHalfZ), Vec3(0, 0, -1));
        }
}
}   // namespace

int main(int, char **)
{
    // ---- THE REFERENCE --------------------------------------------------------------------------------------------
    const auto t0 = std::chrono::steady_clock::now();
    double A = 0.0, B = 0.0;
    enginetest::ggxSpecularAlbedoHemi(1.0, A, B);
    Ref ref;
    ref.aP = double(kRho) * enginetest::disneyDiffuseAlbedoHemi(1.0) + 0.04 * A + B;
    ref.R.resize(256);
    double Rmean = 0.0;
    for (size_t i = 0; i < ref.R.size(); ++i) {
        const double mu = (double(i) + 0.5) / double(ref.R.size());
        double a = 0.0, b = 0.0;
        enginetest::ggxSpecularAlbedo(mu, 0.0, a, b, 128);
        ref.R[i] = double(kF0) * a + b;
        Rmean += 2.0 * ref.R[i] * mu / double(ref.R.size());
    }
    // the matte mean of each room by the path trace (4 x 256 cosine samples a point: four batches give its error)
    double refMean[2] = { 0, 0 }, refErr[2] = { 0, 0 };
    for (int room = 0; room < 2; ++room) {
        ref.mirror = room == 1;
        double batch[4] = { 0, 0, 0, 0 };
        size_t nPts = 0;
        for (int k = 0; k < 4; ++k) {
            Rng g{ 0x9E3779B97F4A7C15ull * uint64_t(k + 1) + uint64_t(room) };
            double sum = 0.0;
            size_t n = 0;
            matteGrid([&](int, const Vec3 &p, const Vec3 &nrm) {
                const double pp[3] = { p.x, p.y, p.z }, nn[3] = { nrm.x, nrm.y, nrm.z };
                sum += ref.matteAt(pp, nn, 256, g);
                ++n;
            });
            batch[k] = sum / double(n);
            nPts = n;
        }
        const double m = 0.25 * (batch[0] + batch[1] + batch[2] + batch[3]);
        double v = 0.0;
        for (const double b2 : batch) v += (b2 - m) * (b2 - m);
        refMean[room] = m;
        refErr[room] = std::sqrt(v / 3.0) / 2.0;   // the standard error of the four batches' mean
        std::printf("   reference %s: the matte faces' mean %.5f +- %.5f (%zu points x 1024 paths)\n",
                    room ? "MIRROR room" : "all-matte room", m, refErr[room], nPts);
    }
    // ...and, for the arms' reading, the room whose mirror reads BLACK to every reader (R = 0: a store with no specular
    // and no continuation) - one batch, printed only
    double blackRatio = 0.0;
    {
        Ref black = ref;
        black.mirror = true;
        std::fill(black.R.begin(), black.R.end(), 0.0);
        Rng g{ 0xB1ACB1ACull };
        double sum = 0.0;
        size_t n = 0;
        matteGrid([&](int, const Vec3 &p, const Vec3 &nrm) {
            const double pp[3] = { p.x, p.y, p.z }, nn[3] = { nrm.x, nrm.y, nrm.z };
            sum += black.matteAt(pp, nn, 256, g);
            ++n;
        });
        blackRatio = sum / double(n) / refMean[0];
    }
    const double refMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double refRatio = refMean[1] / refMean[0];
    const double refRatioErr = refRatio * std::sqrt(std::pow(refErr[0] / refMean[0], 2) + std::pow(refErr[1] / refMean[1], 2));
    const double qRel = quantumOf(refMean[1]) / refMean[1];
    std::printf("   a' = %.5f (the all-matte limit E / (1 - a') = %.5f); the mirror's R( mu ) at mu 1 / 0.5 / 0.1: %.4f / %.4f / "
                "%.4f, its hemispherical mean %.4f (F0 %.2f); the reference ratio %.5f +- %.5f (q %.5f of it); a mirror black to "
                "its readers %.5f; the reference %.0f ms\n",
                ref.aP, double(kE) / (1.0 - ref.aP), ref.reflect(1.0), ref.reflect(0.5), ref.reflect(0.1), Rmean,
                double(kF0), refRatio, refRatioErr, qRel * refRatio, blackRatio, refMs);

    // ---- THE ENGINE -----------------------------------------------------------------------------------------------
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-mirror-closed-form-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    double mean[2] = { 0, 0 };
    bool ok[2] = { false, false };
    for (int room = 0; room < 2; ++room) {
        View *in = e->createOffscreenView("mirror_in", 128, 128, Colour(0, 0, 0), OffscreenContract::StillPicture);
        Scene *s = e->createScene("mirror_closed_form");
        if (!in || !s) { std::printf("FAIL: view/scene\n"); return 1; }
        in->setScene(s);
        if (!e->rayQueryAvailable() || !e->rayTracing()) {
            std::printf("SKIP: no ray-query device here - the cards' gather and the continuation are traced\n");
            return 77;
        }
        {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = 1;
            in->setPostFx(fx);
        }
        float sh[27] = { 0 };
        s->setAmbientSh(sh);   // the black world
        const float t = kThick;
        const bool m = room == 1;
        const NodeId walls[6] = {
            slab(s, Vec3(0.0f, -0.5f * t, 0.0f), Vec3(2.0f * kHalfX + 2.0f * t, t, 2.0f * kHalfZ + 2.0f * t), false),
            slab(s, Vec3(0.0f, kHeight + 0.5f * t, 0.0f), Vec3(2.0f * kHalfX + 2.0f * t, t, 2.0f * kHalfZ + 2.0f * t), false),
            slab(s, Vec3(-kHalfX - 0.5f * t, 0.5f * kHeight, 0.0f), Vec3(t, kHeight + 2.0f * t, 2.0f * kHalfZ + 2.0f * t), false),
            slab(s, Vec3(kHalfX + 0.5f * t, 0.5f * kHeight, 0.0f), Vec3(t, kHeight + 2.0f * t, 2.0f * kHalfZ + 2.0f * t), false),
            slab(s, Vec3(0.0f, 0.5f * kHeight, -kHalfZ - 0.5f * t), Vec3(2.0f * kHalfX + 2.0f * t, kHeight + 2.0f * t, t), m),
            slab(s, Vec3(0.0f, 0.5f * kHeight, kHalfZ + 0.5f * t), Vec3(2.0f * kHalfX + 2.0f * t, kHeight + 2.0f * t, t), false) };
        // inside, from a lower corner up across the room toward the -z wall
        enginetest::testCameraLookAt(in, Vec3(3.0f, 0.8f, 3.0f), Vec3(-3.0f, 2.6f, -3.0f));
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::On;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: GI\n"); return 1; }
        int rest = -1;
        unsigned long long relights = ~0ull, admitted = ~0ull;
        for (int f = 0, quiet = 0; f < 600; ++f) {
            run(1);
            const GiStatus g = s->published().gi;
            const bool still = g.cards.relights == relights && g.cards.admitted == admitted && g.cards.queueLength == 0u;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = (!g.cascades.empty() && !g.chainOwed && !g.feedbackOwed && !g.ifdWorking && still) ? quiet + 1 : 0;
            if (quiet >= 30) { rest = f + 1 - quiet; break; }
        }
        double sum = 0.0, lo = 1e30, hi = 0.0;
        size_t n = 0;
        bool seen[6] = { false, false, false, false, false, false };
        matteGrid([&](int k, const Vec3 &p, const Vec3 &nrm) {
            CardSample cs;
            if (!s->readCardAt(p, nrm, cs, walls[k]) || !cs.ok || !cs.lit) return;
            const double L = std::max(std::max(double(cs.radiance[0]), double(cs.radiance[1])), double(cs.radiance[2]));
            sum += L;
            lo = std::min(lo, L);
            hi = std::max(hi, L);
            ++n;
            seen[k] = true;
        });
        size_t faces = 0;
        for (int k = 0; k < 6; ++k) faces += (k != kMirrorWall && seen[k]) ? 1u : 0u;
        mean[room] = n ? sum / double(n) : 0.0;
        ok[room] = rest >= 0 && faces == 5u;
        std::printf("   ENGINE %s: at rest after %d frames | %zu points on %zu matte faces, the mean %.5f (the reference "
                    "%.5f, %+.3f %%), the texels %.5f .. %.5f\n", m ? "MIRROR room" : "all-matte room", rest, n, faces,
                    mean[room], refMean[room], 100.0 * (mean[room] / refMean[room] - 1.0), n ? lo : 0.0, hi);
        GiParams off;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        run(2);
        in->setScene(nullptr);
        e->destroyView(in);
        e->destroyScene(s);
        run(2);
    }
    const double ratio = mean[0] > 0.0 ? mean[1] / mean[0] : 0.0;
    std::printf("   THE RATIO: engine %.5f against the reference %.5f: %+.4f (%.2f q; a mirror black to its readers "
                "%.5f; the reference's MC error %.2f q)\n", ratio, refRatio, ratio - refRatio,
                std::fabs(ratio - refRatio) / (qRel * refRatio), blackRatio, refRatioErr / (qRel * refRatio));
    CHECK(refRatioErr <= 0.25 * qRel * refRatio, "(0) the reference's Monte Carlo error under q / 4");
    CHECK(ok[0] && ok[1] && std::fabs(ratio - refRatio) <= qRel * refRatio,
          "(1) A MIRROR WALL CONSERVES ENERGY IN THE STORE: the matte faces' mean, mirror room / all-matte room, within q "
          "of the path-traced reference's ratio");
    std::printf("\ngi.mirror_closed_form: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
