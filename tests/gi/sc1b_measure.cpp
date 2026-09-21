// SURFACE-CACHE-1b's MEASUREMENT ARM — not a suite, a tool. Built beside the
// gates so the numbers in the lane's report are taken by code that is read.
//
// Three numbers the brief asks for:
//   1. CPU milliseconds per CARD CAPTURE in the REAL scene manager (against
//      SURFACE-CACHE-0's 0.042-0.057 ms in a one-item scratch scene).
//   2. ...with and without the scene's shadow node, so the shadow term's price
//      is stated rather than folded in.
//   3. VRAM and page occupancy at the tier's residency radius on a scene of
//      Showroom-2 density.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

int main(int argc, char **argv)
{
    const int items = argc > 1 ? std::atoi(argv[1]) : 16;
    const bool shadows = argc > 2 ? (std::atoi(argv[2]) != 0) : true;

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "sc1b-measure-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("measure", 1920u, 1080u, Colour(0, 0, 0));
    Scene *s = e->createScene("measure");
    if (!view || !s) { std::printf("view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    view->setShadows(shadows);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

    // A GROUND SLAB + `items` crates, the Showroom's own density and scale.
    MeshData floorData = enginetest::unitCubeMesh();
    {
        std::vector<MeshCardDesc> cards;
        for (unsigned a = 0; a < 6u; ++a) {
            MeshCardDesc c;
            c.axis = (unsigned char)a;
            c.halfU = c.halfV = 0.5f;
            c.halfDepth = 0.52f;
            cards.push_back(c);
        }
        floorData.cards = cards;
    }
    {
        PbrParams p;
        p.albedo = Colour(0.7f, 0.7f, 0.7f);
        p.roughness = 0.8f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(floorData);
        const NodeId n = s->createNode();
        s->attachMesh(n, mesh, mat);
        enginetest::setNodeScale(s, n, Vec3(30.0f, 0.2f, 30.0f));
        enginetest::setNodePosition(s, n, Vec3(0, -0.1f, 0));
    }
    PbrParams p;
    p.albedo = Colour(0.6f, 0.45f, 0.25f);
    p.roughness = 0.6f;
    const MaterialId mat = s->createPbrMaterial(p);
    const MeshId mesh = s->createMesh(floorData);
    const int side = int(std::ceil(std::sqrt(double(items))));
    for (int i = 0; i < items; ++i) {
        const NodeId n = s->createNode();
        s->attachMesh(n, mesh, mat);
        enginetest::setNodeScale(s, n, Vec3(2.0f, 2.0f, 2.0f));
        enginetest::setNodePosition(s, n,
                                    Vec3(float(i % side) * 3.0f - float(side) * 1.5f, 1.0f,
                                         float(i / side) * 3.0f - float(side) * 1.5f));
    }
    {
        const NodeId sun = s->createNode();
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 2.0f / 3.14159265358979323846f;
        l.castShadows = shadows;
        s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(0.2f, 0.0f, 0.0f, 0.98f), Vec3(1, 1, 1));
        s->setLight(sun, l);
    }

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.cards = GiToggle::On;
    gi.testBoundsMin = Vec3(-32.0f, -2.0f, -32.0f);
    gi.testBoundsMax = Vec3(32.0f, 16.0f, 32.0f);
    s->setGlobalIllumination(gi);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 8.0f, -20.0f), Vec3(0.0f, 1.0f, 0.0f));

    // WARM FIRST. The first frames of a process are the shader/PSO compile
    // storm and the capture permutation's own compile; a number taken there is
    // a number about the compiler (CLAUDE.md).
    render(e, 60);
    const CardCacheStatus warm = s->giStatus().cards;
    std::printf("\n== SURFACE-CACHE-1b, %d items, shadows %s\n", items, shadows ? "ON" : "OFF");
    std::printf("   atlas: %u pages of %u, %u B/texel (emissive %s), %.1f MB, %u pages used\n",
                warm.pages, warm.pageSize, warm.bytesPerTexel, warm.emissiveFormat.c_str(),
                double(warm.bytes) / (1024.0 * 1024.0), warm.pagesUsed);
    std::printf("   resident: %u instances, %u cards, radius %.1f m; budget %u texels\n",
                warm.instancesResident, warm.cardsResident, warm.residencyRadius,
                warm.budgetTexels);

    // THE CAPTURE COST. Every card is re-queued by a light write, then the
    // frames are read one at a time: `captureMs` is measured around the capture
    // workspace's own update and `capturesLastFrame` is what it bought.
    double best = 1e9, worst = 0.0, sum = 0.0, sumWs = 0.0, sumCopy = 0.0;
    unsigned frames = 0u, captured = 0u;
    for (int round = 0; round < 3; ++round) {
        // A light write throws every resident card back on the queue — the
        // cheapest way to re-queue the whole resident set without moving one.
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = (2.0f + 0.001f * float(round)) / 3.14159265358979323846f;
        l.castShadows = shadows;
        const NodeId sun2 = s->createNode();
        s->setNodeTransform(sun2, Vec3(0, 0, 0), Quat(0.2f, 0.0f, 0.0f, 0.98f), Vec3(1, 1, 1));
        s->setLight(sun2, l);
        s->setNodeVisible(sun2, false);
        for (int i = 0; i < 400; ++i) {
            render(e, 1);
            const CardCacheStatus c = s->giStatus().cards;
            if (c.capturesLastFrame) {
                const double perCard = double(c.captureMs) / double(c.capturesLastFrame);
                best = std::min(best, perCard);
                worst = std::max(worst, perCard);
                sum += double(c.captureMs);
                sumWs += double(c.captureWorkspaceMs);
                sumCopy += double(c.captureCopyMs);
                captured += c.capturesLastFrame;
                ++frames;
            }
            if (c.queueLength == 0u && i > 4) break;
        }
    }
    std::printf("   CAPTURE, in the REAL scene manager: %u cards over %u frames, "
                "%.4f ms/card mean (best %.4f, worst %.4f)\n",
                captured, frames, captured ? sum / double(captured) : 0.0, best, worst);
    std::printf("   ...of which %.4f ms/card is the WORKSPACE (pass set-up, cull, Hlms pass"
                " buffer, raster) and %.4f ms/card the five COPIES into the atlas\n",
                captured ? sumWs / double(captured) : 0.0,
                captured ? sumCopy / double(captured) : 0.0);
    std::printf("   (SURFACE-CACHE-0's scratch-scene figure was 0.042-0.057 ms/card in frame,"
                " six cards per workspace update and no copy)\n");
    return 0;
}
