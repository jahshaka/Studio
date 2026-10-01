// gi.lamp_falloff — A LAMP'S BOUNCE FALLS OFF EXACTLY AS ITS DIRECT LIGHT (IMAGE-1).
//
// THE DEFECT. Photon's voxel light injection applied a point or spot light's
// cone and nothing else: no distance term, no range. Every voxel of every
// cascade received the lamp's full intensity at any distance, past its range
// too, so a lamp's bounce flooded rooms its direct light never reached (and
// against the inverse-square direct light the lamp's GI would have been about
// nine times too bright at 3 m).
//
// THE LAW. The injection now calls the pixel's own falloff — the fork's JahBrdf
// `jahLightAttenuation`, in C++ `lightFalloff` (Types.h):
//     E = I / max(d^2, rSrc^2) * saturate(1 - (d/R)^4)^2
// and the engine culls a lamp whose range misses a cascade.
//
// THE FIXTURE. A rough floor, one camera-centred cascade (+-4 m at 64 cells),
// no sky, no ambient, no bounce (the DIRECT store alone: numBounces 0). A point
// lamp straight above one floor voxel's centre, at heights from 1 m to R/2: the
// voxel's stored radiance divided by lightFalloff(d) — d from the VOXEL'S
// CENTRE, which is where the injection evaluates the law — is one constant
// within 5 %. Then a lamp of small range: every floor voxel farther than R from
// it holds exactly nothing, and the nearer ones hold light.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

/// Re-injects the lights at rest (what the host's at-rest tick does after a lamp
/// moves) and reads the store until the value at (x, y, z) stops moving
/// (frames, never a clock); returns it with the volume it came from.
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

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-lamp-falloff-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setFixedFrameDelta(1.0f / 60.0f);
    View *view = e->createOffscreenView("lamp-falloff", 128, 128, Colour(0, 0, 0));
    Scene *s = e->createScene("lamp-falloff");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams mp; mp.albedo = Colour(0.8f, 0.8f, 0.8f); mp.roughness = 1.0f;
    const MaterialId mat = s->createPbrMaterial(mp);
    const NodeId floorN = s->createNode();
    s->attachMesh(floorN, cube, mat);
    s->setNodeTransform(floorN, Vec3(0, -0.05f, 0), Quat(), Vec3(7.5f, 0.1f, 7.5f));

    const NodeId lamp = s->createNode();
    LightDesc ld;
    ld.type = LightType::Point;
    ld.intensity = 4.0f;
    ld.range = 6.0f;
    ld.castShadows = false;
    s->setNodeTransform(lamp, Vec3(0.0f, 1.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    s->setLight(lamp, ld);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 0;            // the DIRECT store alone
    gi.ddgi = GiToggle::Off;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 4.0f, 64, 0.0f };
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 1.5f, 0.0f), Vec3(0.0f, 0.0f, -4.0f)));
    CHECK_MSG(s->setGlobalIllumination(gi), "%s", "the voxel arm builds");
    render(e, 8);
    GiVoxelVolume v;
    for (int f = 0; f < 60 && !(s->giVoxelVolume(0, v) && v.available); ++f) render(e, 1);
    if (!s->giVoxelVolume(0, v) || !v.available) {
        CHECK_MSG(false, "%s", "the store reads back");
        return 1;
    }

    // THE FLOOR VOXEL nearest the origin, in the layer just above y = 0 (the
    // floor's top face sits on a cell boundary and lands in the cell above it).
    const auto index = [&](double w, int axis) {
        return int(std::floor((w - v.origin[axis]) / v.cell[axis]));
    };
    const int vx = index(0.01, 0), vy = index(0.01, 1), vz = index(0.01, 2);
    const double cx = v.origin[0] + (vx + 0.5) * v.cell[0];
    const double cy = v.origin[1] + (vy + 0.5) * v.cell[1];
    const double cz = v.origin[2] + (vz + 0.5) * v.cell[2];
    std::printf("   the floor voxel (%d %d %d), centre (%.4f %.4f %.4f), cell %.4f m\n", vx, vy, vz, cx,
                cy, cz, double(v.cell[0]));

    // ---- 1. THE LAW between 1 m and R / 2 ----------------------------------
    double ref = -1.0, worst = 0.0;
    for (double d : { 1.0, 1.5, 2.0, 2.5, 3.0 }) {
        s->setNodeTransform(lamp, Vec3(float(cx), float(cy + d), float(cz)), Quat(), Vec3(1, 1, 1));
        const double stored = settledVoxel(e, s, vx, vy, vz, v);
        const double law = lightFalloff(d, ld.range, ld.sourceRadius);
        if (ref < 0.0) ref = stored / law;
        const double ratio = stored / (ref * law);
        std::printf("   lamp %.1f m above the voxel: stored %.6f, law %.6f -> ratio %.4f\n", d, stored,
                    ref * law, ratio);
        worst = std::max(worst, std::fabs(ratio - 1.0));
    }
    CHECK_MSG(ref > 0.0, "the lamp lights its voxel at all (%.6f per unit of the law)", ref);
    CHECK_MSG(worst <= 0.05,
              "THE VOXEL'S LIGHT FOLLOWS THE PIXEL'S FALLOFF from 1 m to R/2 (worst %.2f %%; <= 5 %%)",
              100.0 * worst);

    // ---- 2. NOTHING PAST THE RANGE ------------------------------------------
    ld.range = 2.5f;
    s->setLight(lamp, ld);
    s->setNodeTransform(lamp, Vec3(float(cx), float(cy + 1.0), float(cz)), Quat(), Vec3(1, 1, 1));
    settledVoxel(e, s, vx, vy, vz, v);
    long beyondLit = 0, beyondN = 0, insideLit = 0, insideN = 0;
    double beyondMax = 0.0;
    for (int z = 0; z < v.depth; ++z)
        for (int x = 0; x < v.width; ++x) {
            const double wx = v.origin[0] + (x + 0.5) * v.cell[0];
            const double wz = v.origin[2] + (z + 0.5) * v.cell[2];
            if (std::fabs(wx) > 3.5 || std::fabs(wz) > 3.5) continue;   // on the floor
            const size_t i = ((size_t(z) * v.height + vy) * v.width + x) * 4;
            const double val = v.light[i] + v.light[i + 1] + v.light[i + 2];
            const double dx = wx - cx, dy = 1.0, dz = wz - cz;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > double(ld.range) + 0.01) {
                ++beyondN;
                if (val > 0.0) { ++beyondLit; beyondMax = std::max(beyondMax, val); }
            } else if (d < 0.8 * double(ld.range)) {
                ++insideN;
                if (val > 0.0) ++insideLit;
            }
        }
    std::printf("   range %.1f m, lamp 1 m up: %ld of %ld floor voxels beyond the range hold light "
                "(max %.3g); %ld of %ld well inside it do\n", double(ld.range), beyondLit, beyondN,
                beyondMax, insideLit, insideN);
    CHECK_MSG(beyondN > 100 && beyondLit == 0,
              "NO VOXEL PAST THE LAMP'S RANGE HOLDS ITS LIGHT (%ld of %ld)", beyondLit, beyondN);
    CHECK_MSG(insideN > 100 && insideLit == insideN,
              "...and every voxel well inside it does (%ld of %ld)", insideLit, insideN);

    GiParams off; off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    e->destroyScene(s);
    engine.reset();
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
