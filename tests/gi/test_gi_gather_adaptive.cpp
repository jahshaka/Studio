// gi.gather_adaptive_fair — THE ADAPTIVE BUDGET STARVES NO CELL (PHOTON-II-1 F3, the merge
// read). The placement gives a second probe to at most `cap` of the cells whose one probe is
// not enough, RANKED in a fixed order per frame (rq_probe_place.comp: the tiles rotated by the
// frame index, the cells of a tile rotated by a hash of (frame, tile)). With the cap below the
// demand the ranking decides who goes without — and a ranking that never rotates starves the
// same cells for ever (the merge read's F3: row-major inside a tile, a 160 x 90 view's lower
// rows). A 160 x 90 view (10 x 6 cells, two tiles) of a forest of thin poles, the probes at
// their cells' centres (jitterOff: the asking set is the same every frame):
//   1. the cap LIFTED: the asking set S, every asker gets its probe;
//   2. the cap at a quarter of |S|: every frame uses the whole budget, and every cell of S
//      has had its second probe by frame ceil(|S| / cap) + 1 (the window slides by `cap` a
//      frame; +1 for a readback frame the host may skip). A ranking that never moves (the
//      row-major order before F3) leaves |S| - cap cells unserved for ever.
// Read through the gather's readback door (GatherStatus::adaptiveCells). Frames, never time.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const int kWithin = 32;

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-gather-adaptive-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("adaptive", 160, 90, Colour(0, 0, 0));
    Scene *s = e->createScene("adaptive");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setOffscreenContract(OffscreenContract::StillPicture);
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_adaptive_fair skips cleanly\n");
        return 0;
    }
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams m; m.albedo = Colour(0.7f, 0.7f, 0.7f); m.roughness = 1.0f;
    const MaterialId mat = s->createPbrMaterial(m);
    auto box = [&](const Vec3 &p, const Vec3 &sc) {
        const NodeId n = s->createNode();
        s->attachMesh(n, cube, mat);
        s->setNodeTransform(n, p, Quat(), sc);
    };
    box(Vec3(0, -0.25f, 0), Vec3(30, 0.5f, 30));       // floor
    box(Vec3(0, 3, -8), Vec3(30, 6, 0.5f));             // back wall
    // THE FOREST: thin poles at staggered depths, a silhouette in most cells.
    for (int i = 0; i < 40; ++i)
        box(Vec3(-4.0f + 0.2f * float(i), 1.5f, -1.0f - 0.15f * float(i % 7)), Vec3(0.05f, 3.0f, 0.05f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 2.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: gi\n"); return 1; }
    enginetest::testCameraLookAt(view, Vec3(0, 1.5f, 4.0f), Vec3(0, 1.2f, -4.0f));

    GatherTuning t;
    t.jitterOff = true;
    t.readback = true;
    t.restOff = true;
    t.adaptiveCap = 1000;    // lifted: one per cell is the most a grid can ask
    s->setGatherTuning(t);
    for (int i = 0; i < 30; ++i) e->renderOneFrame();
    GatherStatus st = s->giStatus().gather;
    const std::vector<unsigned char> asked = st.adaptiveCells;
    const unsigned cells = st.probesX * st.probesY;
    unsigned demand = 0;
    for (unsigned char a : asked) demand += a;
    std::printf("   grid %u x %u, %u cells ask for a second probe (requested %u; readback %zu cells, %zu px)\n",
                st.probesX, st.probesY, demand, st.adaptiveRequested, asked.size(), st.irradiance.size() / 4u);
    if (asked.size() != cells) { std::printf("FAIL: the readback door returned no cells\n"); return 1; }
    CHECK_MSG(asked.size() == cells && demand >= 12u && st.adaptive == demand,
              "the cap lifted: %u of %u cells ask and every one gets its probe (appended %u)", demand, cells,
              st.adaptive);

    const unsigned cap = std::max(1u, demand / 4u);
    t.adaptiveCap = int(cap);
    s->setGatherTuning(t);
    for (int i = 0; i < 6; ++i) e->renderOneFrame();
    std::vector<unsigned char> got(cells, 0u);
    unsigned lastFrame = ~0u, frames = 0, fullBudget = 0, firstAll = 0;
    for (int i = 0; i < kWithin + 8 && frames < unsigned(kWithin); ++i) {
        e->renderOneFrame();
        st = s->giStatus().gather;
        if (st.adaptiveCells.size() != cells || st.irradianceFrame == lastFrame) continue;
        lastFrame = st.irradianceFrame;
        ++frames;
        unsigned n = 0;
        for (unsigned c = 0; c < cells; ++c) {
            n += st.adaptiveCells[c];
            got[c] |= st.adaptiveCells[c];
        }
        if (n == cap) ++fullBudget;
        bool all = true;
        for (unsigned c = 0; c < cells; ++c) all = all && (!asked[c] || got[c]);
        if (all && !firstAll) firstAll = frames;
    }
    unsigned starved = 0;
    for (unsigned c = 0; c < cells; ++c) starved += (asked[c] && !got[c]) ? 1u : 0u;
    std::printf("   cap %u: %u frames read, %u used the whole budget, every asker served by frame %u, %u never\n",
                cap, frames, fullBudget, firstAll, starved);
    CHECK_MSG(frames >= 16u && fullBudget == frames,
              "with the cap at %u every frame spends the whole budget (%u of %u frames)", cap, fullBudget, frames);
    const unsigned bound = (demand + cap - 1u) / cap + 1u;
    CHECK_MSG(starved == 0u && firstAll > 0u && firstAll <= bound,
              "NO CELL IS STARVED: every one of the %u asking cells gets its second probe by frame %u (bar "
              "ceil(%u / %u) + 1 = %u; %u never)", demand, firstAll, demand, cap, bound, starved);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
