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
#include "jahshaka/engine/PhotonStatus.h"
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
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
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
    const PhotonDispatchStatus d0 = photonDispatchStatus();   // from the first GI work on
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

    // ---- ONE GI REGION (ENGINE-UPGRADE-2B stage 3) ---------------------------------------------
    // Every Photon compute dispatch of this scene's life — the arm's build, the cascades', the
    // field's three, the injections, the mips, the border mirrors — was made inside the GI region
    // (PhotonComponent::GiRegion: the GI step and the arm build at engine.pre, the cards' pass).
    {
        const PhotonDispatchStatus d1 = photonDispatchStatus();
        std::printf("   photon dispatches: %llu in the GI region, %llu outside\n", d1.inRegion - d0.inRegion,
                    d1.outsideRegion - d0.outsideRegion);
        CHECK(d1.inRegion > d0.inRegion, "the cards scene's GI dispatched inside the GI region");
        CHECK(d1.outsideRegion == d0.outsideRegion, "NO Photon dispatch outside the GI region (the cards scene)");
    }
    // ---- A PLAIN VCT SCENE (no cards, no rays): photon_update exists and carries its GI --------------
    {
        e->destroyView(view);
        e->destroyScene(s);
        View *v2 = e->createOffscreenView("photonplain", kSize, kSize, Colour(0, 0, 0));
        Scene *s2 = e->createScene("photonplain");
        v2->setScene(s2);
        s2->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
        const NodeId floorN = enginetest::addTestCube(s2, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
        s2->setNodeTransform(floorN, Vec3(0.0f, -0.55f, 0.0f), Quat(), Vec3(20.0f, 0.1f, 20.0f));
        const NodeId still = enginetest::addTestCube(s2, Colour(0.7f, 0.4f, 0.2f), 0.0f, 0.7f);
        enginetest::setNodePosition(s2, still, Vec3(0.0f, 0.3f, 0.0f));
        enginetest::addDirectionalLight(s2, Vec3(-0.3f, -1.0f, -0.6f), 3.0f);
        enginetest::testCameraLookAt(v2, Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 0.3f, -1.0f));
        const PhotonDispatchStatus p0 = photonDispatchStatus();
        GiParams g2;
        g2.mode = GiMode::Vct;
        g2.quality = GiQuality::Medium;
        g2.numBounces = 1;
        s2->setGlobalIllumination(g2);
        for (int i = 0; i < 12; ++i) e->renderOneFrame();
        e->setFrameMonitor(MonitorLevel::Review);
        { std::vector<FrameRecord> drop; e->renderOneFrame(); e->takeFrameRecords(drop); }
        // A STILL OBJECT MOVED: its cascades re-voxelise and re-inject, frame after frame.
        for (int f = 0; f < 6; ++f) {
            enginetest::setNodePosition(s2, still, Vec3(0.2f * float(f + 1), 0.3f, 0.0f));
            e->renderOneFrame();
        }
        e->setFrameMonitor(MonitorLevel::Off);
        std::vector<FrameRecord> r2;
        e->takeFrameRecords(r2);
        unsigned withUpdate = 0;
        for (const FrameRecord &r : r2)
            for (const FramePass &p : r.passes)
                if (p.pass == "Jahshaka photon update") { ++withUpdate; break; }
        const PhotonDispatchStatus p1 = photonDispatchStatus();
        std::printf("   plain VCT: %u of %zu frames ran photon_update; dispatches %llu in, %llu outside\n",
                    withUpdate, r2.size(), p1.inRegion - p0.inRegion, p1.outsideRegion - p0.outsideRegion);
        CHECK(withUpdate > 0, "PRESENCE: a plain VCT scene has photon_update and runs it on the frames its GI works");
        CHECK(p1.inRegion > p0.inRegion && p1.outsideRegion == p0.outsideRegion,
              "NO Photon dispatch outside the GI region (the plain VCT scene: the arm, the moved still's cascades)");
        // ---- THE VERB- AND DRAIN-TIME BUILDS (stage 3 read, finding A) ---------------------------
        // After the scene has run: a GI configuration change (setGlobalIllumination rebuilds the
        // arm through giDeferrable), an explicit refreshGi (the drain's plan() ->
        // refreshGlobalIllumination -> the staged build / rebuildVct) and a material edit, each
        // settled. RED BEFORE THE FIX: the arm those two rebuilt was dispatched outside every region.
        const MaterialId editMat = s2->createPbrMaterial(PbrParams());
        const NodeId edited = s2->createNode();
        s2->attachMesh(edited, s2->createMesh(enginetest::unitCubeMesh()), editMat);
        enginetest::setNodePosition(s2, edited, Vec3(-1.0f, 0.3f, 0.5f));
        for (int i = 0; i < 6; ++i) e->renderOneFrame();
        const PhotonDispatchStatus v0 = photonDispatchStatus();
        GiParams g3 = g2;
        g3.numBounces = 2;   // a configuration change: the arm rebuilds
        s2->setGlobalIllumination(g3);
        for (int i = 0; i < 6; ++i) e->renderOneFrame();
        s2->refreshGlobalIllumination();
        for (int i = 0; i < 6; ++i) e->renderOneFrame();
        PbrParams red;
        red.albedo = Colour(0.9f, 0.1f, 0.1f);
        s2->setPbrMaterial(editMat, red);
        for (int i = 0; i < 12; ++i) e->renderOneFrame();
        const PhotonDispatchStatus v1 = photonDispatchStatus();
        std::printf("   set GI + refreshGi + a material edit: dispatches %llu in, %llu outside\n",
                    v1.inRegion - v0.inRegion, v1.outsideRegion - v0.outsideRegion);
        CHECK(v1.inRegion > v0.inRegion && v1.outsideRegion == v0.outsideRegion,
              "NO Photon dispatch outside the GI region (set GI after frames, refreshGi, a material edit)");
        e->destroyView(v2);
        e->destroyScene(s2);
    }

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
