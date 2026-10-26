// gi.emitter_energy (PHOTON-7 stage 10-3, R2: energy is conserved) - AN EMITTER BRIGHTER THAN THE OLD MERGE CAP KEEPS
// ITS LIGHT. The voxeliser's order-independent merge (VoxelMerge_piece_cs.any) sums every contribution on a fixed-point
// grid; each contribution was CLAMPED to [0, 16] first, so any emitter over 16 entered the voxels at 16 - the light the
// store re-emits and the bounce multiplies was cut (a material of 20 stored 16.0000 at c0 and c1, measured). The
// emissive sum now takes a PER-VOXEL SHARED EXPONENT: a pre-pass writes each voxel's brightest contribution, and the
// sum is taken relative to it (2^-18 of it) - exact, order-free, coupled to nothing outside the voxel.
// THE FIXTURE: bounce_series' sealed room (8 x 4 x 8 m, 0.3 m slabs, albedo 0.5), every slab emitting E, High, rays,
// the field and the cards off, the chain run to its feedback's rest; two arms, E = 100 and 1000 (both over the old cap).
// THE LAWS:
//   (1) THE STORE HOLDS THE MATERIAL: cascades 0 and 1's emissive volumes peak at E within the half-float store's step
//       (2^-10 of E) and the merge's (2^-18);
//   (2) THE BOUNCE FOLLOWS E: the inner-wall voxels' mean radiance over E is the same in both arms within two
//       half-float steps (the series' limit E / (1 - a'(1 - e)) is linear in E; with (1) holding E in the store, the
//       room's operator is the same at 100 and 1000). Measured: L / E 1.5814 and 1.5818, and 1.5818 at E = 1.
// RED before the fix: both arms stored 16 (L / E 0.2531 and 0.0253). A fixture (the engine alone, one small offscreen
// view; 10 s budget, so two arms). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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
const float kRho = 0.5f;
const float kHalfX = 4.0f, kHalfZ = 4.0f, kHeight = 4.0f, kThick = 0.3f;

void slab(Scene *s, const Vec3 &centre, const Vec3 &size, float emission)
{
    PbrParams p;
    p.albedo = Colour(kRho, kRho, kRho);
    p.roughness = 1.0f;
    p.emissive = Colour(emission, emission, emission);
    const NodeId n = s->createNode();
    s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
    enginetest::setNodePosition(s, n, centre);
    enginetest::setNodeScale(s, n, size);
}

/// A cascade's inner-wall voxels' mean surface radiance (bounce_series' selection: covered, on the room's inner skin,
/// a metre from the faces' edges; the brightest channel over the coverage).
double innerWallMean(Scene *s, int cascade, size_t &count)
{
    count = 0;
    GiVoxelVolume v;
    if (!s->giVoxelLight(cascade, v) || !v.available || v.light.empty()) return -1.0;
    const float cell = v.cell[0];
    double sum = 0.0;
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
                sum += std::max(std::max(v.light[i], v.light[i + 1]), v.light[i + 2]) / c;
                ++count;
            }
    return count ? sum / double(count) : -1.0;
}
}   // namespace

int main(int, char **)
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-emitter-energy-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    const float arms[2] = { 100.0f, 1000.0f };
    double ratio[2] = { -1.0, -1.0 };
    for (int k = 0; k < 2; ++k) {
        const float E = arms[k];
        View *view = e->createOffscreenView("emitter_energy", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
        Scene *s = e->createScene("emitter_energy");
        if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
        view->setScene(s);
        float sh[27] = {};
        s->setAmbientSh(sh);
        const float w = 2.0f * kHalfX + 2.0f * kThick, d = 2.0f * kHalfZ + 2.0f * kThick;
        slab(s, Vec3(0.0f, -0.5f * kThick, 0.0f), Vec3(w, kThick, d), E);
        slab(s, Vec3(0.0f, kHeight + 0.5f * kThick, 0.0f), Vec3(w, kThick, d), E);
        slab(s, Vec3(-kHalfX - 0.5f * kThick, 0.5f * kHeight, 0.0f), Vec3(kThick, kHeight + 2.0f * kThick, d), E);
        slab(s, Vec3(kHalfX + 0.5f * kThick, 0.5f * kHeight, 0.0f), Vec3(kThick, kHeight + 2.0f * kThick, d), E);
        slab(s, Vec3(0.0f, 0.5f * kHeight, -kHalfZ - 0.5f * kThick), Vec3(w, kHeight + 2.0f * kThick, kThick), E);
        slab(s, Vec3(0.0f, 0.5f * kHeight, kHalfZ + 0.5f * kThick), Vec3(w, kHeight + 2.0f * kThick, kThick), E);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 2.0f, -3.0f));
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::Off;
        gi.cards = false;
        gi.bounces = true;
        gi.updateBudget = 0;
        if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: GI\n"); return 1; }
        e->waitForAsyncShaders();
        int f = 0, quiet = 0;
        for (; f < 900 && quiet < 8; ++f) {
            run(1);
            const GiStatus g = s->published().gi;
            quiet = (!g.cascades.empty() && !g.chainOwed && !g.feedbackOwed) ? quiet + 1 : 0;
        }
        size_t n0 = 0;
        const double L = innerWallMean(s, 0, n0);
        ratio[k] = L > 0.0 ? L / E : -1.0;
        bool storeOk = true;
        for (int c = 0; c < 2; ++c) {
            const GiVoxelStats vs = s->giVoxelStats(c);
            const double tol = E * (1.0 / 1024.0 + 1.0 / 262144.0);
            std::printf("   E %g: cascade %d's emissive store (%s) peaks at %.4f (%lld above 1)\n", E, c,
                        vs.emissiveFormat.c_str(), double(vs.peakEmissive), vs.emissiveAboveOne);
            storeOk = storeOk && std::fabs(double(vs.peakEmissive) - E) <= tol;
        }
        std::printf("   E %g: at rest after %d frames, %zu inner-wall voxels of cascade 0, mean radiance %.5f (L / E %.6f)\n",
                    E, quiet >= 8 ? f - quiet : -1, n0, L, ratio[k]);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "(1) E %g: cascades 0 and 1's emissive stores hold the material's E within the store's step", E);
        CHECK(quiet >= 8 && storeOk, msg);
        GiParams off;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        run(2);
        e->destroyScene(s);
        e->destroyView(view);
        run(2);
    }
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "(2) the room's radiance over E is the same at E 100 and 1000 within two half-float "
                                        "steps (%.6f against %.6f)", ratio[1], ratio[0]);
        CHECK(ratio[0] > 0.0 && ratio[1] > 0.0 && std::fabs(ratio[1] / ratio[0] - 1.0) <= 2.0 / 1024.0, msg);
    }
    std::printf("\ngi.emitter_energy: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
