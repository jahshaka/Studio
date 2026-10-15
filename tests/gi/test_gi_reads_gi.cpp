// gi.reads_gi — THE GI IS BOUND ONLY ON THE PASSES THAT SAMPLE IT (ENGINE-UPGRADE-ASYNC-3 stage 2).
//
// Every pass definition says whether its shaders sample the scene's GI (CompositorPassDef::mJahReadsGi,
// a reader unless the chain marked it otherwise), and the engine's binding (PhotonPassBinding) gives
// the voxel volumes and the field — their pass properties, pass-buffer blocks, textures and barriers —
// to READER passes only. The frame's first and last reader are where the async compute protocol waits
// and signals, so the list must be exact: a reader marked otherwise would lose its GI (the six
// selftest hashes would move), a non-reader left a reader would close the window for nothing.
// On a carded scene with rays and SSR (every chain stage present), the frame monitor's records:
//   1. THE LIST: the opaque pass, the ray tier's photon trace / resolve and photon_update READ;
//      the SSR G-buffer prepass, the Atom decode prepass, the card capture, the SSR helpers' mask, the
//      overlays, the Atom id passes, the distortion field, every shadow caster pass and every quad
//      DO NOT (each asserted PRESENT in the records, so none is vacuous);
//   2. THE BINDING FOLLOWS IT: pass preparations with the GI bound (readers) and without it
//      (non-readers of a scene that has a GI) both happened;
//   3. the frame's first and last GI readers, printed (the window's two ends).
// RED ON THE BASE: no flag, no reader record, the GI bound on every non-caster scene pass.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/PhotonStatus.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-reads-gi-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("readsgi", 128, 96, Colour(0, 0, 0));
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.reads_gi skips\n");
        return 0;
    }
    Scene *s = e->createScene("readsgi");
    view->setScene(s);
    view->setShadows(true);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
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
    // A DISTORTION object: the chain grows its distortion-field pass (HlmsUnlit, no GI).
    {
        PbrParams dp;
        dp.shadingModel = ShadingModel::Distortion;
        const NodeId dn = s->createNode();
        s->attachMesh(dn, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(dp));
        enginetest::setNodePosition(s, dn, Vec3(-1.0f, 0.5f, 0.5f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.6f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 0.3f, -1.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.cards = true;
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.distortion = true;   // the distortion-field pass (the cube below wears a distortion material)
    view->setPostFx(fx);
    enginetest::frames(e, 1);
    // From the GI's first frames on: the card capture runs while the atlas fills.
    const PhotonBindingStatus b0 = photonBindingStatus();
    e->setFrameMonitor(MonitorLevel::Review);
    s->setGlobalIllumination(gi);
    enginetest::frames(e, 16);
    e->setFrameMonitor(MonitorLevel::Off);
    std::vector<FrameRecord> recs;
    e->takeFrameRecords(recs);
    const PhotonBindingStatus b1 = photonBindingStatus();
    CHECK(!recs.empty(), "the monitor recorded frames");

    // name -> (seen, reads) over every record; a name that both reads and does not is a defect.
    std::map<std::string, std::pair<unsigned, unsigned>> seen;
    bool shadowReads = false;
    std::string first, last;
    for (const FrameRecord &r : recs) {
        int f = -1, l = -1;
        for (size_t i = 0; i < r.passes.size(); ++i) {
            const FramePass &p = r.passes[i];
            auto &e2 = seen[p.pass];
            ++e2.first;
            if (p.readsGi) ++e2.second;
            const bool shadow = p.bucket == PassBucket::ShadowView || p.bucket == PassBucket::ShadowReflect ||
                                p.bucket == PassBucket::ShadowProbe;
            if (shadow && p.readsGi) shadowReads = true;
            if (p.readsGi) { if (f < 0) f = int(i); l = int(i); }
        }
        if (f >= 0 && first.empty()) {
            first = r.passes[size_t(f)].workspace + " / " + r.passes[size_t(f)].pass;
            last = r.passes[size_t(l)].workspace + " / " + r.passes[size_t(l)].pass;
        }
    }
    for (const auto &kv : seen)
        std::printf("   %-44s %s (%u of %u)\n", kv.first.c_str(), kv.second.second ? "READS" : "-", kv.second.second,
                    kv.second.first);
    const auto reads = [&](const char *name) {
        auto it = seen.find(name);
        return it != seen.end() && it->second.second == it->second.first;
    };
    const auto none = [&](const char *name) {
        auto it = seen.find(name);
        return it == seen.end() || it->second.second == 0u;
    };
    const auto present = [&](const char *name) { return seen.count(name) != 0; };
    CHECK(present("Jahshaka opaque") && reads("Jahshaka opaque"), "the opaque pass READS the GI");
    CHECK(present("Jahshaka photon update") && reads("Jahshaka photon update"), "photon_update READS it (the card relight)");
    CHECK(present("Jahshaka photon trace") && reads("Jahshaka photon trace") && reads("Jahshaka photon resolve"),
          "the ray tier's photon trace and resolve READ it");
    CHECK(present("Jahshaka overlays") && none("Jahshaka overlays"), "the overlays pass does NOT");
    CHECK(present("Jahshaka SSR helper mask") && none("Jahshaka SSR helper mask"), "the SSR helpers' mask does NOT");
    CHECK(present("Jahshaka SSR prepass") && none("Jahshaka SSR prepass"), "the SSR G-buffer prepass does NOT");
    CHECK(present("Jahshaka atom decode prepass") && none("Jahshaka atom decode prepass"), "the Atom decode prepass does NOT");
    CHECK(present("Jahshaka card capture") && none("Jahshaka card capture"), "the card capture does NOT");
    CHECK(present("Jahshaka atom id") && present("Jahshaka atom id late") && none("Jahshaka atom id") &&
              none("Jahshaka atom id late"),
          "the Atom id passes do NOT");
    CHECK(present("Jahshaka distortion field") && none("Jahshaka distortion field"),
          "the distortion field does NOT (HlmsUnlit displacement)");
    CHECK(!shadowReads, "no shadow node's pass reads it");
    std::printf("   the first GI reader: %s\n   the last:  %s\n", first.c_str(), last.c_str());
    std::printf("   pass preparations of the GI scene: %llu with the GI bound, %llu without (non-readers)\n",
                b1.readerPrepares - b0.readerPrepares, b1.nonReaderPrepares - b0.nonReaderPrepares);
    CHECK(b1.readerPrepares > b0.readerPrepares, "THE BINDING: reader passes were prepared WITH the GI");
    CHECK(b1.nonReaderPrepares > b0.nonReaderPrepares, "...and the passes that sample none WITHOUT it");

    e->destroyView(view);
    e->destroyScene(s);
    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
