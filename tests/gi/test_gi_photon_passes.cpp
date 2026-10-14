// gi.photon_passes — PHOTON'S GPU WORK IS THREE COMPOSITOR PASSES, IN ORDER
// (ENGINE-UPGRADE-2 stage 2; photon/PhotonComponent.h).
//
// The frame's own listing is the evidence: the monitor records every pass the compositor
// executed, in execution order (FrameRecord.passes). On a carded scene drawn by a view
// with rays (the reflection and the gather on, the hit decode in its chain):
//   1. photon_update ("Jahshaka photon update") runs in the GI workspace right after the
//      card capture's, before any of the view's passes;
//   2. in the view's chain, photon_trace runs in front of the hit decode pass, and
//      photon_resolve after it and in front of the first pass that shades with the
//      prepass (the screen decode, else the opaque pass);
//   3. each runs ONCE per frame per view.
// RED ON THE BASE: none of the three passes exists (the work rode the listeners of the
// capture workspace and of the view's scene passes).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 96;

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-photon-passes-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("photonpasses", kSize, kSize, Colour(0, 0, 0));
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.photon_passes skips\n");
        return 0;
    }
    Scene *s = e->createScene("photonpasses");
    view->setScene(s);
    view->setShadows(true);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
    {
        MeshData md = enginetest::unitCubeMesh();
        md.cards = enginetest::boxCards(0.5f);
        PbrParams fp;
        fp.albedo = Colour(0.6f, 0.6f, 0.6f);
        fp.roughness = 0.9f;
        const NodeId floorN = s->createNode();
        s->attachMesh(floorN, s->createMesh(md), s->createPbrMaterial(fp));
        s->setNodeTransform(floorN, Vec3(0.0f, -0.55f, 0.0f), Quat(), Vec3(20.0f, 0.1f, 20.0f));
        PbrParams mp;
        mp.albedo = Colour(1, 1, 1);
        mp.metalness = 1.0f;
        mp.roughness = 0.05f;
        const NodeId mirror = s->createNode();
        s->attachMesh(mirror, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(mp));
        enginetest::setNodePosition(s, mirror, Vec3(0.0f, 0.5f, -2.0f));
        // A MOVER (no cards, not voxelised): a ray hit on it is the hit decode's.
        const NodeId mov = enginetest::addTestCube(s, Colour(0.7f, 0.4f, 0.2f), 0.0f, 0.7f);
        s->setNodeMovable(mov, true);
        enginetest::setNodePosition(s, mov, Vec3(1.0f, 0.3f, 1.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.6f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 0.3f, -1.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.cards = true;
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);
    for (int i = 0; i < 12; ++i) e->renderOneFrame();

    e->setFrameMonitor(MonitorLevel::Review);
    {
        std::vector<FrameRecord> drop;
        e->renderOneFrame();
        e->takeFrameRecords(drop);
    }
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    e->setFrameMonitor(MonitorLevel::Off);
    std::vector<FrameRecord> recs;
    e->takeFrameRecords(recs);
    CHECK(!recs.empty(), "the monitor recorded frames");

    const RayQueryStatus rq = s->rayQueryStatus();
    std::printf("   rays: reflections %d, hit decode draws %d\n", rq.enabled ? 1 : 0, rq.hitDecodeDraws);
    unsigned good = 0, framesSeen = 0;
    std::string listing;
    for (const FrameRecord &r : recs) {
        if (r.passes.empty()) continue;
        ++framesSeen;
        int update = -1, firstView = -1, trace = -1, decode = -1, resolve = -1, shade = -1;
        unsigned nUpdate = 0, nTrace = 0, nResolve = 0;
        for (size_t i = 0; i < r.passes.size(); ++i) {
            const FramePass &p = r.passes[i];
            const bool ofView = p.workspace.find("photonpasses") != std::string::npos ||
                                p.node.find("photonpasses") != std::string::npos;
            if (p.pass == "Jahshaka photon update") { ++nUpdate; if (update < 0) update = int(i); }
            if (p.pass == "Jahshaka photon trace") { ++nTrace; if (trace < 0) trace = int(i); }
            if (p.pass == "Jahshaka photon resolve") { ++nResolve; if (resolve < 0) resolve = int(i); }
            if (p.pass == "Jahshaka hit decode" && decode < 0) decode = int(i);
            if ((p.pass == "Jahshaka atom decode" || p.pass == "Jahshaka opaque") && shade < 0 && resolve >= 0)
                shade = int(i);
            if (ofView && firstView < 0) firstView = int(i);
        }
        if (listing.empty())
            for (const FramePass &p : r.passes) listing += "    " + p.workspace + " / " + p.pass + "\n";
        const bool ok = nUpdate == 1 && nTrace == 1 && nResolve == 1 && update >= 0 && trace > update &&
                        decode > trace && resolve > decode && shade == resolve + 1;
        if (ok) ++good;
        else
            std::printf("   frame %llu: update %d (x%u) trace %d (x%u) decode %d resolve %d (x%u) shade %d\n",
                        r.frame, update, nUpdate, trace, nTrace, decode, resolve, nResolve, shade);
    }
    if (!good) std::printf("   the first frame's listing:\n%s", listing.c_str());
    CHECK(rq.enabled, "the view traces (rays on: the ray passes have work)");
    CHECK(framesSeen > 0 && good == framesSeen,
          "EVERY FRAME: photon_update once (the GI workspace, before the view), then in the view's chain "
          "photon_trace -> the hit decode -> photon_resolve -> the first pass that shades with the prepass, "
          "each once");

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
