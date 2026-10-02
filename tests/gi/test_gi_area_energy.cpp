// gi.area_energy — ONE AREA-LIGHT ENERGY, HELD TO A CLOSED FORM (AREA-SCALE-1, plan 9cg).
//
// THE DEFINITION. An area light of intensity I is a Lambertian emitter of RADIANCE I in the
// engine's light units (a sun of intensity I lights a facing surface of albedo a to radiance
// a I; a point lamp of intensity I does so at 1 m). So at a point P on the rectangle's axis,
// a height h below it, the irradiance is E = pi I F(h) with F the point-to-rectangle form
// factor, and a Lambertian floor of albedo a reflects radiance a I F(h). For a w x l
// rectangle (half sizes A, B) on its axis:
//     F(h) = (2/pi) [ A/sqrt(A^2+h^2) atan(B/sqrt(A^2+h^2)) + B/sqrt(B^2+h^2) atan(A/sqrt(B^2+h^2)) ]
// (the parallel-rectangle form factor; it tends to A B 4 / (pi h^2) far away and to 1 at
// contact, where an emitting plane lights a surface it touches with its own radiance).
//
// THE MEASUREMENT. The SAME point lamp at the same place is the normaliser: the floor's
// BRDF, its albedo, the store's decode and the readback's scale all cancel in
// (area / point), and the point lamp's own law is the one falloff (lightFalloff), so
//     ratio(h) = (area / point) * lightFalloff(h) / F(h)
// is 1 for an area light that carries the defined energy. Measured on both paths: the
// PIXEL (an HDR readback straight down at the floor, the approximate area light and the
// accurate LTC one) and the VOXEL store (Photon's light injection, the direct store alone,
// at the floor voxel the readback looks at — h from the voxel's centre, where the
// injection evaluates the light). Three heights; the range is far (40 m) so the window
// is 1 to five decimals.
//
// THE BARS. gi.area_energy: the VOXEL store and the accurate (LTC) pixel within 5 % of the
// closed form at every height. gi.area_energy_target (--pixel): the pixel's approximate area
// light within 5 % — a photon-target row: the approximation is the fork's media.
//
// WHAT IT FOUND (AREA-SCALE-1, measured on d-build c5fbe870c, a matte floor at roughness 1):
// the voxel store is the closed form (0.9996 / 0.9999 / 1.0004 at 1 / 1.5 / 2.5 m: the LTC
// form factor the injection evaluates is exact) and so is the LTC pixel (0.9998 / 1.0003 /
// 1.0001). The APPROXIMATE area light's pixel draws 5.32 / 4.59 / 4.21 x it: its roughness
// booster lerp(1, 4, roughness) (x4 at roughness 1) times its far-field law
// r^2 / max(d^2, r^2) (r = sqrt(w l / pi)) against the form factor (1.33 / 1.15 / 1.05 x; the
// disc's own on-axis law r^2 / (r^2 + d^2) is within 1 % of the rectangle's). IMAGE-1's
// "voxel at 1/4 of the pixel" was the pixel over-drawing, not the voxel under-holding. Both
// terms are AreaLights_piece_ps.any in the fork: the fix is a fork commit.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/lightfalloff.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

namespace {

constexpr double kPi = 3.14159265358979323846;
/// The floor's roughness. HlmsPbs's diffuse for a point lamp (and the area approximation's,
/// BRDF_AreaLightApprox — the same normalised Disney term) at N.L = N.V = 1 is the Lambert
/// term times energyFactor = lerp(1, 1/1.51, roughness) (Frostbite's renormalisation); the
/// LTC area light's diffuse is the plain Lambert term (AreaLights_LTC_piece_ps.any). So the
/// LTC pixel's ratio to the point lamp carries 1 / energyFactor, which the arm takes out.
constexpr double kRough = 1.0;
double disneyEnergyFactor(double r) { return 1.0 + (1.0 / 1.51 - 1.0) * r; }

void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

/// The point-to-rectangle form factor on the rectangle's axis (half sizes a, b; height h).
double rectFormFactor(double a, double b, double h)
{
    const double A = std::sqrt(a * a + h * h), B = std::sqrt(b * b + h * h);
    return (2.0 / kPi) * (a / A * std::atan(b / A) + b / B * std::atan(a / B));
}

/// Re-injects the lights at rest and reads the direct store at (x, y, z) until it stops
/// moving (frames, never a clock).
double settledVoxel(Engine *e, Scene *s, int x, int y, int z, GiVoxelVolume &v)
{
    render(e, 2);
    s->refreshGiLighting(false);
    double last = -1.0;
    for (int round = 0; round < 20; ++round) {
        render(e, 6);
        if (!s->giVoxelVolume(0, v) || !v.available) continue;
        const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4;
        const double now = v.light[i] + v.light[i + 1] + v.light[i + 2];
        if (last >= 0.0 && std::fabs(now - last) <= 1e-6 * std::max(1.0, std::fabs(now))) return now;
        last = now;
    }
    return last;
}

}   // namespace

int main(int argc, char **argv)
{
    const bool pixelTarget = argc > 1 && std::strcmp(argv[1], "--pixel") == 0;
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-area-energy-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setFixedFrameDelta(1.0f / 60.0f);
    View *view = e->createOffscreenView("area-energy", 128, 128, Colour(0, 0, 0));
    Scene *s = e->createScene("area-energy");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    // MATTE (the default ground's recipe): Specular workflow, F0 0 — the pixel carries the
    // diffuse alone, so no specular lobe rides on either light's answer.
    PbrParams mp;
    mp.albedo = Colour(0.8f, 0.8f, 0.8f);
    mp.roughness = kRough;
    mp.workflow = PbrParams::Workflow::Specular;
    mp.ior = 1.0f;
    mp.specularColour = Colour(0.0f, 0.0f, 0.0f);
    const MaterialId mat = s->createPbrMaterial(mp);
    const NodeId floorN = s->createNode();
    s->attachMesh(floorN, cube, mat);
    s->setNodeTransform(floorN, Vec3(0, -0.05f, 0), Quat(), Vec3(7.5f, 0.1f, 7.5f));

    const NodeId lamp = s->createNode();
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 0;            // the DIRECT store alone
    gi.ddgi = GiToggle::Off;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 4.0f, 64, 0.0f };
    {
        LightDesc ld;
        ld.type = LightType::Point;
        ld.intensity = 4.0f;
        ld.range = 40.0f;
        ld.castShadows = false;
        s->setNodeTransform(lamp, Vec3(0.0f, 1.0f, 0.0f), Quat(), Vec3(1, 1, 1));
        s->setLight(lamp, ld);
    }
    const CameraDesc wide = enginetest::testCameraDescLookAt(Vec3(0.0f, 1.5f, 0.0f), Vec3(0.0f, 0.0f, -4.0f));
    view->setCamera(wide);
    CHECK_MSG(s->setGlobalIllumination(gi), "%s", "the voxel arm builds");
    render(e, 8);
    GiVoxelVolume v;
    for (int f = 0; f < 60 && !(s->giVoxelVolume(0, v) && v.available); ++f) render(e, 1);
    if (!s->giVoxelVolume(0, v) || !v.available) {
        CHECK_MSG(false, "%s", "the store reads back");
        return 1;
    }
    // THE FLOOR VOXEL nearest the origin, in the layer just above y = 0.
    const auto index = [&](double w, int axis) {
        return int(std::floor((w - v.origin[axis]) / v.cell[axis]));
    };
    const int vx = index(0.01, 0), vy = index(0.01, 1), vz = index(0.01, 2);
    const double cx = v.origin[0] + (vx + 0.5) * v.cell[0];
    const double cy = v.origin[1] + (vy + 0.5) * v.cell[1];
    const double cz = v.origin[2] + (vz + 0.5) * v.cell[2];
    std::printf("   the floor voxel (%d %d %d), centre (%.4f %.4f %.4f), cell %.4f m\n", vx, vy, vz, cx,
                cy, cz, double(v.cell[0]));

    PostFxDesc fx;
    fx.hdrReadback = true;
    view->setPostFx(fx);
    const auto pixelAt = [&]() {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(float(cx), 0.6f, float(cz) + 0.001f),
                                                         Vec3(float(cx), 0.0f, float(cz))));
        render(e, 4);
        ImageF img;
        if (!view->readPixelsHdr(img)) return -1.0;
        const Colour c = img.at(64, 64);
        return double(c.r + c.g + c.b) / 3.0;
    };

    const double kW = 1.0, kL = 1.0, kRange = 40.0;
    const double heights[3] = { 1.0, 1.5, 2.5 };
    double worstVoxel = 0.0, worstPixel = 0.0, worstLtc = 0.0;
    for (double h : heights) {
        // 0 point, 1 area (approximate), 2 area (accurate, LTC)
        double px[3] = { 0, 0, 0 }, vox[3] = { 0, 0, 0 };
        for (int k = 0; k < 3; ++k) {
            LightDesc l;
            l.castShadows = false;
            l.range = float(kRange);
            l.intensity = 4.0f;
            if (k == 0) l.type = LightType::Point;
            else { l.type = LightType::Area; l.accurate = k == 2; l.rectWidth = float(kW); l.rectHeight = float(kL); }
            s->setLight(lamp, l);
            // the pixel sees the floor's top face (y = 0); the store's light is evaluated at
            // the voxel's centre — each path's own h
            s->setNodeTransform(lamp, Vec3(float(cx), float(h), float(cz)), Quat(), Vec3(1, 1, 1));
            px[k] = pixelAt();
            view->setCamera(wide);
            s->setNodeTransform(lamp, Vec3(float(cx), float(cy + h), float(cz)), Quat(), Vec3(1, 1, 1));
            vox[k] = settledVoxel(e, s, vx, vy, vz, v);
        }
        const double F = rectFormFactor(0.5 * kW, 0.5 * kL, h);
        const double law = lightFalloff(h, kRange, double(LightDesc().sourceRadius));
        const double pixRatio = px[0] > 0.0 ? (px[1] / px[0]) * law / F : -1.0;
        const double ltcRatio = px[0] > 0.0 ? (px[2] / px[0]) * law / F * disneyEnergyFactor(kRough) : -1.0;
        const double voxRatio = vox[0] > 0.0 ? 0.5 * (vox[1] + vox[2]) / vox[0] * law / F : -1.0;
        std::printf("   h %.1f m: F %.5f (x h^2 %.4f) | pixel point %.6f approx %.6f ltc %.6f | voxel point %.6f "
                    "approx %.6f ltc %.6f\n",
                    h, F, F * h * h, px[0], px[1], px[2], vox[0], vox[1], vox[2]);
        std::printf("   h %.1f m: against the closed form — voxel %.4f, pixel approx %.4f, pixel LTC %.4f\n", h,
                    voxRatio, pixRatio, ltcRatio);
        worstVoxel = std::max(worstVoxel, std::fabs(voxRatio - 1.0));
        worstPixel = std::max(worstPixel, std::fabs(pixRatio - 1.0));
        worstLtc = std::max(worstLtc, std::fabs(ltcRatio - 1.0));
    }
    view->setPostFx(PostFxDesc());
    if (!pixelTarget) {
        CHECK_MSG(worstVoxel <= 0.05,
                  "THE VOXEL STORE HOLDS THE DEFINED AREA-LIGHT ENERGY: a I F(h) within 5 %% at 1, 1.5 and "
                  "2.5 m (worst %.2f %%)", 100.0 * worstVoxel);
        CHECK_MSG(worstLtc <= 0.05,
                  "...and so does the accurate (LTC) pixel (worst %.2f %%)", 100.0 * worstLtc);
    } else {
        std::printf("target: the approximate area light's pixel against the closed form, worst %.2f %% (bar 5 %%)\n",
                    100.0 * worstPixel);
        CHECK_MSG(worstPixel <= 0.05,
                  "THE APPROXIMATE AREA LIGHT'S PIXEL DRAWS THE DEFINED ENERGY within 5 %% (worst %.2f %%)",
                  100.0 * worstPixel);
    }

    GiParams off; off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    e->destroyScene(s);
    engine.reset();
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
