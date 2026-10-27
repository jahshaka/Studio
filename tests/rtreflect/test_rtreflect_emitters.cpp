// rtreflect.emitters — THE EMITTER TABLE (PHOTON-7 stage 11, 11-1a): the instances the reflection's next-event
// estimation aims at, written by RayQueryTier::updateEmitterTable beside the hit table (its u[5] / u[6] section).
//
// THE LAWS (each read off RayQueryStatus, which publishes the table exactly as it was uploaded):
//   (a) AN EMITTER IS A TRACED INSTANCE WHOSE MATERIAL ROW EMITS - any channel of its emissive above zero (a light at
//       any radiance: the floor is zero). Seven emissive pillars beside a metal floor and a matte box: SEVEN targets,
//       none over the cap; each target's radiance is its row's emissive bit for bit, its sphere the instance's world
//       box's (centre the box's, radius its half diagonal), and the table is ORDERED BY SLOT (strictly ascending).
//   (b) A MOVE IS NO NEW SET: one pillar moved 2 m - its target's centre follows, the SET's serial (what folds into the
//       reflection's rest key) does not move (a moving emitter is a mover: the rest follows it per texel).
//   (c) THE NEGATIVE CONTROL: the pillars' emissive set to black - ZERO targets, and the set's serial moved.
//   (d) THE CAP (GiQualityFacts::reflectEmitters, a count): kCapPlus more emitters than the tier's cap - the cap's
//       count kept, the rest COUNTED (reflectEmittersOver = kCapPlus, never silent), and the ones left out the dimmest
//       by power (the radiance x the sphere's cross-section; equal spheres here, so by radiance).
// FIXTURE: High, rays on, 256 x 144, an offscreen live view; frames, never time.

#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
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

static const unsigned kWidth = 256;
static const unsigned kHeight = 144;
/// (d): this many emitters past the tier's cap.
static const unsigned kCapPlus = 6u;

static NodeId addBox(Scene *s, MaterialId m, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()), m);
    enginetest::setNodeScale(s, n, scale);
    enginetest::setNodePosition(s, n, pos);
    return n;
}

static void rayTier(Scene *s, View *view)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    gi.testProbeRegionMin = Vec3(-30.0f, -2.0f, -30.0f);
    gi.testProbeRegionMax = Vec3(30.0f, 10.0f, 30.0f);
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.reflectSources = ReflectSources::Rays;
    view->setPostFx(fx);
}

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-rtreflect-emitters-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("emitters", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f),
                                        jahshaka::engine::OffscreenContract::Live);
    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: no ray queries on this machine - the row is about the ray tier\n");
        return 0;
    }
    Scene *s = e->createScene("emitters");
    view->setScene(s);
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
    PbrParams floorP;
    floorP.albedo = Colour(0.85f, 0.85f, 0.85f);
    floorP.metalness = 1.0f;
    floorP.roughness = 0.2f;
    addBox(s, s->createPbrMaterial(floorP), Vec3(0.0f, -0.1f, 0.0f), Vec3(40.0f, 0.2f, 40.0f));
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    addBox(s, s->createPbrMaterial(matte), Vec3(0.0f, 0.5f, 3.0f), Vec3(1.0f, 1.0f, 1.0f));
    const Colour cols[7] = { Colour(1.0f, 0.066f, 0.066f), Colour(0.066f, 1.0f, 0.066f),
                             Colour(0.066f, 0.133f, 1.0f), Colour(1.0f, 0.8f, 0.066f),
                             Colour(1.0f, 0.066f, 1.0f),   Colour(0.066f, 0.933f, 0.933f),
                             Colour(4.0f, 4.0f, 4.0f) };   // (an HDR emitter: the row is unclamped fp32)
    MaterialId pillarMat[7];
    NodeId pillar[7];
    PbrParams pillarP[7];
    for (int i = 0; i < 7; ++i) {
        pillarP[i].albedo = Colour(0.05f, 0.05f, 0.05f);
        pillarP[i].emissive = cols[i];
        pillarP[i].roughness = 0.9f;
        pillarMat[i] = s->createPbrMaterial(pillarP[i]);
        pillar[i] = addBox(s, pillarMat[i], Vec3(-12.0f + 4.0f * float(i), 1.5f, -2.0f), Vec3(1.0f, 3.0f, 1.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
    rayTier(s, view);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 5.0f, 14.0f), Vec3(0.0f, 1.0f, 0.0f));
    enginetest::frames(e, 30);

    // (a)
    {
        const RayQueryStatus rq = s->published().rayQuery;
        std::printf("-- (a) the table: %d targets, %u over the cap, set serial %llu\n", rq.reflectEmitters,
                    rq.reflectEmittersOver, rq.reflectEmitterSetSerial);
        for (const auto &t : rq.reflectEmitterTable)
            std::printf("   slot %u centre (%.3f, %.3f, %.3f) radius %.4f radiance (%.3f, %.3f, %.3f)\n", t.slot,
                        t.centre[0], t.centre[1], t.centre[2], t.radius, t.radiance[0], t.radiance[1], t.radiance[2]);
        CHECK_MSG(rq.reflectEmitters == 7 && rq.reflectEmitterTable.size() == 7u,
                  "(a) seven emissive pillars are seven targets (the metal floor and the matte box none): %d",
                  rq.reflectEmitters);
        CHECK_MSG(rq.reflectEmittersOver == 0u, "(a) none over the cap (%u)", rq.reflectEmittersOver);
        bool ordered = true;
        for (size_t i = 1; i < rq.reflectEmitterTable.size(); ++i)
            ordered = ordered && rq.reflectEmitterTable[i - 1].slot < rq.reflectEmitterTable[i].slot;
        CHECK_MSG(ordered, "(a) the table is ordered by slot (strictly ascending)");
        // each pillar's target: matched by its world centre (x = -12 + 4i), its radiance its row's bit for bit
        const float halfDiag = 0.5f * std::sqrt(1.0f + 9.0f + 1.0f);
        int matched = 0;
        for (int i = 0; i < 7; ++i)
            for (const auto &t : rq.reflectEmitterTable)
                if (near(t.centre[0], -12.0f + 4.0f * float(i), 1e-3f) && near(t.centre[1], 1.5f, 1e-3f) &&
                    near(t.centre[2], -2.0f, 1e-3f) && near(t.radius, halfDiag, 1e-3f) &&
                    t.radiance[0] == cols[i].r && t.radiance[1] == cols[i].g && t.radiance[2] == cols[i].b)
                    ++matched;
        CHECK_MSG(matched == 7, "(a) every pillar's target: its world box's sphere (radius %.4f) and its row's "
                  "radiance bit for bit (%d of 7)", halfDiag, matched);
    }

    // (b)
    unsigned long long serialB = 0ull;
    {
        const unsigned long long before = s->published().rayQuery.reflectEmitterSetSerial;
        enginetest::setNodePosition(s, pillar[3], Vec3(2.0f, 1.5f, -2.0f));
        enginetest::frames(e, 6);
        const RayQueryStatus rq = s->published().rayQuery;
        bool followed = false;
        for (const auto &t : rq.reflectEmitterTable)
            if (near(t.centre[0], 2.0f, 1e-3f) && t.radiance[0] == cols[3].r) followed = true;
        CHECK_MSG(followed, "(b) the moved pillar's target follows it (centre x 0 -> 2)");
        CHECK_MSG(rq.reflectEmitterSetSerial == before, "(b) a move is no new set: the set's serial %llu -> %llu",
                  before, rq.reflectEmitterSetSerial);
        serialB = rq.reflectEmitterSetSerial;
    }

    // (c)
    {
        for (int i = 0; i < 7; ++i) {
            PbrParams p = pillarP[i];
            p.emissive = Colour(0.0f, 0.0f, 0.0f);
            s->setPbrMaterial(pillarMat[i], p);
        }
        enginetest::frames(e, 10);
        const RayQueryStatus rq = s->published().rayQuery;
        CHECK_MSG(rq.reflectEmitters == 0 && rq.reflectEmitterTable.empty(),
                  "(c) THE NEGATIVE CONTROL: the pillars dark - zero targets (%d)", rq.reflectEmitters);
        CHECK_MSG(rq.reflectEmitterSetSerial != serialB, "(c) ...and the set's serial moved (%llu -> %llu)", serialB,
                  rq.reflectEmitterSetSerial);
    }

    // (d)
    {
        const unsigned cap = giQualityFacts(GiQuality::High).reflectEmitters;
        const unsigned n = cap + kCapPlus;
        for (unsigned i = 0; i < n; ++i) {
            PbrParams p;
            p.albedo = Colour(0.05f, 0.05f, 0.05f);
            p.emissive = Colour(0.01f * float(i + 1), 0.0f, 0.0f);   // the i-th the i-th dimmest
            addBox(s, s->createPbrMaterial(p), Vec3(-15.0f + 0.4f * float(i % 75), 0.1f, 8.0f + 0.4f * float(i / 75)),
                   Vec3(0.2f, 0.2f, 0.2f));
        }
        enginetest::frames(e, 10);
        const RayQueryStatus rq = s->published().rayQuery;
        float dimmestKept = 1e9f;
        for (const auto &t : rq.reflectEmitterTable) dimmestKept = std::min(dimmestKept, t.radiance[0]);
        std::printf("-- (d) %u emitters against the cap %u: %d kept, %u over, the dimmest kept %.3f\n", n, cap,
                    rq.reflectEmitters, rq.reflectEmittersOver, dimmestKept);
        CHECK_MSG(rq.reflectEmitters == int(cap) && rq.reflectEmittersOver == kCapPlus,
                  "(d) THE CAP: %u kept, %u counted over (%d, %u)", cap, kCapPlus, rq.reflectEmitters,
                  rq.reflectEmittersOver);
        CHECK_MSG(near(dimmestKept, 0.01f * float(kCapPlus + 1), 1e-5f),
                  "(d) the ones left out are the dimmest by power (the dimmest kept %.3f, the %u-th)", dimmestKept,
                  kCapPlus + 1);
    }

    e->destroyScene(s);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
