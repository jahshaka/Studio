// gi.card_oracle (PHOTON-7 stage 5, 5-1; SPECS/PHOTON_7_SPEC.md stage 5's oracle) - THE CARDS REFINE ONLY WHAT CHANGED,
// AND WHAT THEY KEEP IS STILL RIGHT: a lamp step on a carded court, checked against an ORACLE - a whole re-solve of the
// same final state (the GI torn down and built again: every voxel injected and converged from scratch, every card
// captured and relit fresh).
//
// THE FIXTURE: a carded 12 m floor, four carded props, the sun, and a lamp of range 3 m between the first two props; the
// fourth prop stands 8 m from the lamp, outside its range; a FIFTH, a 6 m emissive (200) cube floating 40 m away, is out
// of the step's reach - its cards' bound stays under a code (its code is 2: the light it gathers cannot move one; the
// bound is a bound, so a nearer prop the change cannot move a code is still listed - the bound's slack, which the row
// prints as the truth's 'relit anyway'). The
// chain at High, the rays on (the cards exist only there) and THE GATHER ON - its hits are the cards' readers, and the
// cards resident are the ones they read (PHOTON-7 stage 5, 5-2); the field off. The cards compared are the ones
// resident in both readings.
// THE STEP: the lamp 4 -> 8 at the scene's rest, run to the rest again (the chain owes nothing and the cache relights
// nothing for 30 frames).
// THE LAWS:
//   (1) EVERY CARD MATCHES THE ORACLE: every lit texel of every resident card within ONE CODE of the oracle's (1 % of the
//       oracle's radiance per channel over the store's floor) PLUS ONE STEP OF THE RADIANCE STORE (RGBA16F since PHOTON-7
//       stage 10's 10-2d, rounding to nearest with 10 mantissa bits: two values a code apart can store a step further
//       apart - a step is at most 2^-10 of the value; the row's own quantum, R3) - the cards the step relit (their direct half: the
//       lamp's sphere; their indirect: the light they gather moved a code) and the cards it did not (what they kept is
//       still the answer: the stale set missed nothing that moved a code) alike;
//   (2) THE STEP RELIT THE CARDS IN THE LAMP'S RANGE (the direct half's region, exact by the range) AND KEPT THE FAR
//       EMISSIVE PROP'S (the stale set is a set: what the change cannot reach is not relit - and (1) proves it right).
//       RED where the direct half's region drops the lamp's range (every card's direct staled by the step: measured
//       once, PHOTON-7 stage 5's read W1 - the emissive prop relit). (The old (3), "the far prop's direct half was not
//       relit", is deleted: the far prop's cards are re-marched by their indirect after the step, a march recomputes
//       both halves, so with the range dropped it stayed green - it could not fail.)
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
NodeId addCardedCube(Scene *s, const Colour &albedo, float roughness)
{
    const NodeId node = s->createNode();
    if (!node) return 0;
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    PbrParams p;
    p.albedo = albedo;
    p.roughness = roughness;
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    return node;
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-card-oracle-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);   // the cards exist where the rays run
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("card_oracle", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("card_oracle");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    // THE CHAIN, so the gather runs (its hits read the cards): an offscreen view takes the post chain only when it
    // allows it, and the gather rides the chain's prepass (test_gi_gather_findings.cpp's armChain).
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        view->setPostFx(fx);
    }
    // A STILL PICTURE GATHERS (View::setOffscreenContract; PHOTON-7 7-5: the gather is recorded only for a view whose
    // chain gathers, and a Live view takes the field's answer)

    const NodeId floor = addCardedCube(scene, Colour(0.7f, 0.7f, 0.7f), 0.9f);
    enginetest::setNodePosition(scene, floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, floor, Vec3(12.0f, 0.1f, 12.0f));
    const Vec3 propAt[4] = { Vec3(-1.0f, 0.5f, 0.0f), Vec3(1.0f, 0.5f, 0.0f), Vec3(0.0f, 0.5f, -2.0f), Vec3(0.0f, 0.5f, 8.0f) };
    NodeId props[4] = {};
    for (int k = 0; k < 4; ++k) {
        props[k] = addCardedCube(scene, Colour(0.3f + 0.15f * k, 0.5f, 0.8f - 0.15f * k), 0.7f);
        enginetest::setNodePosition(scene, props[k], propAt[k]);
    }
    NodeId far = 0;
    {
        far = scene->createNode();
        MeshData d = enginetest::unitCubeMesh();
        d.cards = enginetest::boxCards(0.5f);
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        p.roughness = 0.8f;
        p.emissive = Colour(200.0f, 200.0f, 200.0f);
        const MeshId mesh = scene->createMesh(d);
        const MaterialId mat = scene->createPbrMaterial(p);
        if (!far || !mesh || !mat || !scene->attachMesh(far, mesh, mat)) { std::printf("FAIL: the far prop\n"); return 1; }
        enginetest::poseRegistry()[scene][far] = enginetest::NodePose{};
        enginetest::setNodePosition(scene, far, Vec3(0.0f, 3.0f, -40.0f));
        enginetest::setNodeScale(scene, far, Vec3(6.0f, 6.0f, 6.0f));
    }
    enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.4f), 2.0f);
    const NodeId lamp = scene->createNode();
    LightDesc lampDesc;
    lampDesc.type = LightType::Point;
    lampDesc.colour = Colour(1.0f, 0.9f, 0.8f);
    lampDesc.intensity = 4.0f;
    lampDesc.range = 3.0f;
    const Vec3 lampPos(0.0f, 1.2f, 0.0f);
    scene->setNodeTransform(lamp, lampPos, Quat(), Vec3(1, 1, 1));
    scene->setLight(lamp, lampDesc);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.5f, 0.0f));

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    // THE REST: the chain owes nothing, and the cache admitted, captured and relit nothing, for 90 frames in a row - past
    // the readers' window (60) and its read-back's lag, so the resident set the reads ask for has arrived (cap 1500)
    const auto settle = [&]() {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 1500 && quiet < 90; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool still = !g.chainOwed && !g.feedbackOwed && g.cards.built && g.cards.relights == relights &&
                               g.cards.admitted == admitted && g.cards.queueLength == 0u;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        return quiet >= 90 ? f - quiet : -1;
    };
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    e->setArm("cards.demandCount", 1.0);
    const int rest0 = settle();
    {
        const GiStatus g = scene->published().gi;
        std::printf("   the readers: the gather running %d (%llu rays a frame) | the table %u slots, %u cards | the gather's "
                    "hits carded %llu / uncarded %llu (the reflection's %llu / %llu) | admitted %llu\n",
                    int(g.gather.running), (unsigned long long)g.gather.raysPerFrame, g.cards.instanceSlots, g.cards.cardRecords,
                    g.cards.demandHits[2], g.cards.demandHits[3], g.cards.demandHits[0], g.cards.demandHits[1], g.cards.admitted);
    }
    std::printf("   the court at rest in %d frames (%u cards resident)\n", rest0, scene->published().gi.cards.cardsResident);
    CHECK(rest0 >= 0, "the court comes to rest");
    std::vector<CardRadiance> before;
    unsigned long long stepFrame = 0ull;
    CHECK(scene->readCardRadiance(before, stepFrame) && !before.empty(), "the cards read back before the step");

    // THE STEP
    lampDesc.intensity = 8.0f;
    scene->setLight(lamp, lampDesc);
    const int rest1 = settle();
    std::vector<CardRadiance> walked;
    unsigned long long walkedFrame = 0ull;
    CHECK(rest1 >= 0 && scene->readCardRadiance(walked, walkedFrame) && !walked.empty(),
          "the step comes to rest and the cards read back");

    // THE ORACLE: the same state solved whole
    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    scene->setGlobalIllumination(gi);
    e->waitForAsyncShaders();
    const int rest2 = settle();
    std::vector<CardRadiance> oracle;
    unsigned long long oracleFrame = 0ull;
    CHECK(rest2 >= 0 && scene->readCardRadiance(oracle, oracleFrame) && !oracle.empty(),
          "the oracle comes to rest and its cards read back");

    // (1) EVERY CARD AGAINST THE ORACLE, by node and card
    std::map<std::pair<NodeId, unsigned>, const CardRadiance *> byKey;
    for (const CardRadiance &c : oracle) byKey[{ c.node, c.card }] = &c;
    size_t cards = 0, relit = 0, kept = 0, missing = 0, overRelit = 0, overKept = 0;
    double worstRelit = 0.0, worstKept = 0.0;
    size_t lampRelit = 0, lampCards = 0, emCards = 0, emRelit = 0;
    std::map<std::pair<NodeId, unsigned>, const CardRadiance *> beforeKey;
    for (const CardRadiance &c : before) beforeKey[{ c.node, c.card }] = &c;
    size_t truthMoved = 0, truthStill = 0, stillRelit = 0;
    for (const CardRadiance &w : walked) {
        auto it = byKey.find({ w.node, w.card });
        if (it == byKey.end() || it->second->size != w.size) { ++missing; continue; }
        const CardRadiance &o = *it->second;
        ++cards;
        const bool wasRelit = w.lastRelit > stepFrame || w.lastIndirect > stepFrame;
        double worst = 0.0;
        size_t over = 0;
        for (size_t t = 0; t < w.rgb.size(); t += 3) {
            bool lit = false;
            for (int k = 0; k < 3; ++k) lit = lit || o.rgb[t + k] > 0.0f || w.rgb[t + k] > 0.0f;
            if (!lit) continue;
            double rel = 0.0;
            for (int k = 0; k < 3; ++k) {
                const double code = 0.01 * std::max(double(o.rgb[t + k]), 5.9604645e-6);
                const double step = std::ldexp(std::max(double(o.rgb[t + k]), 5.9604645e-6), -10);   // RGBA16F (10-2d)
                // in codes, the store's step taken off (a texel one step apart from the oracle beyond a code fails)
                rel = std::max(rel, std::max(0.0, std::fabs(double(w.rgb[t + k]) - double(o.rgb[t + k])) - step) / code);
            }
            worst = std::max(worst, rel);
            over += rel > 1.0 ? 1u : 0u;
        }
        if (over) std::printf("   OFFENDER node %llu card %u size %u worst %.3f over %zu relit %llu/%llu (step %llu)\n",
                              (unsigned long long)w.node, w.card, w.size, worst, over, w.lastRelit, w.lastIndirect, stepFrame);
        if (wasRelit) { ++relit; worstRelit = std::max(worstRelit, worst); overRelit += over; }
        else { ++kept; worstKept = std::max(worstKept, worst); overKept += over; }
        if (w.node == far) { ++emCards; emRelit += wasRelit ? 1u : 0u; }
        // THE TRUTH: did the card's light move a code from before the step to the oracle?
        auto b = beforeKey.find({ w.node, w.card });
        if (b != beforeKey.end() && b->second->size == o.size) {
            double moved = 0.0;
            for (size_t t = 0; t < o.rgb.size(); ++t) {
                const double code = 0.01 * std::max(double(o.rgb[t]), 5.9604645e-6);
                moved = std::max(moved, std::fabs(double(b->second->rgb[t]) - double(o.rgb[t])) / code);
            }
            if (moved > 1.0) ++truthMoved;
            else { ++truthStill; stillRelit += wasRelit ? 1u : 0u; }
        }
        if (w.node == props[0] || w.node == props[1]) { ++lampCards; lampRelit += wasRelit ? 1u : 0u; }
    }
    std::printf("   %zu cards resident in both readings (%zu of the step's not resident in the oracle's): RELIT by the step %zu (worst %.3f codes, %zu texels over a "
                "code), KEPT %zu (worst %.3f codes, %zu texels over a code); the lamp's props %zu of %zu relit\n", cards,
                missing, relit, worstRelit, overRelit, kept, worstKept, overKept, lampRelit, lampCards);
    CHECK(cards > 0 && overRelit == 0 && overKept == 0,
          "(1) EVERY CARD MATCHES THE ORACLE WITHIN ONE CODE - the relit and the kept alike");
    std::printf("   the truth (before the step against the oracle): %zu cards moved over a code, %zu did not - of those %zu were "
                "relit anyway (the bound's slack); the far emissive prop %zu of %zu relit\n", truthMoved, truthStill,
                stillRelit, emRelit, emCards);
    CHECK(lampCards > 0 && lampRelit == lampCards && emCards > 0 && emRelit == 0,
          "(2) THE STEP RELIT THE CARDS IN THE LAMP'S RANGE AND KEPT THE FAR EMISSIVE PROP'S (a stale set, not a sweep)");

    scene->setGlobalIllumination(off);
    run(2);
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
