// photon-rough — THE ROUGH SPECULAR'S FIXTURE (PHOTON-7 stage 7, 7-3; the lead's ruling 2026-10-24: photon-ramp's strips
// run 0.00-0.15, every one glossy, and no environment held a rough surface - the walk rows' base reads no rough pixel).
//
// photon-room's sealed shell (envbuild.h roomShell: the 8 m cube, its opening, the lamp 6 m up) with photon-ramp's
// lighting - the shell's lamp and THE EDGE, the white emitter panel on the far wall from y 3.3 m up to 7.9 m - and FOUR
// ROUGH METAL FLOOR STRIPS side by side across the room's middle, 0.6 m wide, perceptual roughness 0.45, 0.55, 0.65 and
// 0.75 (the walk's ROUGH class, 0.4 to 0.8): the band where a view with a ray reflection fades from the ray to the rough
// specular (0.4-0.5 at the High and Epic cutoff) and where the rough specular alone answers. Each strip's reflection of
// the room and the panel is a wide lobe - the surface whose WALKING FLICKER (M1, gi.walk_rough) measures the rough
// specular's estimator: the voxel cone with no history before 7-3, the gather's probes after it.
// THE EXPECTATION is the shell's: THE KEEP RULE (kProbeFacesOfGeometry = 2 of six faces) - every probe inside the sealed
// room photographs geometry in six directions, all kept. The strips change no probe's faces.
#include "envbuild.h"

#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include <cmath>

namespace lab {
namespace envs {

namespace {

constexpr float kRho = 0.5f;

Built buildRough(unsigned seed, bool bake)
{
    Builder b("photon-rough", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    roomShell(b, unit, b.material(kRho, kRho, kRho, 1.0f));
    using V = iris::Vec3;
    for (int i = 0; i < 4; ++i) {
        const float x0 = -1.2f + 0.6f * float(i);
        fixedBox(b, unit, QStringLiteral("rough-%1").arg(45 + 10 * i), V(x0, 0, -3.6f), V(x0 + 0.59f, 0.02f, 1.6f),
                 b.material(0.9f, 0.9f, 0.9f, 0.45f + 0.1f * float(i), 1.0f));
    }
    fixedBox(b, unit, "edge", V(-3.9f, 3.3f, -3.99f), V(3.9f, 7.9f, -3.95f), b.emitter(1.0f));
    return b.finish();
}

void stopRough(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 1.6f, 2.4f);
    target = iris::Vec3(0, 0, -3.0f);
}

std::vector<Prediction> expectRough(const Built &, const Optics &)
{
    return { { "probesKeptShare", 1.0, "every probe of the sealed shell sees >= 2 faces of geometry: all kept" } };
}

std::string describeRough()
{
    return "photon-rough: photon-room's sealed shell with photon-ramp's lighting (the lamp, the far wall's emitter panel "
           "3.3 - 7.9 m up) and four rough metal floor strips, 0.6 m wide, at perceptual roughness 0.45 / 0.55 / 0.65 / "
           "0.75 - the walk's ROUGH class (PHOTON-7 stage 7, 7-3: the rough specular's walking flicker, gi.walk_rough). "
           "Predicts THE KEEP RULE only: every probe sees >= 2 faces, all kept.";
}

}   // namespace

Env photonRough() { return Env{ "photon-rough", describeRough, buildRough, stopRough, expectRough, 500.0f, false }; }

// photon-firefly - THE REFLECTION's FIREFLY FIXTURE (PHOTON-7 stage 11, 11-1b: next-event estimation toward emitters).
// photon-room's sealed shell and lamp, a ROUGH METAL FLOOR PLATE (perceptual roughness 0.30: under the ray tiers' cutoff,
// so the ray reflection IS its reflection) and ONE SMALL BRIGHT EMITTER above it - a 0.3 m cube of radiance 30, 1.2 m up:
// a rough lobe finds it by chance (the BRDF-only estimator's firefly, photon-rough's tail in its purest form), where the
// emitter table's cone finds it every sample. THE CAMERA sees the emitter's reflection in the plate (its mirror point
// about z 0.2). No expectation of its own: the reflection's rest and its 4,096-sample reference are measured on it.
namespace {
Built buildFirefly(unsigned seed, bool bake)
{
    Builder b("photon-firefly", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    roomShell(b, unit, b.material(kRho, kRho, kRho, 1.0f));
    using V = iris::Vec3;
    fixedBox(b, unit, "plate", V(-1.5f, 0, -1.5f), V(1.5f, 0.02f, 1.5f), b.material(0.9f, 0.9f, 0.9f, 0.30f, 1.0f));
    fixedBox(b, unit, "lamp-cube", V(-0.15f, 1.05f, -1.65f), V(0.15f, 1.35f, -1.35f), b.emitter(30.0f));
    return b.finish();
}
void stopFirefly(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 1.6f, 2.4f);
    target = iris::Vec3(0, 0, -0.5f);
}
std::vector<Prediction> expectFirefly(const Built &, const Optics &) { return {}; }
std::string describeFirefly()
{
    return "photon-firefly: photon-room's sealed shell and lamp, a rough metal floor plate (perceptual 0.30) and one "
           "small bright emitter above it (a 0.3 m cube, radiance 30, 1.2 m up) - the reflection's firefly (PHOTON-7 "
           "stage 11, 11-1b: the BRDF-only estimator finds the emitter by chance, next-event estimation every sample). "
           "No prediction: the rest and its 4,096-sample reference are measured on it.";
}
}   // namespace

// photon-glint - THE HIT'S GLINT (PHOTON-7 stage 11, the sphere-light glint): photon-room's shell WITHOUT its lamp, a
// rough metal floor plate (perceptual 0.35), a POLISHED METAL PANEL standing on the far side (perceptual 0.05, facing the
// camera) and ONE point lamp (source radius 0.1 m) 1.67 m up between them: rough floor rays that reach the panel meet
// the lamp's mirror glint there - a near-delta under a point light's lobe (D ~ 3e5 at alpha 0.0025), a disc of the lamp's
// size under a sphere light's. Measured on it: the reflection's sigma a sample (the rest's 4,096-sample references).
namespace {
Built buildGlint(unsigned seed, bool bake)
{
    Builder b("photon-glint", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    roomShell(b, unit, b.material(kRho, kRho, kRho, 1.0f), false);
    using V = iris::Vec3;
    fixedBox(b, unit, "plate", V(-1.5f, 0, -1.5f), V(1.5f, 0.02f, 1.5f), b.material(0.9f, 0.9f, 0.9f, 0.35f, 1.0f));
    fixedBox(b, unit, "panel", V(-1.0f, 0.0f, -2.05f), V(1.0f, 2.0f, -2.0f), b.material(0.95f, 0.95f, 0.95f, 0.05f, 1.0f));
    auto lamp = iris::LightNode::create();
    lamp->setLightType(iris::LightType::Point);
    lamp->setName(QStringLiteral("glint-lamp"));
    b.built().doc->getRootNode()->addChild(lamp);
    lamp->setLocalPos(V(0.0f, 1.67f, 0.0f));
    lamp->setPropertyValue(QStringLiteral("intensity"), 3.0f);
    lamp->setPropertyValue(QStringLiteral("distance"), 10.0f);
    lamp->setPropertyValue(QStringLiteral("sourceRadius"), 0.1f);
    return b.finish();
}
void stopGlint(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 1.4f, 2.6f);
    target = iris::Vec3(0, 0, -0.5f);
}
std::vector<Prediction> expectGlint(const Built &, const Optics &) { return {}; }
std::string describeGlint()
{
    return "photon-glint: photon-room's shell without its lamp, a rough metal floor plate (perceptual 0.35), a polished "
           "metal panel on the far side (0.05) and one point lamp (source radius 0.1 m) between them - the hit's glint "
           "(PHOTON-7 stage 11: a point light's lobe is a near-delta at a polished hit; a sphere light's a disc of its "
           "size). No prediction: the reflection's sigma is measured on it.";
}
}   // namespace

Env photonGlint() { return Env{ "photon-glint", describeGlint, buildGlint, stopGlint, expectGlint, 500.0f, false }; }

Env photonFirefly() { return Env{ "photon-firefly", describeFirefly, buildFirefly, stopFirefly, expectFirefly, 500.0f, false }; }

}   // namespace envs
}   // namespace lab
