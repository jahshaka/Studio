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
//
// `sc1b_measure showroom` is item 8 of the fix round: WHAT DOES
// `SHADOW_NODE_RECALCULATE` PER CARD COST, on a scene shaped like Showroom 2
// (its density AND its three shadow-casting POINT lamps, which is where the
// Grand Showroom's 25 ms once came from — a directional-only fixture cannot
// see a cube-map caster pass at all). BOTH ARMS IN ONE PROCESS: the recalc arm,
// then the card row off and on again with `JAH_CARD_NO_SHADOW_RECALC` set,
// which rebuilds the capture workspace with the node on REUSE — the lever is
// read once per cache build for exactly this.
//
// IT IS SHOWROOM-2-SHAPED AND NOT THE SAMPLE: this tool links the engine only
// and cannot open a Studio project. What it reproduces is the variable the
// number turns on — the caster set and the lamps.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

/// The capture cost of the resident set as it stands, drained three times.
/// Returns ms per card and writes the card count.
static double measureCaptures(Engine *e, Scene *s, unsigned &cardsOut, double &wsOut)
{
    double sum = 0.0, sumWs = 0.0;
    unsigned captured = 0u;
    for (int round = 0; round < 3; ++round) {
        // A light write throws every resident card back on the queue.
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = (2.0f + 0.001f * float(round)) / 3.14159265358979323846f;
        l.castShadows = true;
        const NodeId sun2 = s->createNode();
        s->setNodeTransform(sun2, Vec3(0, 0, 0), Quat(0.2f, 0.0f, 0.0f, 0.98f), Vec3(1, 1, 1));
        s->setLight(sun2, l);
        s->setNodeVisible(sun2, false);
        for (int i = 0; i < 600; ++i) {
            render(e, 1);
            const CardCacheStatus c = s->giStatus().cards;
            if (c.capturesLastFrame) {
                sum += double(c.captureMs);
                sumWs += double(c.captureWorkspaceMs);
                captured += c.capturesLastFrame;
            }
            if (c.queueLength == 0u && i > 4) break;
        }
    }
    cardsOut = captured;
    wsOut = captured ? sumWs / double(captured) : 0.0;
    return captured ? sum / double(captured) : 0.0;
}

/// ITEM 8 — the price of a per-card shadow-node recalculation, both arms in one
/// process, on a Showroom-2-shaped scene at the High tier's own budget.
static int showroomArm()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "sc1b-showroom-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("showroom", 1920u, 1080u, Colour(0, 0, 0));
    Scene *s = e->createScene("showroom");
    if (!view || !s) { std::printf("view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    view->setShadows(true);
    s->setAmbient(Colour(0.12f, 0.12f, 0.14f), Colour(0.08f, 0.08f, 0.10f));

    MeshData md = enginetest::unitCubeMesh();
    {
        std::vector<MeshCardDesc> cards;
        for (unsigned a2 = 0; a2 < 6u; ++a2) {
            MeshCardDesc c;
            c.axis = (unsigned char)a2;
            c.halfU = c.halfV = 0.5f;
            c.halfDepth = 0.52f;
            cards.push_back(c);
        }
        md.cards = cards;
    }
    PbrParams fp;
    fp.albedo = Colour(0.7f, 0.7f, 0.7f);
    fp.roughness = 0.8f;
    const MaterialId floorMat = s->createPbrMaterial(fp);
    const MeshId mesh = s->createMesh(md);
    {
        const NodeId n = s->createNode();
        s->attachMesh(n, mesh, floorMat);
        enginetest::setNodeScale(s, n, Vec3(24.0f, 0.2f, 24.0f));
        enginetest::setNodePosition(s, n, Vec3(0, -0.1f, 0));
    }
    // FOUR WALLS + forty props: Showroom 2's own shape (rooms of sixteen), so
    // the caster set a lamp's cube faces sweep is the sample's, not a plane's.
    PbrParams wp;
    wp.albedo = Colour(0.6f, 0.58f, 0.55f);
    wp.roughness = 0.7f;
    const MaterialId wallMat = s->createPbrMaterial(wp);
    const float half = 12.0f;
    for (int w = 0; w < 4; ++w) {
        const NodeId n = s->createNode();
        s->attachMesh(n, mesh, wallMat);
        const bool alongX = (w & 1) == 0;
        enginetest::setNodeScale(s, n, alongX ? Vec3(2.0f * half, 5.0f, 0.3f)
                                              : Vec3(0.3f, 5.0f, 2.0f * half));
        enginetest::setNodePosition(s, n,
                                    alongX ? Vec3(0.0f, 2.5f, (w == 0 ? half : -half))
                                           : Vec3((w == 1 ? half : -half), 2.5f, 0.0f));
    }
    PbrParams pp;
    pp.albedo = Colour(0.6f, 0.45f, 0.25f);
    pp.roughness = 0.6f;
    const MaterialId propMat = s->createPbrMaterial(pp);
    for (int i = 0; i < 40; ++i) {
        const NodeId n = s->createNode();
        s->attachMesh(n, mesh, propMat);
        enginetest::setNodeScale(s, n, Vec3(1.6f, 1.6f, 1.6f));
        enginetest::setNodePosition(s, n, Vec3(float(i % 8) * 2.8f - 9.8f, 0.8f,
                                               float(i / 8) * 2.8f - 7.0f));
    }
    // THE SUN + THREE SHADOW-CASTING POINT LAMPS — the half a directional-only
    // fixture cannot measure: a point lamp's shadow is a CUBE, six faces per
    // recalculation.
    {
        const NodeId sun = s->createNode();
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 2.0f / 3.14159265358979323846f;
        l.castShadows = true;
        s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(0.2f, 0.0f, 0.0f, 0.98f), Vec3(1, 1, 1));
        s->setLight(sun, l);
    }
    for (int i = 0; i < 3; ++i) {
        const NodeId lamp = s->createNode();
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1.0f, 0.95f, 0.85f);
        l.intensity = 6.0f;
        l.range = 14.0f;
        l.castShadows = true;
        s->setNodeTransform(lamp, Vec3(float(i - 1) * 7.0f, 3.5f, float(i - 1) * 4.0f), Quat(),
                            Vec3(1, 1, 1));
        s->setLight(lamp, l);
    }

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;     // the High tier's own card budget
    gi.cards = GiToggle::On;
    gi.testBoundsMin = Vec3(-26.0f, -2.0f, -26.0f);
    gi.testBoundsMax = Vec3(26.0f, 14.0f, 26.0f);
    s->setGlobalIllumination(gi);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 6.0f, -18.0f), Vec3(0.0f, 1.0f, 0.0f));
    render(e, 90);   // warm: the compile storm and the first captures

    const CardCacheStatus warm = s->giStatus().cards;
    std::printf("\n== ITEM 8: SHADOW_NODE_RECALCULATE per card, Showroom-2-shaped,"
                " High tier\n");
    std::printf("   scene: 45 carded instances (floor, 4 walls, 40 props), a sun and THREE"
                " shadow-casting POINT lamps\n");
    std::printf("   resident: %u instances, %u cards, %u of %u pages, budget %u texels"
                " (%u cards a frame)\n",
                warm.instancesResident, warm.cardsResident, warm.pagesUsed, warm.pages,
                warm.budgetTexels, warm.budgetTexels / (128u * 128u));

    unsigned cardsA = 0u, cardsB = 0u;
    double wsA = 0.0, wsB = 0.0;
    const double armA = measureCaptures(e, s, cardsA, wsA);

    // ARM B, IN THE SAME PROCESS: the lever is read once per cache BUILD, so
    // the row goes off (the cache is freed) and on again (rebuilt on REUSE).
    setenv("JAH_CARD_NO_SHADOW_RECALC", "1", 1);
    GiParams off = gi;
    off.cards = GiToggle::Off;
    s->setGlobalIllumination(off);
    render(e, 4);
    s->setGlobalIllumination(gi);
    render(e, 90);
    const double armB = measureCaptures(e, s, cardsB, wsB);

    std::printf("   ARM A  recalculate per card : %.4f ms/card over %u cards"
                " (workspace %.4f)\n", armA, cardsA, wsA);
    std::printf("   ARM B  shadow node REUSED   : %.4f ms/card over %u cards"
                " (workspace %.4f)\n", armB, cardsB, wsB);
    std::printf("   THE RECALCULATION COSTS     : %.4f ms/card (%.0f%% of the capture)\n",
                armA - armB, armA > 0.0 ? 100.0 * (armA - armB) / armA : 0.0);
    std::printf("   at the High budget of %u cards a frame that is %.3f ms a frame\n",
                warm.budgetTexels / (128u * 128u),
                (armA - armB) * double(warm.budgetTexels / (128u * 128u)));
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "showroom") return showroomArm();
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
