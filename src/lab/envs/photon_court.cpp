// photon-court — Photon's court fixture (LAB-1 stage 2). Not dollied (its law does not move with the distance);
// `--env-list` prints the law at every stop, and the Photon spec measures it.
//
// photon-court — THE COURT AT SCALE. A 20 x 20 m court floor, walls on three sides (x = -10, x = +10, z = -10; 8 m
// tall), open to the sky and to +Z, and a two-sided canopy (an 8 x 8 m quad, 4 m up, over the back half).
// THE EXPECTATION. (1) Every probe of the court sees geometry in at least two faces (the floor and a wall, or the
// canopy): all kept (the selftest's court: 18 of 18 since PHOTON-5). (2) A TWO-SIDED canopy's voxel darkness equals the
// two-winding reference (gi.voxel_two_sided's law: both coverage halves and both normals) — the ratio 1.
#include "envbuild.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include <cmath>

namespace lab {
namespace envs {

namespace {

constexpr float kRho = 0.5f;

// ---- photon-court --------------------------------------------------------------------------------------------------

Built buildCourt(unsigned seed, bool bake)
{
    Builder b("photon-court", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    const auto matte = b.material(kRho, kRho, kRho, 1.0f);
    using V = iris::Vec3;
    fixedBox(b, unit, "floor", V(-10.3f, -0.3f, -10.3f), V(10.3f, 0, 10.3f), matte);
    fixedBox(b, unit, "wall-west", V(-10.3f, 0, -10.3f), V(-10.0f, 8, 10.0f), matte);
    fixedBox(b, unit, "wall-east", V(10.0f, 0, -10.3f), V(10.3f, 8, 10.0f), matte);
    fixedBox(b, unit, "wall-north", V(-10.0f, 0, -10.3f), V(10.0f, 8, -10.0f), matte);
    Arrays c;
    c.quad(V(-4, 4, -2), V(4, 4, -2), V(4, 4, -10), V(-4, 4, -10), V(0, -1, 0));
    const int o = b.instance(b.mesh(c), QStringLiteral("canopy"), QStringLiteral("subject"), V(0, 0, 0), 0.0f,
                             V(1, 1, 1), b.material(0.8f, 0.3f, 0.2f, 1.0f), true, true);
    Feature f;
    f.kind = Feature::Fixed;
    f.object = o;
    f.centre = V(0, 4, -6);
    f.radius = 5.7f;
    f.triangles = 2;
    b.feature(f);
    return b.finish();
}

void stopCourt(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 3, 18);
    target = iris::Vec3(0, 3, -6);
}

std::vector<Prediction> expectCourt(const Built &, const Optics &)
{
    return { { "probesKeptShare", 1.0, "every probe of the court sees >= 2 faces of geometry: all kept (18 of 18)" },
             { "canopyTwoSidedRatio", 1.0, "the two-sided canopy's voxel darkness / the two-winding reference" } };
}

std::string describeCourt()
{
    return "photon-court: the court at scale (a 20 m floor, walls on three sides, open sky) with a two-sided 8 m "
           "canopy. Predicts THE KEEP RULE — every probe sees >= 2 faces of geometry: all kept — and the canopy's "
           "two-sided voxel darkness EQUAL to the two-winding reference (ratio 1).";
}

}   // namespace

Env photonCourt() { return Env{ "photon-court", describeCourt, buildCourt, stopCourt, expectCourt, 500.0f, false }; }

}   // namespace envs
}   // namespace lab
