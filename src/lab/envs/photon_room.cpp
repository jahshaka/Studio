// photon-room and photon-court — Photon's two closed-form fixtures (LAB-1 stage 2). Not dollied (their laws do not
// move with the distance); `--env-list` prints the law at every stop, and the Photon spec measures them.
//
// photon-room — AN ENCLOSED ROOM. An 8 m cube room (walls 0.3 m thick, inside x, z in [-4, 4], y in [0, 8]) with ONE
// 1 x 1 m opening in its +Z wall, a lamp (kDefaultLampIntensity) 6 m up, every surface matte at albedo rho = 0.5.
// THE EXPECTATION. (1) THE BOUNCE RESIDUAL (ENGINE.md PHOTON-5: "bounces run to a residual"): the light volume's energy
// ratio between two passes r = E2 / E1 is, for an enclosed diffuse room, the fraction of each bounce's light that
// lands on a wall again and is re-emitted: r = rho (1 - A_open / A_inside) = 0.5 x (1 - 1 / 384) = 0.4987, and the
// injection runs n = ceil(log(0.02 (1 - r)) / log r) = 7 passes (at most 8) — the residual of the geometric series
// then under 2 %; the multi-bounce gain 1 / (1 - r) = 1.995. (2) THE PROBE KEEP RULE (kProbeFacesOfGeometry = 2 of six
// faces): every probe inside a sealed room photographs geometry in six directions — ALL of them are kept.
//
#include "envbuild.h"

#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include <cmath>

namespace lab {
namespace envs {

namespace {

constexpr float kRho = 0.5f;

// ---- photon-room ---------------------------------------------------------------------------------------------------

Built buildRoom(unsigned seed, bool bake)
{
    Builder b("photon-room", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    const auto matte = b.material(kRho, kRho, kRho, 1.0f);
    using V = iris::Vec3;
    const float t = 0.3f, h = 4.0f, top = 8.0f;
    fixedBox(b, unit, "floor", V(-h - t, -t, -h - t), V(h + t, 0, h + t), matte);
    fixedBox(b, unit, "ceiling", V(-h - t, top, -h - t), V(h + t, top + t, h + t), matte);
    fixedBox(b, unit, "wall-x0", V(-h - t, 0, -h), V(-h, top, h), matte);
    fixedBox(b, unit, "wall-x1", V(h, 0, -h), V(h + t, top, h), matte);
    fixedBox(b, unit, "wall-z0", V(-h, 0, -h - t), V(h, top, -h), matte);
    // the +Z wall around its 1 x 1 m opening (x in [-0.5, 0.5], y in [3.5, 4.5])
    fixedBox(b, unit, "wall-z1-left", V(-h, 0, h), V(-0.5f, top, h + t), matte);
    fixedBox(b, unit, "wall-z1-right", V(0.5f, 0, h), V(h, top, h + t), matte);
    fixedBox(b, unit, "wall-z1-below", V(-0.5f, 0, h), V(0.5f, 3.5f, h + t), matte);
    fixedBox(b, unit, "wall-z1-above", V(-0.5f, 4.5f, h), V(0.5f, top, h + t), matte);
    auto lamp = iris::LightNode::create();
    lamp->setLightType(iris::LightType::Point);
    lamp->setName(QStringLiteral("lamp"));
    b.built().doc->getRootNode()->addChild(lamp);
    lamp->setLocalPos(V(0, 6, 0));
    lamp->setPropertyValue(QStringLiteral("intensity"), iris::lens::kDefaultLampIntensity);
    lamp->setPropertyValue(QStringLiteral("distance"), 20.0f);
    return b.finish();
}

void stopRoom(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 2, 3);
    target = iris::Vec3(0, 3, -4);
}

std::vector<Prediction> expectRoom(const Built &, const Optics &)
{
    const double inside = 6.0 * 64.0, open = 1.0;
    const double r = kRho * (1.0 - open / inside);
    const double n = std::ceil(std::log(0.02 * (1.0 - r)) / std::log(r));
    return { { "bounceRatio", r, "r = rho (1 - A_open / A_inside): the energy ratio of two passes" },
             { "passes", std::min(8.0, n), "n = ceil(log(0.02 (1 - r)) / log r), at most 8: the residual under 2 %" },
             { "multiBounceGain", 1.0 / (1.0 - r), "1 / (1 - r): the enclosed room's light over its first bounce" },
             { "probesKeptShare", 1.0, "every probe of a sealed room sees geometry in >= 2 faces: all kept" } };
}

std::string describeRoom()
{
    return "photon-room: an enclosed 8 m room (one 1 x 1 m opening, a lamp, matte walls of albedo 0.5). Predicts the "
           "BOUNCE RESIDUAL — r = rho (1 - A_open/A_inside) = 0.4987, n = ceil(log(0.02 (1-r)) / log r) = 7 passes, "
           "the gain 1/(1-r) = 1.995 — and THE KEEP RULE: every probe inside sees >= 2 faces of geometry, all kept.";
}

}   // namespace

Env photonRoom() { return Env{ "photon-room", describeRoom, buildRoom, stopRoom, expectRoom, 500.0f, false }; }

}   // namespace envs
}   // namespace lab
