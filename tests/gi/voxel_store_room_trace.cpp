// VOXEL-STORE-ROOM-TRACE (PHOTON-VOXEL-5 item (ii)) — a TOOL, not a suite: scripting.e2e.gi_voxel_store's
// room (a closed 5 m cube of 0.2 m slabs, one point lamp at (0, 4, 0)) at High, the chain on, one bounce -
// every cascade-0 voxel holding a face that looks INTO the room (its surface position inside the interior)
// whose direct light is ZERO: where the injection shadows a face the lamp sees (the lab predicts every one
// lit: gi.voxel_lab storeroom). Printed by the face (half, axis) and by position class.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace jahshaka::engine;

int main(int argc, char **argv)
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "voxel-store-room-trace-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("storeroom", 128, 128, Colour(0, 0, 0));
    Scene *s = e->createScene("storeroom");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.5f, 1.8f), Vec3(0.0f, 2.5f, -3.0f));
    const float S = 5.0f, T = 0.2f;
    const auto box = [&](float x, float y, float z, float sx, float sy, float sz) {
        const NodeId n = enginetest::addTestCube(s, Colour(0.791f, 0.791f, 0.791f), 0.0f, 0.0f);
        enginetest::setNodePosition(s, n, Vec3(x, y, z));
        enginetest::setNodeScale(s, n, Vec3(sx, sy, sz));
    };
    box(0, 0, 0, S, T, S); box(0, S, 0, S, T, S);
    box(-S / 2, S / 2, 0, T, S, S); box(S / 2, S / 2, 0, T, S, S);
    box(0, S / 2, -S / 2, S, S, T); box(0, S / 2, S / 2, S, S, T);
    const NodeId lamp = s->createNode();
    s->setNodeTransform(lamp, Vec3(0.0f, S - 1.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    LightDesc ld;
    ld.type = LightType::Point;
    ld.colour = Colour(1, 1, 1);
    ld.intensity = 0.12f;
    ld.range = 30.0f;
    s->setLight(lamp, ld);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::Off;
    gi.numBounces = 1;
    gi.cascades = true;
    gi.epicTier = argc > 1 && std::string(argv[1]) == "epic";
    s->setGlobalIllumination(gi);
    for (int f = 0; f < 16; ++f) e->renderOneFrame();
    for (int f = 0; f < 4000 && !s->giStatus().giAtRest; ++f) e->renderOneFrame();
    GiVoxelVolume v;
    if (!s->giVoxelVolume(0, v) || !v.available) { std::printf("no store\n"); return 1; }
    long lit = 0, dark = 0;
    std::map<std::string, long> byClass;
    int shown = 0;
    const double lo = -S / 2 + T / 2, hi = S / 2 - T / 2, ylo = T / 2, yhi = S - T / 2;
    for (int z = 0; z < v.depth; ++z) for (int y = 0; y < v.height; ++y) for (int x = 0; x < v.width; ++x) {
        const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4;
        const double c = v.albedo[i + 3];
        if (!(c > 0)) continue;
        const bool isLit = v.light[i] + v.light[i + 1] + v.light[i + 2] > 1e-6;
        // the faces this voxel holds that look INTO the room
        std::string faces;
        for (int h = 0; h < 2; ++h) for (int a = 0; a < 3; ++a) {
            const std::vector<float> &cov = h ? v.coverageN : v.coverageP;
            const std::vector<float> &pos = h ? v.positionN : v.positionP;
            const double o = cov[i + size_t(a)];
            if (!(o > 0)) continue;
            const double p = v.origin[a] + pos[i + size_t(a)] / o * (v.cell[a] * (a == 0 ? v.width : a == 1 ? v.height : v.depth));
            const double l = a == 1 ? ylo : lo, u = a == 1 ? yhi : hi;
            const bool inward = (h == 0 && std::fabs(p - l) < 0.02) || (h == 1 && std::fabs(p - u) < 0.02);
            if (inward) { faces += (h ? '-' : '+'); faces += "xyz"[a]; }
        }
        if (faces.empty()) continue;
        if (isLit) { ++lit; continue; }
        ++dark;
        const double w[3] = { v.origin[0] + (x + 0.5) * v.cell[0], v.origin[1] + (y + 0.5) * v.cell[1],
                              v.origin[2] + (z + 0.5) * v.cell[2] };
        int edges = 0;
        for (int a = 0; a < 3; ++a) {
            const double l = a == 1 ? ylo : lo, u = a == 1 ? yhi : hi;
            if (w[a] < l + v.cell[a] || w[a] > u - v.cell[a]) ++edges;
        }
        byClass[faces + (edges >= 2 ? " edge/corner" : " flat")] += 1;
        if (shown++ < 20)
            std::printf("   DARK voxel (%.3f %.3f %.3f) faces %s normal %.2f %.2f %.2f two %d\n", w[0], w[1], w[2], faces.c_str(),
                        v.normal.empty() ? 0.0 : v.normal[i] * 2 - 1, v.normal.empty() ? 0.0 : v.normal[i + 1] * 2 - 1,
                        v.normal.empty() ? 0.0 : v.normal[i + 2] * 2 - 1, v.normal.empty() ? -1 : int(v.normal[i + 3] > 0.5f));
    }
    long allLit = 0;
    for (size_t i = 0; i < v.light.size(); i += 4)
        allLit += std::max(std::max(v.light[i], v.light[i + 1]), v.light[i + 2]) > 0.0f;
    std::printf("== %s: voxels holding an inward face: lit %ld, DARK (hidden faces) %ld; every lit voxel %ld\n",
                gi.epicTier ? "Epic" : "High", lit, dark, allLit);
    for (auto &kv : byClass) std::printf("   %-24s %ld\n", kv.first.c_str(), kv.second);
    view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    return 0;
}
