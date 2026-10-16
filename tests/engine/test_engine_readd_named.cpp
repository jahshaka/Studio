// engine.readd_named — THE 10k RE-ADD IS PAID IN WHAT IT NAMED (ENGINE-UPGRADE-2B stage 5).
//
// The reparent bench's fragment at the engine's seams (bench_scenegraph.cpp (c): a 500-node subtree
// moved between two hosts, its items re-created by the mirror): the subtree's root re-parented, each of
// its 500 items detached and attached again, ONE renderOneFrame - at 1k and at 10k items, on a GI scene
// whose movement scan and lamp-map caster scan both run. Per item everything is O(1) (indexItemNode:
// a push and a mark; unindexItemNode: a swap-remove); the frame must pay O(the 500), never O(the scene):
//   named        the slots the GPU scene's walk took from the names: <= 500 (the subtree; + 0 movers)
//   compared     the slots it compared:                               <= 500
//   flags        the flags words it recomputed:                       <= 500
//   full walks   whole GPU-scene walks + full GI / caster scans:      == 0
// RED BEFORE (the cap cliff): attachMesh named a node three to four times, so the 500 re-adds pushed
// ~2,000 names into lists capped at items + decals + 64 - under the cap at 10k, OVER it at 1k, where the
// overflow owed the whole walk on the SMALLER scene.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/GpuSubstrate.h"
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

static void runScale(Engine *e, unsigned items)
{
    const std::string tag = "readd" + std::to_string(items);
    View *view = e->createOffscreenView(tag.c_str(), 96, 96, Colour(0.05f, 0.06f, 0.08f, 1.0f));
    Scene *s = e->createScene(tag.c_str());
    view->setScene(s);
    view->setShadows(true);
    s->setAmbient(Colour(0.2f, 0.2f, 0.2f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    // A CACHED LAMP (a point light with shadows): the lamp-map cache's caster scan runs.
    {
        LightDesc ld;
        ld.type = LightType::Point;
        ld.colour = Colour(1, 1, 1);
        ld.intensity = 20.0f;
        ld.range = 30.0f;
        ld.castShadows = true;
        const NodeId ln = s->createNode();
        s->setLight(ln, ld);
        enginetest::setNodePosition(s, ln, Vec3(0.0f, 6.0f, 0.0f));
    }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 8.0f, 30.0f), Vec3(0.0f, 0.0f, 0.0f));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createPbrMaterial(PbrParams());
    const NodeId hostA = s->createNode(), hostB = s->createNode();
    enginetest::setNodePosition(s, hostB, Vec3(5.0f, 0.0f, 0.0f));
    const NodeId sub = s->createNode();
    s->setNodeParent(sub, hostA);
    const unsigned kSub = 500u;
    std::vector<NodeId> subNodes;
    const unsigned side = 100u;
    for (unsigned i = 0; i < items; ++i) {
        const NodeId n = s->createNode();
        s->attachMesh(n, cube, mat);
        if (i < kSub) { s->setNodeParent(n, sub); subNodes.push_back(n); }
        enginetest::setNodePosition(s, n, Vec3(float(i % side) * 0.6f - 30.0f, 0.3f, float(i / side) * 0.6f - 30.0f));
        enginetest::setNodeScale(s, n, Vec3(0.3f, 0.3f, 0.3f));
    }
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    s->setGlobalIllumination(gi);
    enginetest::frames(e, 60);
    const GpuSubstrateStatus a = gpuSubstrateStatus(*s);
    // THE FRAGMENT: remove, add (the items re-created), ONE frame.
    s->setNodeParent(sub, hostB);
    for (NodeId n : subNodes) s->detachMesh(n);
    for (NodeId n : subNodes) s->attachMesh(n, cube, mat);
    enginetest::frames(e, 1);
    const GpuSubstrateStatus b = gpuSubstrateStatus(*s);
    const unsigned long long named = b.slotsNamed - a.slotsNamed, compared = b.slotsCompared - a.slotsCompared,
                             flags = b.flagsRecomputed - a.flagsRecomputed,
                             full = (b.wholeWalks - a.wholeWalks) + (b.scansFull - a.scansFull);
    std::printf("READD at %u items: named %llu, compared %llu, flags recomputed %llu, full walks %llu "
                "(whole GPU-scene walks %llu, full scans %llu)\n", items, named, compared, flags, full,
                b.wholeWalks - a.wholeWalks, b.scansFull - a.scansFull);
    CHECK(b.live, "%u: the GPU scene is live (not vacuous)", items);
    CHECK(named > 0 && named <= kSub, "%u: the walk takes at most the re-added subtree's %u slots from the names (%llu)",
          items, kSub, named);
    CHECK(compared <= kSub, "%u: it compares at most those (%llu)", items, compared);
    CHECK(flags <= kSub, "%u: it recomputes at most their flags words (%llu)", items, flags);
    CHECK(full == 0u, "%u: NO FULL WALK - the GPU scene's, the GI scan's or the caster scan's (%llu)", items, full);
    e->destroyView(view);
    e->destroyScene(s);
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-engine-readd-named-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    runScale(engine.get(), 1000u);
    runScale(engine.get(), 10000u);
    std::printf("engine.readd_named: %s (%d failure(s))\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
