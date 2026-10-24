// photon-area — THE AREA LIGHT'S PENUMBRA FIXTURE (PHOTON-7 stage 7: the walk 7-A owed, run at the stage's master check;
// the owner's law - every light is blocked by a wall on every path).
//
// photon-room's sealed shell (envbuild.h roomShell, its lamp OFF: the area light is the room's only light) with ONE
// AREA LIGHT, a 2 x 1 m rectangle 3 m over the floor at z -1.5, shining down (intensity 6, range 12 m), and an OPAQUE
// PLATE 1.8 m up (0.05 m thick) over the floor's -x half, x in [-3.5, 0], z in [-3.5, 0.5]: the plate stops the light
// over its half of the floor, and its edge at x 0 throws a PENUMBRA as wide as the light's 2 m width seen past the
// plate, 2 x 1.8 / (3 - 1.8) = 3 m on the floor, centred under the edge - every stage of the ratio estimator (the umbra,
// the penumbra's slope, the lit floor) in one view. A GLOSSY METAL STRIP 0.6 m wide (perceptual roughness 0.3) runs
// across the floor through the penumbra at z in [-1.8, -1.2]: the specular-weighted ratio's surface (the walk's
// GLOSSY class), beside the floor's PLASTER (0.9). The walk (gi.walk_area) crosses the penumbra under the plate's
// height. THE EXPECTATION is the shell's: THE KEEP RULE - every probe of the sealed room sees >= 2 faces, all kept.
#include "envbuild.h"

#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include <cmath>

namespace lab {
namespace envs {

namespace {

constexpr float kRho = 0.5f;

Built buildArea(unsigned seed, bool bake)
{
    Builder b("photon-area", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    roomShell(b, unit, b.material(kRho, kRho, kRho, 0.9f), false);
    using V = iris::Vec3;
    fixedBox(b, unit, "plate", V(-3.5f, 1.8f, -3.5f), V(0.0f, 1.85f, 0.5f), b.material(kRho, kRho, kRho, 0.9f));
    fixedBox(b, unit, "glossy-strip", V(-3.0f, 0.0f, -1.8f), V(3.0f, 0.02f, -1.2f),
             b.material(0.9f, 0.9f, 0.9f, 0.3f, 1.0f));
    auto light = iris::LightNode::create();
    light->setLightType(iris::LightType::Area);
    light->setName(QStringLiteral("area"));
    b.built().doc->getRootNode()->addChild(light);
    light->setLocalPos(V(0.0f, 3.0f, -1.5f));
    light->setPropertyValue(QStringLiteral("rectWidth"), 2.0f);
    light->setPropertyValue(QStringLiteral("rectHeight"), 1.0f);
    light->setPropertyValue(QStringLiteral("intensity"), 6.0f);
    light->setPropertyValue(QStringLiteral("distance"), 12.0f);
    return b.finish();
}

void stopArea(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(2.0f, 1.4f, 2.5f);
    target = iris::Vec3(0.0f, 0.0f, -1.5f);
}

std::vector<Prediction> expectArea(const Built &, const Optics &)
{
    return { { "probesKeptShare", 1.0, "every probe of the sealed shell sees >= 2 faces of geometry: all kept" } };
}

std::string describeArea()
{
    return "photon-area: photon-room's sealed shell, its lamp off, with ONE AREA LIGHT (2 x 1 m, 3 m over the floor, "
           "shining down) and an opaque plate 1.8 m up over the floor's -x half whose edge throws a 3 m PENUMBRA across "
           "the floor (the umbra, the slope, the lit floor in one view), a glossy metal strip (0.3) running through it "
           "(PHOTON-7 stage 7: the area visibility's walk, gi.walk_area - its M1 and live residual). Predicts THE KEEP "
           "RULE only: every probe sees >= 2 faces, all kept.";
}

}   // namespace

Env photonArea() { return Env{ "photon-area", describeArea, buildArea, stopArea, expectArea, 500.0f, false }; }

}   // namespace envs
}   // namespace lab
