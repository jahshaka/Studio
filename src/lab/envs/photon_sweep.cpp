// photon-sweep - BISTRO-REST-1's harness for THE SHARE's PRECISION LOOP AT A CASCADE SEAM (SPECS/BISTRO_REST_1_SPEC.md;
// spikes/bistro-rest-1). Bistro Exterior at Epic never came to rest: a few voxel halves orbited at period 2 (ABAB under the
// Jacobi door, 2-18 fp16 steps; gi.hitDirect 0 ended it). THE LOOP: a half of cascade 1 just under its TOP FACE gathers by
// TLAS rays; the ones that meet thin geometry just ABOVE the face are read in cascade 2 or 3, whose coarse cells give that
// geometry a tiny coverage k - so the half's premultiplied light (radiance x k) is a few subnormal quanta, where the hit
// read's bounce share was weighed by a precision TRUST that ramps over exactly that range (the shipped jahShareTrust,
// jah_rq_hit.glsl: its slope amplified the share up to 3x) - and those coarse halves gather back from cascade 1 (the TLAS
// route reads inward). Bistro's toggling cells: cascade 1's top brick layer and cascade 3's halves of k 0.09 / 0.14 at
// 3.8e-6 / 1.35e-5, over building_03's zinc roof. A sunlit or a merely dim scene is not enough (this env's first two forms,
// a sunlit wall and a sealed corridor of the same fins, rested in 187 and 296 frames): the loop needs the SEAM.
// THE SCENE, at the Bistro walk's start pose so Epic places the chain exactly as there (c1 [-20, 0] x [0, 20] x [-5, 15],
// c2 [-22.5, 7.5] x [-7.5, 22.5] x [-7.5, 22.5], c3's 15 m bricks): a ROOF SLAB x [-19.5, -0.5], z [-4.5, 14.5], its top
// at 19.83 m (cascade 1's top cell layer), and on it 41 COLUMNS of THIN FINS (one per albedo 0.30 .. 0.70 in 0.01 steps,
// every surface of the column that albedo), five fins a column along z - 0.02 / 0.05 / 0.09 / 0.16 / 0.30 m thick in x,
// 1.0 m long, standing 19.83 .. 20.53 m ACROSS cascade 1's top face. The sun (the environments', labscene.cpp: toward -Z
// and down, pitch 40 degrees) lights the roof and the fins' +Z ends; the fins' +-X sides take no direct sun (D = 0) and
// face each other and the roof. Each column is 13 mm further along x than the even spacing, so the fins straddle the
// cells' planes differently; posts hold the slab (shadowing nothing at the seam).
// THE EXPECTATION: a still camera rests - the store steady frame to frame, the feedback at rest, every reader held
// (gi.still_rest_epic).
#include "envbuild.h"

namespace lab {
namespace envs {

namespace {

Built buildSweep(unsigned seed, bool bake)
{
    Builder b("photon-sweep", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    using V = iris::Vec3;
    const auto grey = b.material(0.5f, 0.5f, 0.5f, 1.0f);
    fixedBox(b, unit, "ground", V(-40.3f, -0.3f, -30.3f), V(20.3f, 0.02f, 35.3f), grey);
    fixedBox(b, unit, "roof", V(-19.5f, 19.43f, -4.5f), V(-0.5f, 19.83f, 14.5f), grey);
    for (int p = 0; p < 4; ++p) {
        const float px = p % 2 ? -1.3f : -18.7f, pz = p / 2 ? 13.7f : -3.7f;
        fixedBox(b, unit, QStringLiteral("post%1").arg(p), V(px - 0.3f, 0.02f, pz - 0.3f), V(px + 0.3f, 19.43f, pz + 0.3f), grey);
    }
    static const float kFinWidth[5] = { 0.02f, 0.05f, 0.09f, 0.16f, 0.30f };
    for (int i = 0; i <= 40; ++i) {
        const float rho = 0.30f + 0.01f * float(i);
        const auto mat = b.material(rho, rho, rho, 1.0f);
        const float x = -19.2f + 0.45f * float(i) + 0.013f * float(i);
        for (int j = 0; j < 5; ++j) {
            const float w = kFinWidth[j], z0 = -4.2f + 3.7f * float(j);
            fixedBox(b, unit, QStringLiteral("c%1-fin%2").arg(i, 2, 10, QLatin1Char('0')).arg(j), V(x - 0.5f * w, 19.83f, z0),
                     V(x + 0.5f * w, 20.53f, z0 + 1.0f), mat);
        }
    }
    // THE PAIR BY NUMBERS (gi.sidePick's read on Bistro, stage 0): cascade 3's orbiting voxel (28, 37, 36) READ ITSELF - its
    // +Y half (k 0.33) cast its rays into the BACK of a one-sided sheet 6 mm above it (an answer of 0), and its few front
    // hits 4-10 mm away were answered by its own coarse cell's other half, whose premultiplied T stood at 3.4e-6 / 5.4e-5
    // in the two states (trust 0 / 1, share 0.048 / 1, the answer 1.8e-4 / 3.8e-3); its +Z half the mirror image. So: a
    // PLATE with a ONE-SIDED SHEET a few millimetres above it (facing up: seen from the plate, a back face), the plate's
    // lit edge a few millimetres past the sheet's, inside cascade 3's cells at that height (y 24.375 .. 26.25, 14 m over
    // the eye: cascade 3 answers there) - one stack a cell along x, the gap 3 .. 20 mm and the plate's width 0.3 .. 1.7 m
    // swept, so a cell's halves land on every coverage and every dimness.
    {
        const auto plateMat = b.material(0.62f, 0.62f, 0.64f, 0.35f, 1.0f);   // Bistro's zinc there
        static const float kGap[5] = { 0.003f, 0.005f, 0.008f, 0.012f, 0.020f };
        for (int i = 0; i < 24; ++i) {
            const float cx = -45.0f + 1.875f * float(i) + 0.9375f;   // the c3 cell's centre along x
            const float w = 0.3f + 1.4f * float(i % 8) / 7.0f, gap = kGap[i % 5];
            const float y0 = 25.05f;
            fixedBox(b, unit, QStringLiteral("stack%1-plate").arg(i), V(cx - 0.5f * w, y0 - 0.02f, 7.75f),
                     V(cx + 0.5f * w, y0, 9.15f), plateMat);
            Arrays sh;   // the one-sided sheet over it, facing +Y, 30 mm short of the plate's +Z edge (its lit sill)
            sh.quad(V(cx - 0.5f * w - 0.05f, y0 + gap, 9.12f), V(cx + 0.5f * w + 0.05f, y0 + gap, 9.12f),
                    V(cx + 0.5f * w + 0.05f, y0 + gap, 7.70f), V(cx - 0.5f * w - 0.05f, y0 + gap, 7.70f), V(0, 1, 0));
            b.instance(b.mesh(sh), QStringLiteral("stack%1-sheet").arg(i), QStringLiteral("subject"), V(0, 0, 0), 0.0f,
                       V(1, 1, 1), plateMat, true, false);
        }
    }
    return b.finish();
}

void stopSweep(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(-10.365f, 11.211f, 4.793f);
    target = iris::Vec3(-4.461f, 9.705f, -0.392f);
}

std::vector<Prediction> expectSweep(const Built &, const Optics &)
{
    return { { "storeMovesAtRest", 0.0, "the voxel store's texels that change frame to frame at a still camera, every cascade" } };
}

std::string describeSweep()
{
    return "photon-sweep: a roof at cascade 1's top face (Epic, the Bistro walk's start pose) carrying 41 columns of thin fins "
           "(albedo 0.30-0.70 in 0.01 steps; 0.02-0.30 m thick) that stand across the face into cascades 2 and 3 - shaded "
           "low-coverage halves in coarse cells read from the finer cascade and reading it back. Predicts A STILL CAMERA RESTS: "
           "the store steady frame to frame (the share's precision loop holds no orbit), every reader held.";
}

}   // namespace

Env photonSweep() { return Env{ "photon-sweep", describeSweep, buildSweep, stopSweep, expectSweep, 500.0f, false }; }

}   // namespace envs
}   // namespace lab
