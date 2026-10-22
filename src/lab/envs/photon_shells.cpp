// photon-shells and photon-roof — PHOTON-7 stage 1's fixtures for LIGHT IS A WORLD FUNCTION (SPECS/PHOTON_7_SPEC.md L1;
// spikes/photon-7/PREFLIGHT_1_2.md §2.2). Not the court: gi.walk crosses photon-court and its base is recorded there.
// Not dollied; `--env-list` prints the law, and gi.light_is_world (tests/gi/light_is_world.sh) measures it through the
// lab's `pin` and `voxels` doors.
//
// THE SUN of every environment (labscene.cpp addSkyAndLights): pitch 40 degrees about +X, so the light travels
// (0, -0.766, -0.643) and a surface sees the sun along L = (0, 0.766, 0.643) - up and toward +Z, the courts' open side;
// a shadow falls toward -Z.
// THE PLANES RULE: every face of every piece lies at least 5 mm off every cascade's cell planes at every tier (cells
// 0.078 / 0.156 / 0.3125 / 0.469 / 0.625 / 1.875 m, all multiples of 0.078125 from the world origin): a face ON a cell
// plane sits on the shadow march's crossing tolerance (LightInjection_piece_cs.any jahInjectPlane), where one placement's
// rounding can flip a crossing the other's does not - a fixture that measures placement must not stand there. So the
// floor's top is 0.02, the walls' inner faces +-10.02 / -10.02 and their outer +-10.32, the walls 8.0 tall (102.4 fine
// cells), every occluder face at the preflight's heights (1.0 1.6 2.0 3.0 3.6 8.0 8.3: >= 6 mm off at every cell).
//
// photon-shells — THE CHAIN'S SHELLS. A 20.6 m court (the floor, walls on three sides, open to +Z and the sky) and:
//   OCCLUDER A, an opaque box x [-1.1, 1.1], y [1.0, 1.6], z [3.1, 4.9]: the floor's sun rays meet its underside at
//     t = (1.0 - 0.02) / 0.766 = 1.28 m (Epic/High: cascade 1's shell [0.625, 1.875); Medium/Low: cascade 1's), its
//     shadow on the floor z in [2.28, 4.08];
//   OCCLUDER B, the same at y [3.0, 3.6], z [-4.9, -3.1]: t = 3.89 m (Epic/High cascade 2's shell [1.875, 5.625);
//     Medium cascade 2's; Low cascade 1's), its shadow z in [-7.4, -5.6];
//   THE PARTIAL OCCLUDER, a two-sided cut-out plane (Builder::cutout's leaf mask) at y 2.0 over x [3, 7], z [-2, 2]: t =
//     2.58 m (Epic/High cascade 2), its shadow x in [3, 7], z in [-3.66, 0.34] - clear of A's and B's;
//   THE LAMP, a point light of intensity 8 (iris::lens::kDefaultLampIntensity), range 1.5 m, at (-7, 1, 7): a cascade-0
//     box placed at (0, 1.6, 0) (x, z in [-5, 5]) misses its range sphere, one placed at (-2, 1.6, 2) holds it - the
//     multiplier arm's two pins.
// THE EXPECTATION (L1): the DIRECT term of a world voxel is the same at every placement of its cascade - within the
// store's quantum (one RGBA16F step) - wherever its shells lie inside their boxes ("band-free": the voxel's march over
// its own cascade's shell, D_k plus the hand-over to the next cell plane, stays inside the box at both placements).
//
// photon-roof — THE LAST CASCADE TO ITS EXIT. The court without occluders, under an opaque plate y [8.0, 8.3] over
// x [-10.32, 10.32], z [-10.32, 4] resting on the walls: the floor's sun rays meet its underside at t = 7.98 / 0.766 =
// 10.42 m - outside cascade 0's box at every tier (cascade 0 is 5 m half: its top is the eye + 5 m) and inside the
// outermost cascade (Epic/High/Medium cascade 3, Low cascade 1). Every floor point with z < 4 - 0.643 t = -2.7 is under
// it. THE EXPECTATION: the DIRECT term of every floor voxel of every cascade in that shadow is 0 (the direct volume holds
// the lamps, the sun and the emission; the sky enters the total only) - the reference D_sun = 0 exactly, at every tier.
#include "envbuild.h"

#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace lab {
namespace envs {

namespace {

constexpr float kRho = 0.5f;

/// THE COURT BOTH SHARE: the floor (top 0.02) and walls on three sides (inner faces at +-10.02 and -10.02, 8.0 tall).
void courtShell(Builder &b, const iris::MeshPtr &unit, const iris::PbrMaterialPtr &matte)
{
    using V = iris::Vec3;
    fixedBox(b, unit, "floor", V(-10.32f, -0.3f, -10.32f), V(10.32f, 0.02f, 10.32f), matte);
    fixedBox(b, unit, "wall-west", V(-10.32f, -0.3f, -10.32f), V(-10.02f, 8.0f, 10.02f), matte);
    fixedBox(b, unit, "wall-east", V(10.02f, -0.3f, -10.32f), V(10.32f, 8.0f, 10.02f), matte);
    fixedBox(b, unit, "wall-north", V(-10.02f, -0.3f, -10.32f), V(10.02f, 8.0f, -10.02f), matte);
}

Built buildShells(unsigned seed, bool bake)
{
    Builder b("photon-shells", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    const auto matte = b.material(kRho, kRho, kRho, 1.0f);
    courtShell(b, unit, matte);
    using V = iris::Vec3;
    fixedBox(b, unit, "occluder-a", V(-1.1f, 1.0f, 3.1f), V(1.1f, 1.6f, 4.9f), matte);
    fixedBox(b, unit, "occluder-b", V(-1.1f, 3.0f, -4.9f), V(1.1f, 3.6f, -3.1f), matte);
    Arrays c;
    c.quad(V(3, 2, 2), V(7, 2, 2), V(7, 2, -2), V(3, 2, -2), V(0, -1, 0));
    b.instance(b.mesh(c), QStringLiteral("partial"), QStringLiteral("subject"), V(0, 0, 0), 0.0f, V(1, 1, 1),
               b.cutout(kRho, kRho, kRho, 1.0f, 8), true, true);
    auto lamp = iris::LightNode::create();
    lamp->setLightType(iris::LightType::Point);
    lamp->setName(QStringLiteral("lamp"));
    b.built().doc->getRootNode()->addChild(lamp);
    lamp->setLocalPos(V(-7, 1, 7));
    lamp->setPropertyValue(QStringLiteral("intensity"), iris::lens::kDefaultLampIntensity);
    lamp->setPropertyValue(QStringLiteral("distance"), 1.5f);
    return b.finish();
}

Built buildRoof(unsigned seed, bool bake)
{
    Builder b("photon-roof", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    const auto matte = b.material(kRho, kRho, kRho, 1.0f);
    courtShell(b, unit, matte);
    using V = iris::Vec3;
    fixedBox(b, unit, "roof", V(-10.32f, 8.0f, -10.32f), V(10.32f, 8.3f, 4.0f), matte);
    return b.finish();
}

void stopCourt(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 1.6f, 0);
    target = iris::Vec3(0, 1.6f, -5);
}

std::vector<Prediction> expectShells(const Built &, const Optics &)
{
    return { { "directPlacementSteps", 0.0,
               "the band-free direct voxels that differ by more than one RGBA16F step between two placements of a cascade" } };
}

std::vector<Prediction> expectRoof(const Built &, const Optics &)
{
    return { { "directUnderRoof", 0.0, "the direct term of every floor voxel under the plate (z < -2.7), every cascade, every tier" } };
}

std::string describeShells()
{
    return "photon-shells: a 20.6 m court with an opaque occluder in cascade 1's shell (A, 1.28 m up the sun ray) and one in "
           "cascade 2's (B, 3.89 m), a cut-out partial plane at 2.0 m and a range-1.5 m lamp of 8 at (-7, 1, 7), every face "
           ">= 5 mm off every cell plane. Predicts LIGHT IS A WORLD FUNCTION: a world voxel's direct term at two placements "
           "of its cascade equal within one RGBA16F step wherever its shells lie inside their boxes.";
}

std::string describeRoof()
{
    return "photon-roof: the court under an opaque plate 8.0-8.3 m up (z < 4): the floor's sun rays meet it 10.42 m out, past "
           "cascade 0's box at every tier. Predicts the march THROUGH the chain to the last cascade's exit: the direct term "
           "of every floor voxel under the plate is 0 at every tier.";
}

}   // namespace

Env photonShells() { return Env{ "photon-shells", describeShells, buildShells, stopCourt, expectShells, 500.0f, false }; }
Env photonRoof() { return Env{ "photon-roof", describeRoof, buildRoof, stopCourt, expectRoof, 500.0f, false }; }

}   // namespace envs
}   // namespace lab
