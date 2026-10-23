// gi.bounce_series (PHOTON-7 stage 4; SPECS/PHOTON_7_SPEC.md stage 4) — THE BOUNCES CONVERGE BY FEEDBACK TO THE SERIES'
// LIMIT, read in the voxel STORE (Scene::giVoxelVolume - no gather, no field, no picture).
//
// A SEALED 8 x 4 x 8 m room (0.3 m slabs, every seam overlapped) whose inner walls EMIT a uniform radiance E = 1 and
// reflect with albedo rho = 0.5 at roughness 1 (no lamp, no sky, no ambient): a wall voxel's surface radiance is
//     L = E + k L_seen,   k = a' (1 - e)
// (a' the surface's re-emission of what it gathers - the diffuse lobe's hemispherical albedo times rho plus the 4 %
// layer's - and e the share of the gather's cones that escape the sealed room: the cones' own leak), whose series
// E (1 + k + k^2 + ...) has the limit E / (1 - k). THE ROOM'S OWN ARM MEASURES k: one Jacobi pass from the direct term
// (the measuring door gi.bounceJacobi, one frame) gives L1 = E + k E, so k = (L1 - E) / E - the product the limit needs,
// with the escape in it (e = 1 - k / a' is printed against the diffuse a' = rho x DA_hemi(1) = 0.351).
// THE LAWS, per tier (Low, Medium, High, Epic), each on cascade 0's inner-wall voxels (covered, a cell inside the
// room's faces, a metre from its edges), surface radiance = the store's total over its coverage:
//   (1) FEEDBACK = A 64-PASS JACOBI WITHIN ONE CODE: the feedback run to its rest (no brick active) against 64 Jacobi
//       passes from the direct term (the door, 64 frames), every voxel within 1 % of the Jacobi's radiance - the stop
//       law's own bound (a brick stops once the tail of its series is under a code);
//   (2) BOTH WITHIN 3 % OF THE ANALYTIC LIMIT E / (1 - k), on the voxels' mean, k measured above (the physics with its
//       own leak: the bar is the room's, never a loosened constant);
//   (3) THE FEEDBACK STOPS: the chain owes nothing at its rest, and it ran fewer brick-passes than the Jacobi's 64 a brick
//       (it stops the converged bricks).
// A fixture (the engine alone, one small offscreen view; rays, the gather, the field and the cards off). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
const float kE = 1.0f;       // the walls' emission (radiance)
const float kRho = 0.5f;     // their albedo
const float kHalfX = 4.0f, kHalfZ = 4.0f, kHeight = 4.0f, kThick = 0.3f;

NodeId slab(Scene *s, const Vec3 &centre, const Vec3 &size)
{
    PbrParams p;
    p.albedo = Colour(kRho, kRho, kRho);
    p.roughness = 1.0f;
    p.emissive = Colour(kE, kE, kE);
    const NodeId n = s->createNode();
    s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
    enginetest::setNodePosition(s, n, centre);
    enginetest::setNodeScale(s, n, size);
    return n;
}

struct Reading {
    bool ok = false;
    size_t voxels = 0;
    double mean = 0.0;
    std::vector<float> L;   // the selected voxels' surface radiance (the brightest channel), in selection order
};

/// Cascade 0's inner-wall voxels: covered (c > 0.5), centred within one cell inside a face of the room, a metre from
/// its edges; L = the store's total / its coverage (the total is premultiplied by c).
Reading read(Scene *s)
{
    Reading r;
    GiVoxelVolume v;
    if (!s->giVoxelVolume(0, v) || !v.available || v.light.empty()) return r;
    const float cell = v.cell[0];
    for (int z = 0; z < v.depth; ++z)
        for (int y = 0; y < v.height; ++y)
            for (int x = 0; x < v.width; ++x) {
                const float p[3] = { v.origin[0] + cell * (x + 0.5f), v.origin[1] + cell * (y + 0.5f),
                                     v.origin[2] + cell * (z + 0.5f) };
                // inside the room's inner box grown by a cell, and not inside it shrunk by a cell: the inner skin
                const bool inGrown = std::fabs(p[0]) < kHalfX + cell && std::fabs(p[2]) < kHalfZ + cell &&
                                     p[1] > -cell && p[1] < kHeight + cell;
                const bool inShrunk = std::fabs(p[0]) < kHalfX - cell && std::fabs(p[2]) < kHalfZ - cell &&
                                      p[1] > cell && p[1] < kHeight - cell;
                if (!inGrown || inShrunk) continue;
                // a metre from every edge of the face it is on (the corners' gather sees another mix)
                int near = 0;
                near += std::fabs(p[0]) > kHalfX - 1.0f ? 1 : 0;
                near += std::fabs(p[2]) > kHalfZ - 1.0f ? 1 : 0;
                near += (p[1] < 1.0f || p[1] > kHeight - 1.0f) ? 1 : 0;
                if (near > 1) continue;
                const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                const float c = v.light[i + 3];
                if (!(c > 0.5f)) continue;
                const float L = std::max(std::max(v.light[i], v.light[i + 1]), v.light[i + 2]) / c;
                r.L.push_back(L);
                r.mean += L;
            }
    r.voxels = r.L.size();
    r.ok = r.voxels > 0;
    if (r.ok) r.mean /= double(r.voxels);
    return r;
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-bounce-series-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    const GiQuality tiers[4] = { GiQuality::Low, GiQuality::Medium, GiQuality::High, GiQuality::Epic };
    const char *names[4] = { "low", "medium", "high", "epic" };
    for (int t = 0; t < 4; ++t) {
        std::printf("\n== %s ==\n", names[t]);
        View *view = e->createOffscreenView("bounce_series", 64, 64, Colour(0, 0, 0));
        Scene *s = e->createScene("bounce_series");
        if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        const float w = 2.0f * kHalfX + 2.0f * kThick, d = 2.0f * kHalfZ + 2.0f * kThick;
        slab(s, Vec3(0.0f, -0.5f * kThick, 0.0f), Vec3(w, kThick, d));                      // the floor
        slab(s, Vec3(0.0f, kHeight + 0.5f * kThick, 0.0f), Vec3(w, kThick, d));             // the ceiling
        slab(s, Vec3(-kHalfX - 0.5f * kThick, 0.5f * kHeight, 0.0f), Vec3(kThick, kHeight + 2.0f * kThick, d));
        slab(s, Vec3(kHalfX + 0.5f * kThick, 0.5f * kHeight, 0.0f), Vec3(kThick, kHeight + 2.0f * kThick, d));
        slab(s, Vec3(0.0f, 0.5f * kHeight, -kHalfZ - 0.5f * kThick), Vec3(w, kHeight + 2.0f * kThick, kThick));
        slab(s, Vec3(0.0f, 0.5f * kHeight, kHalfZ + 0.5f * kThick), Vec3(w, kHeight + 2.0f * kThick, kThick));
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 2.0f, -3.0f));
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = tiers[t];
        gi.ddgi = GiToggle::Off;
        gi.gather = GiToggle::Off;
        gi.cards = false;
        gi.bounces = true;
        gi.updateBudget = 0;
        if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: the chain\n"); return 1; }
        e->waitForAsyncShaders();
        // (1) THE FEEDBACK TO ITS REST: the chain owes nothing for 8 frames in a row (cap 900).
        int f = 0, quiet = 0;
        for (; f < 900 && quiet < 8; ++f) {
            run(1);
            const GiStatus g = s->published().gi;
            quiet = (!g.cascades.empty() && !g.chainOwed && !g.feedbackOwed) ? quiet + 1 : 0;
        }
        const GiStatus atRest = s->published().gi;
        unsigned long long passes = 0, bricksRan = 0;
        unsigned bricks0 = 0;
        if (!atRest.cascades.empty()) {
            passes = atRest.cascades[0].feedbackPasses;
            bricksRan = atRest.cascades[0].feedbackBricksRan;
            bricks0 = atRest.cascades[0].bricks;
        }
        std::printf("   the feedback came to rest in %d frames: cascade 0 ran %llu passes over %llu bricks (%u bricks; %.1f "
                    "brick-passes a brick), %llu stops, %llu resumes, %llu saturated\n", f - quiet, passes, bricksRan,
                    bricks0, bricks0 ? double(bricksRan) / bricks0 : 0.0,
                    atRest.cascades.empty() ? 0ull : atRest.cascades[0].feedbackStops,
                    atRest.cascades.empty() ? 0ull : atRest.cascades[0].feedbackResumes,
                    atRest.cascades.empty() ? 0ull : atRest.cascades[0].feedbackSaturated);
        CHECK(quiet >= 8, "THE FEEDBACK STOPS: the chain owes nothing at its rest");
        const Reading fb = read(s);
        // THE ROOM'S OWN OPERATOR: one Jacobi pass from the direct term.
        e->setArm("gi.bounceJacobi", 1.0);
        run(1);   // the arm latches at the frame's top: that frame restarts from the direct term and runs pass 1
        const Reading j1 = read(s);
        run(63);
        const Reading j64 = read(s);
        e->setArm("gi.bounceJacobi", 0.0);
        run(2);
        if (!fb.ok || !j1.ok || !j64.ok || fb.voxels != j1.voxels || fb.voxels != j64.voxels) {
            CHECK(false, "the store reads back the same inner-wall voxels three times");
        } else {
            const double k = (j1.mean - kE) / kE;
            const double limit = kE / (1.0 - k);
            const double aDiffuse = kRho * 0.701832;   // rho x DA_hemi(1) (JahDiffuseAlbedo_piece_all.any)
            double worst = 0.0;
            size_t over = 0;
            for (size_t i = 0; i < fb.voxels; ++i) {
                const double rel = std::fabs(double(fb.L[i]) - double(j64.L[i])) / std::max(double(j64.L[i]), 1e-6);
                worst = std::max(worst, rel);
                over += rel > 0.01 ? 1u : 0u;
            }
            std::printf("   %zu inner-wall voxels (cell %.4f m): one Jacobi pass %.5f -> k = a'(1 - e) = %.5f (e = %.4f against "
                        "the diffuse a' %.4f); the limit E / (1 - k) = %.5f\n", fb.voxels,
                        double(s->published().gi.cascades.empty() ? 0.0f : s->published().gi.cascades[0].cell), j1.mean,
                        k, 1.0 - k / aDiffuse, aDiffuse, limit);
            std::printf("   feedback %.5f (%+.2f %% of the limit), Jacobi-64 %.5f (%+.2f %%); feedback vs Jacobi-64: worst "
                        "voxel %.3f codes, %zu voxels over one code\n", fb.mean, 100.0 * (fb.mean / limit - 1.0), j64.mean,
                        100.0 * (j64.mean / limit - 1.0), 100.0 * worst, over);
            CHECK(over == 0u, "(1) FEEDBACK = A 64-PASS JACOBI WITHIN ONE CODE at every inner-wall voxel");
            CHECK(std::fabs(fb.mean / limit - 1.0) <= 0.03 && std::fabs(j64.mean / limit - 1.0) <= 0.03,
                  "(2) BOTH WITHIN 3 % OF THE ANALYTIC LIMIT E / (1 - k), k the room's own measured operator");
            CHECK(bricks0 > 0u && bricksRan < 64ull * bricks0,
                  "(3) the feedback ran fewer brick-passes than the Jacobi's 64 a brick (it stops the converged ones)");
        }
        GiParams off;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        run(2);
        e->destroyScene(s);
        e->destroyView(view);
        run(2);
    }
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
