// photon-rough — THE ROUGH SPECULAR'S FIXTURE (PHOTON-7 stage 7, 7-3; the lead's ruling 2026-10-24: photon-ramp's strips
// run 0.00-0.15, every one glossy, and no environment held a rough surface - gi.walk's base reads no rough pixel).
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

}   // namespace envs
}   // namespace lab
