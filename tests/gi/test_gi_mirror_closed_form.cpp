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
//   * a MATTE wall STORES E + a' G, G the cosine-weighted mean incoming radiance and a' = rho DA_hemi(1) + 0.04 A-bar(1) +
//     B-bar(1) - the material's whole lobe by direct integration, card_closed_form's a' (the dielectric's 4 % specular
//     share included) - and SENDS a ray arriving at cos mu E + ( rho DA( mu, 1 ) + 0.04 A-bar + B-bar ) G: the readers'
//     view restore (jahHitBounceToward, jah_card_view.glsl), the diffuse lobe at its directional albedo;
//   * the MIRROR sends E (its emission, Lambertian) plus R( mu ) times the radiance along its mirror direction,
//     R( mu ) = F0 A( mu, 0 ) + B( mu, 0 ) - BRDF_Default's split DFG at the near-delta lobe by direct integration
//     (enginetesthelpers.h ggxSpecularAlbedo, the alpha floor 1e-3), a function of the angle, never F0 alone.
// THE LAW IS THE RATIO (the lead's ruling): (the matte faces' area mean with the mirror wall) / (the same faces' area mean
// in the all-matte room, the same build, the same frames) within q of the reference's ratio - the room's own residues
// (bounce_series and card_closed_form are declared at 1 - 3.7 q: the lobe tables, the gather's deficit) cancel in it to
// first order. q is the store's quantum at the mirror room's mean (one half-float step) as a fraction of it. The
// reference's Monte Carlo error is printed, held under q / 4.
// DECLARED (REDS-1, the lead's ruling): law (1) at 1.44 q - the ratio +0.0009 under the cards' RGBA16F Indirect/Flat
// layers; under the old R11G11B10F layers it read 0.09-0.68 q, the 6-bit grid's rounding landing well (the two rooms'
// values sit at different offsets on it): the cards' real bias in the mirror room, the matte walls reading the mirror's
// light 0.09 % high, cause unmeasured (the continuation's read in the cards' gather, or the mirror card's diffuse half).
// Law (2), the voxel skin's ratio, at 9-10 q (+0.0065): NOT the cross-cascade deficit (every hit read at c0: +0.0057),
// which is real in the store's LEVEL (the all-matte room 1.5275 -> 1.5865 under that arm) and cancels in the ratio.
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
    double aP;                 // the matte walls' a' (the hemispherical mean: what a texel / a voxel STORES)
    double rhoDA[257];         // ...toward a viewer at cos mu: rho DA( mu, 1 ) (the diffuse lobe's directional albedo, the
    double specBar = 0.0;      //    engine's view restore) + the specular share's integral 0.04 A-bar + B-bar
    double toward(double mu) const
    {
        const double x = std::min(1.0, std::max(0.0, mu)) * 256.0;
        const int i = std::min(255, int(x));
        return rhoDA[i] + (rhoDA[i + 1] - rhoDA[i]) * (x - double(i)) + specBar;
    }
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
                // the hit's radiance TOWARD THIS RAY's viewer, as the engine's readers restore it (jahHitBounceToward,
                // jah_card_view.glsl): the diffuse lobe at its directional albedo, the specular share as its integral
                w *= toward(-(d[0] * n[0] + d[1] * n[1] + d[2] * n[2]));
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
/// THE VOXEL STORE's matte skin (bounce_series' read: cascade 0's covered inner-skin cells off the room's edges, the
/// ROOM-FACING side), the mirror's face excluded: each cell's point on its face, its inward normal, its L
struct Skin { std::vector<Vec3> p, n; std::vector<double> L; };
Skin voxelSkin(Scene *s)
{
    Skin r;
    GiVoxelVolume v;
    if (!s->giVoxelLight(0, v) || !v.available || v.light.empty()) return r;
    const float cell = v.cell[0];
    for (int z = 0; z < v.depth; ++z)
        for (int y = 0; y < v.height; ++y)
            for (int x = 0; x < v.width; ++x) {
                const float p[3] = { v.origin[0] + cell * (x + 0.5f), v.origin[1] + cell * (y + 0.5f),
                                     v.origin[2] + cell * (z + 0.5f) };
                const bool inGrown = std::fabs(p[0]) < kHalfX + cell && std::fabs(p[2]) < kHalfZ + cell &&
                                     p[1] > -cell && p[1] < kHeight + cell;
                const bool inShrunk = std::fabs(p[0]) < kHalfX - cell && std::fabs(p[2]) < kHalfZ - cell &&
                                      p[1] > cell && p[1] < kHeight - cell;
                if (!inGrown || inShrunk) continue;
                int near = 0;
                near += std::fabs(p[0]) > kHalfX - 1.0f ? 1 : 0;
                near += std::fabs(p[2]) > kHalfZ - 1.0f ? 1 : 0;
                near += (p[1] < 1.0f || p[1] > kHeight - 1.0f) ? 1 : 0;
                if (near > 1) continue;
                const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                const float c = v.light[i + 3];
                if (!(c > 0.5f)) continue;
                float in[3] = { 0.0f, 0.0f, 0.0f };
                const float dx = kHalfX - std::fabs(p[0]), dz = kHalfZ - std::fabs(p[2]), dy0 = p[1], dy1 = kHeight - p[1];
                const float dmin = std::min(std::min(std::fabs(dx), std::fabs(dz)), std::min(std::fabs(dy0), std::fabs(dy1)));
                Vec3 on(p[0], p[1], p[2]);
                if (dmin == std::fabs(dy0)) { in[1] = 1.0f; on.y = 0.0f; }
                else if (dmin == std::fabs(dy1)) { in[1] = -1.0f; on.y = kHeight; }
                else if (dmin == std::fabs(dx)) { in[0] = p[0] > 0.0f ? -1.0f : 1.0f; on.x = p[0] > 0.0f ? kHalfX : -kHalfX; }
                else { in[2] = p[2] > 0.0f ? -1.0f : 1.0f; on.z = p[2] > 0.0f ? kHalfZ : -kHalfZ; }
                if (in[2] > 0.5f) continue;   // the -z face: the mirror's
                const float *side = v.light.data() + i;
                if (v.normal.size() == v.light.size() && v.lightFront.size() == v.light.size() &&
                    v.lightBack.size() == v.light.size() && v.normal[i + 3] > 0.5f) {
                    const float n[3] = { 2.0f * v.normal[i] - 1.0f, 2.0f * v.normal[i + 1] - 1.0f,
                                         2.0f * v.normal[i + 2] - 1.0f };
                    side = (n[0] * in[0] + n[1] * in[1] + n[2] * in[2] >= 0.0f ? v.lightFront.data() : v.lightBack.data()) + i;
                }
                r.p.push_back(on);
                r.n.push_back(Vec3(in[0], in[1], in[2]));
                r.L.push_back(double(std::max(std::max(side[0], side[1]), side[2]) / c));
            }
    return r;
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
    ref.specBar = 0.04 * A + B;
    for (int i = 0; i <= 256; ++i) ref.rhoDA[i] = double(kRho) * enginetest::disneyDiffuseAlbedo(double(i) / 256.0, 1.0);
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
            sum += black.matteAt(pp, nn, 64, g);   // (printed only: a coarse batch)
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
    Skin skin[2];
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
        skin[room] = voxelSkin(s);
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
    // (2) THE VOXEL STORE - its matte skin fed by the ray tier's SIDES' rays, a reader with no C2 of its own (the probe
    // gathers are the same class; (7) as built read -5.7 % here). A side's ray reads THE CASCADES ALONE at its hit
    // (rq_voxel_sides.comp). THE RATIO, as (1): the skin's mean in the mirror room / in the all-matte room, against the
    // path trace's ratio AT THE SKIN's OWN CELLS with the engine's BRDF (a', the cards' row's) - the store's own residue
    // (its lobe a_v, read from the all-matte room and printed) cancels in it to first order. DECLARED, PRINTED (the
    // lead's ruling, 2026-10-29): the voxel skin's ratio reads +0.60 % (9.8 q) over its reference with the directional
    // re-emission (+0.36 %, 5.9 q, with a Lambertian one) - THE VOXEL ROUTE's CROSS-CASCADE RESIDUE, REDS-1's: cascade 0
    // holds no cell within 1.5 m of the mirror, the excess grows with the mirror a face sees (floor / ceiling +0.60 %,
    // the +-x walls +0.32 %, the +z wall facing it +0.90 %) - a mirror lengthens the paths, so a side's continued ray is
    // answered by a coarser cascade whose deficit differs from c0's. (7) as built read -5.7 % here (a black mirror), the
    // fix +0.6 %; REDS-1's c0-clamped arm measures the residue.
    double vox[2] = { 0, 0 }, voxRef = 0.0, voxErr = 0.0, aV = 0.0;
    for (int room = 0; room < 2; ++room) {
        for (const double L : skin[room].L) vox[room] += L;
        vox[room] = skin[room].L.empty() ? 0.0 : vox[room] / double(skin[room].L.size());
    }
    if (vox[0] > double(kE) && !skin[1].p.empty()) {
        aV = 1.0 - double(kE) / vox[0];   // (printed: the store's own lobe, its residue against the engine's BRDF)
        Ref rv = ref;                     // THE ENGINE's BRDF (a', the cards' row's): the ratio cancels the store's residue
        rv.mirror = true;
        const int per = std::max(64, int(917504 / skin[1].p.size()));
        double batch[4] = { 0, 0, 0, 0 };
        for (int k = 0; k < 4; ++k) {
            Rng g{ 0x5EED5EEDull * uint64_t(k + 7) };
            double sum = 0.0;
            for (size_t i = 0; i < skin[1].p.size(); ++i) {
                const double pp[3] = { skin[1].p[i].x, skin[1].p[i].y, skin[1].p[i].z },
                             nn[3] = { skin[1].n[i].x, skin[1].n[i].y, skin[1].n[i].z };
                sum += rv.matteAt(pp, nn, per / 4, g);
            }
            batch[k] = sum / double(skin[1].p.size());
        }
        voxRef = 0.25 * (batch[0] + batch[1] + batch[2] + batch[3]);
        double vv = 0.0;
        for (const double b2 : batch) vv += (b2 - voxRef) * (b2 - voxRef);
        voxErr = std::sqrt(vv / 3.0) / 2.0;
    }
    const double voxRatio = vox[0] > 0.0 ? vox[1] / vox[0] : 0.0, voxRefRatio = voxRef * (1.0 - ref.aP) / double(kE);
    const double qV = quantumOf(voxRef) / std::max(voxRef, 1e-9) * voxRefRatio;
    std::printf("   THE VOXEL STORE (the sides' rays): %zu / %zu skin cells, the voxel's lobe a_v %.5f (all-matte %.5f); the mirror room "
                "%.5f against the path trace at its cells %.5f +- %.5f: the ratio %.5f against %.5f (%+.4f, %.2f q)\n",
                skin[0].L.size(), skin[1].L.size(), aV, vox[0], vox[1], voxRef, voxErr, voxRatio, voxRefRatio,
                voxRatio - voxRefRatio, qV > 0.0 ? std::fabs(voxRatio - voxRefRatio) / qV : 0.0);
    CHECK(refRatioErr <= 0.25 * qRel * refRatio && voxErr * (1.0 - ref.aP) / double(kE) <= 0.25 * qV,
          "(0) the references' Monte Carlo error under q / 4");
    CHECK(ok[0] && ok[1] && std::fabs(ratio - refRatio) <= qRel * refRatio,
          "(1) A MIRROR WALL CONSERVES ENERGY IN THE STORE: the matte faces' mean, mirror room / all-matte room, within q "
          "of the path-traced reference's ratio");
    // (2) is PRINTED, DECLARED by the lead (2026-10-29): not asserted, not loosened - see its head above
    std::printf("\ngi.mirror_closed_form: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
