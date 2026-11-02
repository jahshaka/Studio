// photon-sweep - BISTRO-REST-1's harness for THE SHARE's PRECISION LOOP (SPECS/BISTRO_REST_1_SPEC.md; spikes/bistro-rest-1).
// Bistro Exterior at Epic never came to rest: a few voxel halves orbited at period 2 (ABAB under the Jacobi door, 2-18
// fp16 steps), and gi.hitDirect 0 ended it - the orbit lives in the hit read's bounce SHARE, whose precision trust
// (jah_rq_hit.glsl jahShareTrust) is a steep function of the store's own value where a half's premultiplied light
// (radiance x its coverage k) is a few subnormal quanta: LOW-COVERAGE halves in SHADE, read by the surfaces they light.
// This scene puts that coupling everywhere, across the values: 41 columns, one per albedo 0.30 .. 0.70 in 0.01 steps (the
// column's every surface), each a SUNLIT PANEL facing +Z (the environments' sun, labscene.cpp: the light toward -Z and
// down, pitch 40 degrees) and, 0.12 m in front of it, five THIN FINS (0.04 m deep, 0.35 m tall) whose shaded backs (-Z)
// face the panel - widths 0.02 / 0.05 / 0.09 / 0.16 / 0.30 m, so a fin's halves hold k from about 0.01 (cascade 3's
// 1.875 m cells) to 1 (cascade 0's 0.078 m), and a pair of every coverage meets every albedo somewhere on the ramp. Each
// column is shifted 13 mm more than the last along x, so the fins straddle the cells' planes differently. The camera
// stands 4 m out at eye height: cascade 0 and cascade 1 both hold the wall (their seam crosses it).
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
    fixedBox(b, unit, "ground", V(-20.3f, -0.3f, -10.3f), V(20.3f, 0.02f, 12.3f), b.material(0.5f, 0.5f, 0.5f, 1.0f));
    static const float kFinWidth[5] = { 0.02f, 0.05f, 0.09f, 0.16f, 0.30f };
    for (int i = 0; i <= 40; ++i) {
        const float rho = 0.30f + 0.01f * float(i);
        const auto mat = b.material(rho, rho, rho, 1.0f);
        const float x = -14.0f + 0.7f * float(i) + 0.013f * float(i);
        const QString col = QStringLiteral("c%1").arg(i, 2, 10, QLatin1Char('0'));
        fixedBox(b, unit, col + QStringLiteral("-panel"), V(x - 0.3f, 0.03f, -0.53f), V(x + 0.3f, 3.03f, -0.43f), mat);
        for (int j = 0; j < 5; ++j) {
            const float y0 = 0.27f + 0.55f * float(j), w = kFinWidth[j];
            fixedBox(b, unit, col + QStringLiteral("-fin%1").arg(j), V(x - 0.5f * w, y0, -0.31f), V(x + 0.5f * w, y0 + 0.35f, -0.27f),
                     mat);
        }
    }
    return b.finish();
}

void stopSweep(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0.0f, 1.6f, 4.0f);
    target = iris::Vec3(0.0f, 1.4f, 0.0f);
}

std::vector<Prediction> expectSweep(const Built &, const Optics &)
{
    return { { "storeMovesAtRest", 0.0, "the voxel store's texels that change frame to frame at a still camera, every cascade" } };
}

std::string describeSweep()
{
    return "photon-sweep: 41 sunlit panels, albedo 0.30-0.70 in 0.01 steps, each with five thin fins 0.12 m in front (0.02-0.30 m "
           "wide) whose shaded backs face it - low-coverage halves in shade read by the surfaces they light, at every value. "
           "Predicts A STILL CAMERA RESTS: the store steady frame to frame (the share's precision loop holds no orbit), every "
           "reader held.";
}

}   // namespace

Env photonSweep() { return Env{ "photon-sweep", describeSweep, buildSweep, stopSweep, expectSweep, 500.0f, false }; }

}   // namespace envs
}   // namespace lab
