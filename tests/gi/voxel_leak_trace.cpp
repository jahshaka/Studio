// VOXEL-LEAK-TRACE (PHOTON-VOXEL-5 item (iii)) — a TOOL, not a suite: where the pixel's four
// diffuse cones pick up the RED of gi.gather's leak room (a sealed 10 m room, a green lamp inside,
// a red lamp a metre outside the -Z wall; the "cones" arm: High, the cascade chain, no field, no
// gather). The measured pixel is the inner face of the -Z wall (z = -5) around (3.6, 2.0).
//
// Per wall thickness (argv: 0.5 by default), with the outside lamp ON and OFF: the four cones
// marched by the engine's own reader (Engine::voxelReaderParity's compute arm, the pixel's march)
// from that face with its normal +z - each cone's colour (the red is the leak), its alpha, the
// cascade it stopped in; the same with the march cut to its first K planes (JAH_MARCH_ONE_STEP
// read at the march's successive plane centres is not available, so the cone's length is swept
// by moving its start along its own axis) - and the store dumped (JAH_VOXEL_DUMP) for the lab.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/voxeldump.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace jahshaka::engine;

int main(int argc, char **argv)
{
    const float T = argc > 1 ? float(std::atof(argv[1])) : 0.5f;
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "voxel-leak-trace-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("leaktrace", 128, 128, Colour(0, 0, 0));
    Scene *s = e->createScene("leaktrace");
    view->setScene(s);
    view->setShadows(true);
    enginetest::leakroom::Room room = enginetest::leakroom::build(s, view, T);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::Off;
    gi.updateBudget = 1;
    gi.numBounces = 1;
    gi.cascades = true;
    s->setGlobalIllumination(gi);
    const double ax[4][3] = { { 0.707107, 0, 0.707107 }, { 0, 0.707107, 0.707107 }, { -0.707107, 0, 0.707107 },
                              { 0, -0.707107, 0.707107 } };
    for (float lamp : { 25.0f, 0.0f }) {
        enginetest::leakroom::setOutsideIntensity(s, room, lamp);
        s->refreshGlobalIllumination();
        for (int f = 0; f < 16; ++f) e->renderOneFrame();
        for (int f = 0; f < 4000 && !s->giStatus().giAtRest; ++f) e->renderOneFrame();
        Image img;
        view->readPixels(img);
        float r = 0, g = 0;
        enginetest::leakroom::meanRG(img, r, g);
        std::printf("== wall %.2f m, outside lamp %.0f: the pixel block r %.4f g %.4f\n", T, lamp, r, g);
        if (lamp > 0) enginetest::dumpVoxelStore(s, "leak");
        GiVoxelVolume v;
        if (!s->giVoxelVolume(0, v) || !v.available) { std::printf("no cascade 0\n"); continue; }
        const double size[3] = { double(v.cell[0]) * v.width, double(v.cell[1]) * v.height, double(v.cell[2]) * v.depth };
        std::printf("   cascade 0: origin (%.2f %.2f %.2f) cell %.4f\n", v.origin[0], v.origin[1], v.origin[2], v.cell[0]);
        for (double px : { 3.6, 2.0, 0.0 })
            for (double py : { 2.0, 0.5, 3.5 }) {
                const double pw[3] = { px, py, -5.0 };
                std::vector<VoxelReaderCone> cones;
                double nls[3] = { 0, 0, 1.0 / size[2] }, l = 1.0 / size[2];
                for (double &q : nls) q /= l;
                for (int k = 0; k < 4; ++k) {
                    double d[3], dl = 0;
                    for (int a = 0; a < 3; ++a) { d[a] = ax[k][a] / size[a]; dl += d[a] * d[a]; }
                    dl = std::sqrt(dl);
                    VoxelReaderCone c;
                    c.posLS = Vec3(float((pw[0] - v.origin[0]) / size[0]), float((pw[1] - v.origin[1]) / size[1]),
                                   float((pw[2] - v.origin[2]) / size[2] + 1.0 / v.depth));
                    c.dirLS = Vec3(float(d[0] / dl), float(d[1] / dl), float(d[2] / dl));
                    c.biasDirLS = Vec3(0, 0, 1);
                    c.tanHalfAngle = 0.98269f;
                    c.flags = 0u;
                    cones.push_back(c);
                }
                std::vector<VoxelReaderAnswer> fr, co;
                if (!e->voxelReaderParity(s, cones, fr, co) || co.size() != 4) { std::printf("   parity failed\n"); continue; }
                std::printf("   face point (%.1f, %.1f, -5):", px, py);
                for (int k = 0; k < 4; ++k)
                    std::printf("  [%d] rgb %.4f %.4f %.4f a %.3f c%.0f", k, co[size_t(k)].march[0], co[size_t(k)].march[1],
                                co[size_t(k)].march[2], co[size_t(k)].march[3], co[size_t(k)].escape[2]);
                std::printf("\n");
            }
    }
    view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    return 0;
}
