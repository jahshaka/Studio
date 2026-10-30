// photon-seam - BISTRO-REST-1's harness scene: ROOFTOPS ACROSS THE CASCADES' SEAMS (SPECS/BISTRO_REST_1_SPEC.md stage 0).
// Bistro Exterior at Epic never came to rest at a still camera (spikes/bistro-rest-1/stage0): a handful of voxels kept
// toggling - c1's top brick layer (y 18.75-20, x -17.5..-15, z 7.5..14) and c2 / c3 at the same world spot - and the
// feedback's resume rule spread their moves to every reader. This scene rebuilds that place without the Bistro: the
// camera is the walk's start pose (-10.365, 11.211, 4.793 -> -4.461, 9.705, -0.392), so at Epic the chain is placed
// exactly as there (c1 [-20, 0] x [0, 20] x [-5, 15], c2 [-22.5, 7.5] x [-7.5, 22.5] x [-7.5, 22.5], c3 15 m bricks),
// and rooftops cross the seams around that spot:
//   A, the seam block: walls x [-19.7, -13.3], z [6.3, 14.7] to 17.6 m; a gable roof (two 0.25 m slabs, ridge along z at
//     x -16.5, 21.4 m) crossing c1's top face (y 20) and lying in c2 and c3; a chimney to 22.1 m;
//   B, across the lane: walls x [-19.7, -13.3], z [-4.7, 3.7] to 19.6 m, a flat roof with a parapet to 20.35 (across y 20);
//   C, behind both, taller: x [-29.7, -21.0], z [2.0, 16.0] to 24.3 m - past c1's -X face and c2's top face (22.5);
//   D, on the camera's other side: x [-6.0, 2.0], z [8.0, 16.0] to 19.2 m.
// Every surface matte, albedo 0.6 (the facades) or 0.5 (the ground); the environments' sun (labscene.cpp
// addSkyAndLights: pitch 40 degrees, the light toward -Z). Faces kept off the cells' planes where cheap (not a law here).
// THE EXPECTATION: a still camera in a still scene comes to rest - the voxel store's content steady frame to frame (a
// hash), the feedback at rest, every reader held (gi.still_rest_epic). Nothing in it moves.
#include "envbuild.h"

#include <cmath>

namespace lab {
namespace envs {

namespace {

constexpr float kFacade = 0.6f, kGround = 0.5f;

Built buildSeam(unsigned seed, bool bake)
{
    Builder b("photon-seam", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    const auto facade = b.material(kFacade, kFacade, kFacade, 1.0f);
    const auto ground = b.material(kGround, kGround, kGround, 1.0f);
    using V = iris::Vec3;
    fixedBox(b, unit, "ground", V(-60.3f, -0.3f, -40.3f), V(40.3f, 0.02f, 50.3f), ground);
    // A: the seam block, its gable and chimney
    fixedBox(b, unit, "a-walls", V(-19.7f, 0.02f, 6.3f), V(-13.3f, 17.6f, 14.7f), facade);
    {
        // two slabs from the eaves (x -19.9 / -13.1, 17.6 m) to the ridge (x -16.5, 21.4 m), 0.25 m thick, z [6.1, 14.9]
        const float run = 3.4f, rise = 3.8f, len = std::sqrt(run * run + rise * rise);
        Arrays g;
        const float a = run / len, c = rise / len;
        for (int side = 0; side < 2; ++side) {
            // u along the slope toward the ridge, v the slab's outward normal (up and away from the ridge), w = u x v
            const float sx = side == 0 ? 1.0f : -1.0f;   // the -X slope rises toward +x, the +X slope toward -x
            const V u(sx * a, c, 0.0f), v(-sx * c, a, 0.0f), w(0.0f, 0.0f, sx);
            const V centre(-16.5f - sx * run * 0.5f, 17.6f + rise * 0.5f, 10.5f);
            g.orientedBox(centre, u, v, w, V(len * 0.5f + 0.15f, 0.125f, 4.4f));
        }
        b.instance(b.mesh(g), QStringLiteral("a-roof"), QStringLiteral("subject"), V(0, 0, 0), 0.0f, V(1, 1, 1), facade,
                   true, false);
    }
    fixedBox(b, unit, "a-chimney", V(-15.2f, 17.0f, 10.0f), V(-14.6f, 22.1f, 10.6f), facade);
    // B: the flat roof and its parapet across y 20
    fixedBox(b, unit, "b-walls", V(-19.7f, 0.02f, -4.7f), V(-13.3f, 19.6f, 3.7f), facade);
    fixedBox(b, unit, "b-parapet-w", V(-19.7f, 19.6f, -4.7f), V(-19.4f, 20.35f, 3.7f), facade);
    fixedBox(b, unit, "b-parapet-e", V(-13.6f, 19.6f, -4.7f), V(-13.3f, 20.35f, 3.7f), facade);
    fixedBox(b, unit, "b-parapet-n", V(-19.4f, 19.6f, -4.7f), V(-13.6f, 20.35f, -4.4f), facade);
    fixedBox(b, unit, "b-parapet-s", V(-19.4f, 19.6f, 3.4f), V(-13.6f, 20.35f, 3.7f), facade);
    // C: taller, behind, across c1's -X face and c2's top
    fixedBox(b, unit, "c-tower", V(-29.7f, 0.02f, 2.0f), V(-21.0f, 24.3f, 16.0f), facade);
    // D: the camera's other side
    fixedBox(b, unit, "d-block", V(-6.0f, 0.02f, 8.0f), V(2.0f, 19.2f, 16.0f), facade);
    return b.finish();
}

void stopSeam(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(-10.365f, 11.211f, 4.793f);
    target = iris::Vec3(-4.461f, 9.705f, -0.392f);
}

std::vector<Prediction> expectSeam(const Built &, const Optics &)
{
    return { { "storeMovesAtRest", 0.0, "the voxel store's texels that change frame to frame at a still camera, every cascade" } };
}

std::string describeSeam()
{
    return "photon-seam: rooftops across the Epic chain's seams at the Bistro walk's start pose - a gable roof and a chimney "
           "through cascade 1's top face (20 m) inside cascades 2 and 3, a parapet across it, a tower past cascade 1's -X face "
           "and cascade 2's top. Predicts A STILL CAMERA RESTS: the voxel store steady frame to frame, the feedback at rest, "
           "every reader held.";
}

}   // namespace

Env photonSeam() { return Env{ "photon-seam", describeSeam, buildSeam, stopSeam, expectSeam, 500.0f, false }; }

}   // namespace envs
}   // namespace lab
