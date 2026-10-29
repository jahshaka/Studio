// gi.card_closed_form (PHOTON-7 stage 10-3, R12: the cards' closed-form limit; L9 is physics) - THE CARDS CONVERGE TO
// THE SERIES' LIMIT INSIDE A SEALED ROOM, AND NOTHING OF IT REACHES THE OUTSIDE. bounce_series' room with the cards on.
// A SEALED 8 x 4 x 8 m room (0.3 m slabs, every seam overlapped, each slab carded) whose walls EMIT a uniform radiance E
// and reflect with albedo rho = 0.5 at roughness 1 (the default dielectric's 4 % specular layer), in a black world; the
// ray tier at High, the cards on, one still view inside the room. A card texel holds E + a' G (the relight: its Flat layer
// the emission and the specular lobe's share, its Indirect the diffuse's - JahCardLight_cs.glsl), G its gather of the
// room. A sealed room escapes nothing, so on the INNER faces the series' limit is
//     L = E / ( 1 - a' ),   a' = rho DA_hemi(1) + F0 A-bar(1) + B-bar(1)
// (the material's whole lobe by direct integration, enginetesthelpers.h - never read from the engine's tables), and
// on the OUTER faces, which see the black void alone, L = E exactly.
// THE BAR IS THE STORE'S QUANTUM q (the lead's ruling, R12): one half-float step at the reference.
// THE LAWS, at the cards' rest:
//   (1) THE INNER FACES HOLD THE LIMIT: the AREA's mean of the six inner faces (a 0.5 m midpoint grid over each) within q
//       of E / (1 - a'), at the tier's 16 rays and at the truth door's 128 (gi.truthRays, set before the scene is
//       built). The area's mean is the law, not each point: the cards' lobe is view-dependent (a hit reads the
//       diffuse toward its viewer), so a point's incident light depends on the angles it sees the room at, while the
//       room's energy balance holds the area's mean of the radiosity (the cards' stored value) to the closed form
//       exactly. The points' range printed. Measured (REDS-1): +0.021 % (0.35 q) at 16 rays, -0.016 % (0.26 q) at 128 -
//       after the diffuse lobe's closed form (it was -0.20 % / -0.22 %: the fit to Ogre's DFG table) and the Indirect and
//       Flat layers at RGBA16F (R11G11B10F's 6-bit grid read +0.08..0.10 % once the radiance store rounded);
//   (2) FROM THE OTHER SIDE, NOTHING (L9): every texel of the +x wall's OUTER face (its cards held resident by
//       cards.force: nothing inside the room reads its outside) within q of E - no light of the room through a wall.
//       Measured: 196,608 texels, every one exactly E.
// A fixture (the engine alone, one 128 x 128 offscreen view; 10 s budget). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
const float kE = 1.0f, kRho = 0.5f;
const float kHalfX = 4.0f, kHalfZ = 4.0f, kHeight = 4.0f, kThick = 0.3f;

double quantumOf(double R) { return R > 0.0 ? std::ldexp(1.0, int(std::floor(std::log2(R))) - 10) : 0.0; }

NodeId slab(Scene *s, const Vec3 &centre, const Vec3 &size)
{
    PbrParams p;
    p.albedo = Colour(kRho, kRho, kRho);
    p.roughness = 1.0f;
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
}   // namespace

int main(int, char **)
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-card-closed-form-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    // THE LIMIT, by direct integration
    double A = 0.0, B = 0.0;
    enginetest::ggxSpecularAlbedoHemi(1.0, A, B);
    const double aTrue = double(kRho) * enginetest::disneyDiffuseAlbedoHemi(1.0) + 0.04 * A + B;
    const double limit = double(kE) / (1.0 - aTrue);
    std::printf("   a' = rho DA_hemi(1) + 0.04 A-bar + B-bar = %.5f; the inner faces' limit E / (1 - a') = %.5f\n", aTrue,
                limit);
    bool inner = true, outer = true;
    for (const int rays : { 16, 128 }) {
        e->setArm("gi.truthRays", rays == 16 ? 0.0 : double(rays));
        View *in = e->createOffscreenView("closed_in", 128, 128, Colour(0, 0, 0), OffscreenContract::StillPicture);
        Scene *s = e->createScene("closed_form");
        if (!in || !s) { std::printf("FAIL: view/scene\n"); return 1; }
        in->setScene(s);
        if (!e->rayQueryAvailable() || !e->rayTracing()) {
            std::printf("SKIP: no ray-query device here - the cards' gather is traced\n");
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
        // each slab and the bucket of its INNER face (2 x axis + negative: the face looking into the room)
        struct Wall { NodeId node; unsigned inner; };
        const Wall walls[6] = {
            { slab(s, Vec3(0.0f, -0.5f * t, 0.0f), Vec3(2.0f * kHalfX + 2.0f * t, t, 2.0f * kHalfZ + 2.0f * t)), 2u },
            { slab(s, Vec3(0.0f, kHeight + 0.5f * t, 0.0f), Vec3(2.0f * kHalfX + 2.0f * t, t, 2.0f * kHalfZ + 2.0f * t)), 3u },
            { slab(s, Vec3(-kHalfX - 0.5f * t, 0.5f * kHeight, 0.0f), Vec3(t, kHeight + 2.0f * t, 2.0f * kHalfZ + 2.0f * t)), 0u },
            { slab(s, Vec3(kHalfX + 0.5f * t, 0.5f * kHeight, 0.0f), Vec3(t, kHeight + 2.0f * t, 2.0f * kHalfZ + 2.0f * t)), 1u },
            { slab(s, Vec3(0.0f, 0.5f * kHeight, -kHalfZ - 0.5f * t), Vec3(2.0f * kHalfX + 2.0f * t, kHeight + 2.0f * t, t)), 4u },
            { slab(s, Vec3(0.0f, 0.5f * kHeight, kHalfZ + 0.5f * t), Vec3(2.0f * kHalfX + 2.0f * t, kHeight + 2.0f * t, t)), 5u } };
        // THE OUTER FACE: the +x wall's every card held resident (cards.force - nothing inside the room reads its outside)
        e->setArm("cards.force", double(unsigned(walls[3].node) * 64u + 63u));
        // inside, from a lower corner up across the room: five inner faces in view, the sixth read by their gathers
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
        // THE INNER FACES' AREA: a 0.5 m midpoint grid over each face's interior (its points in proportion to its area),
        // read at the card's texel; and EVERY TEXEL of the +x wall's outer face (all of it lies outside the room); a
        // value is its brightest channel
        double sumIn = 0.0, lo = 1e30, hi = 0.0, worstOut = 0.0;
        size_t nIn = 0, nOut = 0, facesIn = 0;
        bool seen[6] = { false, false, false, false, false, false };
        const auto inner1 = [&](int k, const Vec3 &p, const Vec3 &n) {
            CardSample cs;
            if (!s->readCardAt(p, n, cs, walls[k].node) || !cs.ok || !cs.lit) return;
            const double L = std::max(std::max(double(cs.radiance[0]), double(cs.radiance[1])), double(cs.radiance[2]));
            sumIn += L;
            lo = std::min(lo, L);
            hi = std::max(hi, L);
            ++nIn;
            seen[k] = true;
        };
        for (float a = -3.75f; a < 4.0f; a += 0.5f)
            for (float b = -3.75f; b < 4.0f; b += 0.5f) {
                inner1(0, Vec3(a, 0.0f, b), Vec3(0, 1, 0));
                inner1(1, Vec3(a, kHeight, b), Vec3(0, -1, 0));
            }
        for (float a = -3.75f; a < 4.0f; a += 0.5f)
            for (float y = 0.25f; y < kHeight; y += 0.5f) {
                inner1(2, Vec3(-kHalfX, y, a), Vec3(1, 0, 0));
                inner1(3, Vec3(kHalfX, y, a), Vec3(-1, 0, 0));
                inner1(4, Vec3(a, y, -kHalfZ), Vec3(0, 0, 1));
                inner1(5, Vec3(a, y, kHalfZ), Vec3(0, 0, -1));
            }
        std::vector<CardRadiance> all;
        unsigned long long cacheFrame = 0ull;
        const bool read = s->readCardRadiance(all, cacheFrame);
        for (const CardRadiance &c : all) {
            if (c.node != walls[3].node || c.bucket != 0u) continue;
            for (size_t i = 0; i + 2 < c.rgb.size(); i += 3) {
                const double L = std::max(std::max(double(c.rgb[i]), double(c.rgb[i + 1])), double(c.rgb[i + 2]));
                if (!(L > 0.0)) continue;   // a texel nothing was captured into
                worstOut = std::max(worstOut, std::fabs(L - double(kE)));
                ++nOut;
            }
        }
        for (const bool b : seen) facesIn += b ? 1u : 0u;
        const double meanIn = nIn ? sumIn / double(nIn) : 0.0;
        std::printf("   %d rays: at rest after %d frames | INNER %zu points on %zu faces, the area's mean %.5f against %.5f "
                    "(%+.3f %%, %.2f q), the texels %.5f .. %.5f | OUTER (+x wall) %zu texels, the worst |L - E| %.6f "
                    "(%.2f q)\n", rays, rest, nIn, facesIn, meanIn, limit, 100.0 * (meanIn / limit - 1.0),
                    std::fabs(meanIn - limit) / quantumOf(limit), nIn ? lo : 0.0, hi, nOut, worstOut,
                    worstOut / quantumOf(kE));
        {   // MEASURING ARM (REDS-1 follow-up, reverted after its one run): the lab's hemi (c) on this room - the floor's centre
            // texel, 64 x 64 stratified cosine rays through Scene::traceRays, each hit's card read toward the ray (its stored
            // radiance); the inner faces all hold ~E / (1 - a'), so the mean is that if the instrument is right
            CardSample c0;
            if (s->readCardAt(Vec3(0.0f, 0.0f, 0.0f), Vec3(0, 1, 0), c0) && c0.ok) {
                const double T[3] = { c0.position[0], c0.position[1] + 0.002, c0.position[2] };
                double sum = 0.0; unsigned n = 0, miss = 0, noCard = 0;
                const int m = 64;
                for (int i = 0; i < m; ++i)
                    for (int j = 0; j < m; ++j) {
                        const double u = (i + 0.5) / m, ph = 6.283185307179586 * (j + 0.5) / m, rr = std::sqrt(u);
                        const double d[3] = { rr * std::cos(ph), std::sqrt(1.0 - u), rr * std::sin(ph) };
                        std::vector<float> rays = { float(T[0]), float(T[1]), float(T[2]), 0.0f, float(d[0]), float(d[1]), float(d[2]),
                                                    100.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                        unsigned mask = 0xFFu;
                        std::memcpy(&rays[8], &mask, sizeof(float));
                        std::vector<float> hits;
                        ++n;
                        if (!s->traceRays(rays, hits) || hits.size() < 4 || hits[0] < 0.0f) { ++miss; continue; }
                        const Vec3 H(float(T[0] + d[0] * hits[0]), float(T[1] + d[1] * hits[0]), float(T[2] + d[2] * hits[0]));
                        CardSample h;
                        if (!s->readCardAt(H, Vec3(float(-d[0]), float(-d[1]), float(-d[2])), h) || !h.ok) { ++noCard; continue; }
                        sum += double(h.radiance[1]);
                    }
                std::printf("   ARM hemi on the sealed room: the floor centre's card gather %.6f, the CPU hemisphere %.6f (%u rays, %u misses, "
                            "%u no card) against the inner faces' %.5f: hemi / limit %+.2f %%, gather / limit %+.2f %%\n", double(c0.gather[1]),
                            n ? sum / n : -1.0, n, miss, noCard, limit, n ? 100.0 * (sum / n / limit - 1.0) : 0.0, 100.0 * (c0.gather[1] / limit - 1.0));
            }
        }
        inner = inner && read && rest >= 0 && facesIn == 6u && std::fabs(meanIn - limit) <= quantumOf(limit);
        outer = outer && read && rest >= 0 && nOut > 1000u && worstOut <= quantumOf(kE);
        e->setArm("cards.force", 0.0);
        GiParams off;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        run(2);
        in->setScene(nullptr);
        e->destroyView(in);
        e->destroyScene(s);
        run(2);
    }
    e->setArm("gi.truthRays", 0.0);
    CHECK(inner, "(1) THE INNER FACES HOLD THE LIMIT: the area's mean within q of E / (1 - a'), at 16 and at 128 rays");
    CHECK(outer, "(2) FROM THE OTHER SIDE, NOTHING: every texel of the outer face within q of E (L9)");
    std::printf("\ngi.card_closed_form: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
