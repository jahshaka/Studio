// gi.union_clear — A COLLAPSED DIRTY UNION NEVER OUTLIVES ITS LIST (ENGINE-UPGRADE-U4 decision 18.1).
//
// The cascades' dirty list collapses to a running union once it is dense (decision 12.2) and keeps the boxes it noted
// as its members, so a cascade re-placed before the mark is tested against what was noted (decision 16.4). A
// whole-arm rebuild answers everything the list held and clears it — and the union's state and members must go with
// it: a later mark (the refresh path marks without anything moved) reading stale members marks the fresh chain from
// boxes the build already answered, and `marked > 0` then skips the light-only refresh.
//
// THE SEQUENCE: a dense block of 100 cubes removed (each departure notes its box: the list collapses) and, in the SAME
// record, a configuration change that rebuilds the arm (the drain applies it after the removals: the rebuild clears
// the list); then, with nothing moving, a lamp added. The lamp's re-solve (the scheduler's settle) must re-voxelise
// NOTHING — a light edit is answered by re-injecting the light, never by re-voxelising geometry nobody moved.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        char m_[512]; std::snprintf(m_, sizeof m_, __VA_ARGS__);                \
        if (cond) std::printf("ok: %s\n", m_);                                  \
        else { std::printf("FAIL: %s\n", m_); ++failures; }                     \
    } while (0)

static unsigned long long cascadeRebuilds(const GiStatus &g)
{
    unsigned long long r = 0ull;
    for (const auto &c : g.cascades) r += c.rebuilds;
    return r;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-union-clear-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("union_clear", 96, 96, Colour(0, 0, 0));
    Scene *s = e->createScene("union_clear");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0.2f, 0.2f, 0.2f), Colour(0.2f, 0.2f, 0.2f));
    const NodeId ground = enginetest::addTestCube(s, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(s, ground, Vec3(60.0f, 0.1f, 60.0f));
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    // A DENSE BLOCK: 100 touching cubes (0.5 m at a 0.5 m pitch), so the departures' boxes collapse to their union.
    std::vector<NodeId> block;
    for (int i = 0; i < 100; ++i) {
        const NodeId n = enginetest::addTestCube(s, Colour(0.6f, 0.5f, 0.4f), 0.0f, 0.8f);
        enginetest::setNodePosition(s, n, Vec3(float(i % 10) * 0.5f - 2.25f, 0.25f, float(i / 10) * 0.5f - 2.25f));
        enginetest::setNodeScale(s, n, Vec3(0.5f, 0.5f, 0.5f));
        block.push_back(n);
    }
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 4.0f, 9.0f), Vec3(0.0f, 0.5f, 0.0f)));
    GiParams gi;
    gi.gather = GiToggle::Off;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 1;   // a live scheduler: the lamp's re-solve runs
    const auto frame = [&](const std::vector<NodeId> &removals) {
        for (NodeId id : removals) s->removeNode(id);   // the scene's stream: applied in the drain, before the fields
        FrameInputScene part;
        part.scene = s;
        part.gi = gi;
        FrameInput in;
        in.scenes.push_back(part);
        enginetest::frames(e, 1, in);
    };
    for (int f = 0; f < 40; ++f) frame({});
    const GiStatus g0 = s->published().gi;
    CHECK(g0.cascades.size() >= 2u, "the chain is built (%zu cascades)", g0.cascades.size());

    // THE REMOVALS AND THE REBUILD, ONE RECORD.
    gi.numBounces = 2;
    frame(block);
    for (int f = 0; f < 30; ++f) frame({});   // the rebuilt chain settles; nothing moves
    const GiStatus g1 = s->published().gi;
    std::printf("   after the removals + the rebuild: arm rebuilds %llu -> %llu, cascade rebuilds %llu -> %llu\n",
                g0.rebuilds, g1.rebuilds, cascadeRebuilds(g0), cascadeRebuilds(g1));
    CHECK(g1.rebuilds > g0.rebuilds, "the configuration change rebuilt the arm (%llu -> %llu)", g0.rebuilds,
          g1.rebuilds);

    // THE LAMP, NOTHING MOVING.
    const NodeId lamp = s->createNode();
    s->setNodeTransform(lamp, Vec3(0.0f, 2.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1.0f, 0.8f, 0.6f);
    l.intensity = 10.0f;
    l.range = 30.0f;
    CHECK(s->setLight(lamp, l), "the lamp");
    const unsigned long long resolves0 = s->published().gi.schedulerResolves;
    const unsigned long long rebuilds0 = cascadeRebuilds(s->published().gi);
    int f = 0;
    for (; f < 120 && s->published().gi.schedulerResolves == resolves0; ++f) frame({});
    for (int k = 0; k < 10; ++k) frame({});   // the frames after the re-solve: anything it marked would rebuild here
    const GiStatus g2 = s->published().gi;
    std::printf("   the lamp's re-solve after %d frames: resolves %llu -> %llu, cascade rebuilds %llu -> %llu\n", f,
                resolves0, g2.schedulerResolves, rebuilds0, cascadeRebuilds(g2));
    CHECK(g2.schedulerResolves > resolves0, "the lamp's edit was re-solved (%llu -> %llu)", resolves0,
          g2.schedulerResolves);
    CHECK(cascadeRebuilds(g2) == rebuilds0, "a lamp edit with nothing moved re-voxelises NOTHING — the rebuild's clear "
          "took the union's members with it (%llu cascade rebuilds after the lamp)", cascadeRebuilds(g2) - rebuilds0);

    view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "gi.union_clear: FAILED (%d)\n" : "gi.union_clear: PASS\n", failures);
    return failures ? 1 : 0;
}
