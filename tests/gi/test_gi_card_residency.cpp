// gi.card_residency (PHOTON-7 stage 5, 5-2; SPECS/PHOTON_7_SPEC.md stage 5) - THE CARDS RESIDENT ARE THE ONES THE READERS
// READ, AND A WALK EQUALS A TELEPORT ON THEM.
//
// THE FIXTURE: two sealed 8 x 4 x 8 m rooms of carded walls (0.2 m slabs), 24 m apart, each with nine carded pillars and a
// lamp; their cards want ~416 pages of the atlas's 256 and the faces a ray inside one can reach ~137 (the inner faces and
// the pillars) - so one room fits, both do not. The chain at High, the rays on, THE GATHER ON (its hits are the readers;
// a 256 x 256 view), the field off. Run W: the camera settles in room A, WALKS to room B in 120 frames (through the walls:
// the gap between the rooms reads their outer faces) and settles; run T: settles in A and TELEPORTS to B. Both read the
// cache at B.
// THE LAWS:
//   (1) THE READ FACES ARE RESIDENT AND NO UNREACHABLE ONE IS: at B, every pillar of room B holds cards in both runs, and
//       no card of a wall's OUTER face (the face toward the outside of its room) was read within the window;
//   (2) THE ATLAS MADE ROOM: the walk's demand (both rooms and the outer faces of the gap) exceeds the atlas and evicted
//       the unread cards (the teleport's fits whole), and every card READ AT B AT LEAST lambda_min TIMES A FRAME
//       (GiQualityFacts::cardDemandHitFloor) in either run (the
//       measuring arm's counts over the 30 frames after the arrival) is resident in the other - walk = teleport on THE
//       RESIDENT SET (the spec's acceptance) down to the demand settle's law (GiQualityFacts::cardDemandSettleFrames:
//       a face read less may arrive in one run and not the other - counted, printed);
//   (3) THE VALUES, printed: every lit texel of every card resident in both runs against the other run's, in codes
//       (1 % of its radiance per channel over the store's floor) past one step of the R11G11B10F store. NOT YET A LAW:
//       the cascades' placements at one pose are path-dependent (the scroll's hysteresis: after the walk c1-c3 trail the
//       eye), and a cone ends at its cascade's box face (jah_voxel_march.glsl), so a card's marched indirect depends on
//       where the boxes stand until the cone march hands over at the world's shells (stage 5's cone-shell item, whose
//       commit makes this a law).
//   (5) A LATE FIRST READER KEEPS THE DEMAND OPEN FOR ITS OWN SETTLE (PHOTON-7 stage 5's read W3): a fourth run, the
//       teleport again with the test door cards.gatherLate = 8 (the gather's stamps within 8 frames of the arrival
//       dropped: it reads first 8 frames late, inside the settle's S = 10) - every card of the teleport's read set
//       (read at B at least lambda_min a frame) resident in it. RED under the readers' MOST clock (the demand closed S
//       frames after the change, before the late reader's first read-back: 106 of the 106 lost); the reflection never
//       reads here, so a least clock would never close it (the rule: each reader from its own first stamp).
//   (4) THE ATLAS COMPACTS: a third run teleports from a CROWDED room (a 7 x 3 x 7 grid of 0.8 m cubes: its read set
//       fills the atlas with quarter-page cards) to room B (whole pages): the arrivals find no free page and the atlas
//       merges its sub-cut pages (compaction moves > 0, each moved card re-captured), and B comes to rest carded.
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
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
// THE DEMAND'S WINDOW (OgreSurfaceCache.cpp's mDemandWindow): the settle's S, read from the facts
const unsigned long long kWindow = giQualityFacts(GiQuality::High, GiViewProfile::Desktop).cardDemandSettleFrames;

struct Room {
    Vec3 centre;
    std::vector<NodeId> pillars;
    struct Wall { NodeId node; unsigned outer; };   // the outer face's bucket
    std::vector<Wall> walls;
};

NodeId cube(Scene *s, const Vec3 &at, const Vec3 &size, const Colour &albedo) {
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 0.8f;
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}

Room buildRoom(Scene *s, const Vec3 &c, bool crowded = false) {
    Room r;
    r.centre = c;
    const float h = 4.0f, w = 8.0f, t = 0.2f;
    // the slabs: the floor (outer -y = bucket 3), the ceiling (+y = 2), the walls (+x 0, -x 1, +z 4, -z 5)
    r.walls.push_back({ cube(s, Vec3(c.x, -t * 0.5f, c.z), Vec3(w, t, w), Colour(0.6f, 0.6f, 0.6f)), 3u });
    r.walls.push_back({ cube(s, Vec3(c.x, h + t * 0.5f, c.z), Vec3(w, t, w), Colour(0.6f, 0.6f, 0.6f)), 2u });
    r.walls.push_back({ cube(s, Vec3(c.x + w * 0.5f + t * 0.5f, h * 0.5f, c.z), Vec3(t, h, w), Colour(0.7f, 0.4f, 0.3f)), 0u });
    r.walls.push_back({ cube(s, Vec3(c.x - w * 0.5f - t * 0.5f, h * 0.5f, c.z), Vec3(t, h, w), Colour(0.3f, 0.4f, 0.7f)), 1u });
    r.walls.push_back({ cube(s, Vec3(c.x, h * 0.5f, c.z + w * 0.5f + t * 0.5f), Vec3(w, h, t), Colour(0.4f, 0.7f, 0.3f)), 4u });
    r.walls.push_back({ cube(s, Vec3(c.x, h * 0.5f, c.z - w * 0.5f - t * 0.5f), Vec3(w, h, t), Colour(0.6f, 0.6f, 0.5f)), 5u });
    if (crowded) {
        // THE CROWDED ROOM (the compaction arm): a 7 x 3 x 7 grid of 0.8 m cubes - every card a 64-texel quarter page,
        // so the read set fills the atlas with sub-cut pages; leaving it frees their slots unread, scattered
        for (int i = -3; i <= 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = -3; k <= 3; ++k)
                    r.pillars.push_back(cube(s, Vec3(c.x + 1.1f * float(i), 0.7f + 1.2f * float(j), c.z + 1.1f * float(k)),
                                             Vec3(0.8f, 0.8f, 0.8f), Colour(0.4f + 0.05f * float(i + 3), 0.5f, 0.6f)));
    } else {
        for (int i = -1; i <= 1; ++i)
            for (int k = -1; k <= 1; ++k)
                r.pillars.push_back(cube(s, Vec3(c.x + 2.4f * float(i), 1.75f, c.z + 2.4f * float(k)),
                                         Vec3(0.4f, 3.5f, 0.4f), Colour(0.5f, 0.5f, 0.5f)));
    }
    const NodeId lamp = s->createNode();
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1.0f, 0.9f, 0.8f);
    l.intensity = 30.0f;
    l.range = 6.0f;
    s->setNodeTransform(lamp, Vec3(c.x + 1.2f, 3.0f, c.z + 1.2f), Quat(), Vec3(1, 1, 1));
    s->setLight(lamp, l);
    return r;
}

struct Run {
    std::vector<CardRadiance> cards;
    /// each card's READ RATE at B over the 30 frames after the arrival (hits a frame, the measuring arm's counts), by
    /// its node and card
    std::map<std::pair<NodeId, unsigned>, double> rate;
    unsigned long long frame = 0ull, newest = 0ull, evicted = 0ull, admitted = 0ull, compaction = 0ull;
    unsigned pagesUsed = 0u;
    int restA = -1, restB = -1;
    Room a, b;
};

bool runOnce(Engine *e, bool walk, Run &out, int &frames, bool crowded = false, int gatherLate = 0) {
    const char *name = crowded ? "residency_crowded" : walk ? "residency_walk" : gatherLate ? "residency_late"
                                                                                          : "residency_teleport";
    View *view = e->createOffscreenView(name, 256, 256, Colour(0, 0, 0));
    Scene *scene = e->createScene(name);
    if (!view || !scene) return false;
    view->setScene(scene);
    {
        PostFxDesc fx;   // the chain, so the gather runs (test_gi_gather_findings.cpp's armChain)
        fx.allowOffscreen = true;
        fx.ssr = 1;
        view->setPostFx(fx);
    }
    view->setOffscreenContract(OffscreenContract::Live);
    out.a = buildRoom(scene, Vec3(-12.0f, 0.0f, 0.0f), crowded);
    out.b = buildRoom(scene, Vec3(12.0f, 0.0f, 0.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    const auto look = [&](float x) {
        enginetest::testCameraLookAt(view, Vec3(x, 1.7f, 2.5f), Vec3(x + 0.5f, 1.5f, -2.0f));
    };
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
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
        if (quiet < 90) {
            const GiStatus g = scene->published().gi;
            std::printf("   NOT AT REST: chainOwed %d feedbackOwed %d built %d relights %llu admitted %llu queue %u "
                        "relitLastFrame %u indirectLastFrame %u reported %llu byRegion %llu overflows %llu\n",
                        int(g.chainOwed), int(g.feedbackOwed), int(g.cards.built), g.cards.relights, g.cards.admitted,
                        g.cards.queueLength, g.cards.relitLastFrame, g.cards.indirectLastFrame, g.cards.indirectReported,
                        g.cards.indirectByRegion, g.cards.indirectOverflows);
            for (size_t k = 0; k < g.cascades.size(); ++k) {
                const auto &c = g.cascades[k];
                std::printf("     c%zu centre (%.2f %.2f %.2f) half %.1f pending %d scrolling %d rebuilds %llu scrolls %llu "
                            "fbActive %u fbOwed %d passes %llu ran %llu stops %llu resumes %llu sat %llu carried %llu "
                            "relightCarried %llu\n", k, double(c.centre.x), double(c.centre.y), double(c.centre.z),
                            double(c.halfSize), c.pending, int(c.scrolling), c.rebuilds, c.scrolls, c.feedbackActive,
                            int(c.feedbackOwed), c.feedbackPasses, c.feedbackBricksRan, c.feedbackStops,
                            c.feedbackResumes, c.feedbackSaturated, c.feedbackCarried, c.relightCarried);
            }
        }
        return quiet >= 90 ? f - quiet : -1;
    };
    look(-12.0f);
    if (!scene->setGlobalIllumination(gi)) return false;
    e->waitForAsyncShaders();
    out.restA = settle();
    if (gatherLate) e->setArm("cards.gatherLate", double(gatherLate));   // (5): the gather reads first N frames late
    if (walk) {
        for (int f = 1; f <= 120; ++f) {
            look(-12.0f + 24.0f * float(f) / 120.0f);
            run(1);
        }
    } else {
        look(12.0f);
    }
    // THE READ RATES AT B (the settle law's own quantity: GiQualityFacts::cardDemandSettleFrames catches every face
    // read at least once a frame): the measuring arm's hit counts over the 30 frames after the arrival, while the readers
    // run (they rest once the view holds)
    {
        std::vector<CardRadiance> c0, c1;
        unsigned long long f0 = 0ull, f1 = 0ull;
        scene->readCardRadiance(c0, f0);
        run(30);
        scene->readCardRadiance(c1, f1);
        std::map<std::pair<NodeId, unsigned>, unsigned long long> h0;
        for (const CardRadiance &k : c0) h0[{ k.node, k.card }] = k.hits;
        for (const CardRadiance &k : c1) {
            auto it = h0.find({ k.node, k.card });
            const unsigned long long base = it != h0.end() && k.hits >= it->second ? it->second : 0ull;
            out.rate[{ k.node, k.card }] = double(k.hits - base) / 30.0;
        }
    }
    out.restB = settle();
    if (gatherLate) e->setArm("cards.gatherLate", 0.0);
    const GiStatus g = scene->published().gi;
    out.evicted = g.cards.evicted;
    out.admitted = g.cards.admitted;
    out.compaction = g.cards.compactionMoves;
    out.pagesUsed = g.cards.pagesUsed;
    const bool ok = scene->readCardRadiance(out.cards, out.frame);
    for (const CardRadiance &c : out.cards) out.newest = std::max(out.newest, c.lastRead);
    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    e->destroyView(view);
    e->destroyScene(scene);
    run(2);
    return ok;
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-card-residency-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    e->setArm("cards.demandCount", 1.0);   // the readers' hits counted: the read rates (2) reads
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));

    Run w, t, c, l;
    const bool okW = runOnce(e, true, w, frames);
    const bool okT = runOnce(e, false, t, frames);
    const bool okC = runOnce(e, false, c, frames, true);
    const bool okL = runOnce(e, false, l, frames, false, 8);
    std::printf("   WALK:     at rest in A %d frames, in B %d | %zu cards resident, %u pages | admitted %llu, evicted %llu, "
                "compaction moves %llu\n", w.restA, w.restB, w.cards.size(), w.pagesUsed, w.admitted, w.evicted,
                w.compaction);
    std::printf("   TELEPORT: at rest in A %d frames, in B %d | %zu cards resident, %u pages | admitted %llu, evicted %llu, "
                "compaction moves %llu\n", t.restA, t.restB, t.cards.size(), t.pagesUsed, t.admitted, t.evicted,
                t.compaction);
    CHECK(okW && okT && w.restA >= 0 && w.restB >= 0 && t.restA >= 0 && t.restB >= 0,
          "both runs come to rest in A and in B and their cards read back");

    // the nodes are the scene's own: the two runs' scenes number them alike (the same build order) - matched by the
    // build's own lists, position by position
    const auto key = [](const Run &r) {
        std::map<NodeId, int> id;   // node -> a run-independent index
        int n = 0;
        for (const Room *room : { &r.a, &r.b }) {
            for (const Room::Wall &wl : room->walls) id[wl.node] = n++;
            for (NodeId p : room->pillars) id[p] = n++;
        }
        return id;
    };
    const std::map<NodeId, int> idW = key(w), idT = key(t);
    const auto readRecently = [](const Run &r, const CardRadiance &c) { return c.lastRead + kWindow >= r.newest; };

    // (1) the pillars of B carded, no outer face read
    size_t pillarsW = 0, pillarsT = 0, outerRead = 0;
    for (const Run *r : { &w, &t }) {
        size_t &pillars = r == &w ? pillarsW : pillarsT;
        for (NodeId p : r->b.pillars)
            if (std::any_of(r->cards.begin(), r->cards.end(), [p](const CardRadiance &c) { return c.node == p; })) ++pillars;
        for (const CardRadiance &c : r->cards)
            for (const Room *room : { &r->a, &r->b })
                for (const Room::Wall &wl : room->walls)
                    if (c.node == wl.node && c.bucket == wl.outer && readRecently(*r, c)) ++outerRead;
    }
    std::printf("   room B's pillars carded: walk %zu of 9, teleport %zu of 9 | outer-face cards read within the window: "
                "%zu\n", pillarsW, pillarsT, outerRead);
    CHECK(pillarsW == 9u && pillarsT == 9u && outerRead == 0u,
          "(1) THE READ FACES ARE RESIDENT (every pillar of B carded) AND NO OUTER FACE WAS READ");

    // (2) the read set resident in both: every card read at B at least once a frame (the settle law's floor) in either
    //     run is resident in the other
    std::map<std::pair<int, unsigned>, const CardRadiance *> inW, inT;
    for (const CardRadiance &c : w.cards) if (idW.count(c.node)) inW[{ idW.at(c.node), c.card }] = &c;
    for (const CardRadiance &c : t.cards) if (idT.count(c.node)) inT[{ idT.at(c.node), c.card }] = &c;
    const auto rateOf = [](const Run &r, const CardRadiance &c) {
        auto it = r.rate.find({ c.node, c.card });
        return it != r.rate.end() ? it->second : 0.0;
    };
    // THE FLOOR: the settle law's lambda_min (GiQualityFacts::cardDemandHitFloor), the one constant
    const double floor = double(giQualityFacts(GiQuality::High, GiViewProfile::Desktop).cardDemandHitFloor);
    size_t readW = 0, readT = 0, missW = 0, missT = 0, bandW = 0, bandT = 0;
    const auto side = [&](const Run &r, const Run &other, const std::map<std::pair<int, unsigned>, const CardRadiance *> &mine,
                          const std::map<std::pair<int, unsigned>, const CardRadiance *> &theirs, const char *name,
                          size_t &read, size_t &miss, size_t &band) {
        size_t unreadOnly = 0;
        for (const auto &kv : mine) {
            const double rt = rateOf(r, *kv.second);
            const bool there = theirs.count(kv.first) != 0u;
            if (rt >= floor) { ++read; miss += there ? 0u : 1u; }
            if (rt >= 0.5 * floor && rt < floor) ++band;
            if (!there && !(rt > 0.0)) ++unreadOnly;
            else if (!there)   // resident in this run only and read at B: printed with its measured rate
                std::printf("   %s only: id %d card %u (face %u) read %.3f hits a frame at B%s\n", name, kv.first.first,
                            kv.first.second, kv.second->bucket, rt,
                            rt >= floor ? " - AT THE FLOOR OR MORE" : rt >= 0.5 * floor ? " (the band floor/2 .. floor)" : "");
        }
        if (unreadOnly)
            std::printf("   %s only, not read at B at all: %zu cards (the path's: the first room's, the gap's outer faces)\n",
                        name, unreadOnly);
        (void)other;
    };
    side(w, t, inW, inT, "walk", readW, missW, bandW);
    side(t, w, inT, inW, "teleport", readT, missT, bandT);
    std::printf("   the read set (read at B at least %.2f hits a frame, the settle law's floor): walk %zu cards (%zu not "
                "resident in the teleport), teleport %zu (%zu not resident in the walk) | in the band floor/2 .. floor "
                "(the law leaves to chance): walk %zu, teleport %zu\n", floor, readW, missW, readT, missT, bandW, bandT);
    CHECK(w.evicted > 0u && readW > 0u && readT > 0u && missW == 0u && missT == 0u,
          "(2) THE ATLAS MADE ROOM (the walk's unread cards evicted) AND THE READ SET IS RESIDENT IN BOTH");

    // (5) THE LATE FIRST READER: the teleport's read set resident in the late run
    {
        const std::map<NodeId, int> idL = key(l);
        std::map<std::pair<int, unsigned>, const CardRadiance *> inL;
        for (const CardRadiance &k : l.cards) if (idL.count(k.node)) inL[{ idL.at(k.node), k.card }] = &k;
        size_t readSet = 0, missL = 0;
        for (const auto &kv : inT)
            if (rateOf(t, *kv.second) >= floor) { ++readSet; missL += inL.count(kv.first) ? 0u : 1u; }
        std::printf("   (5) the gather first reading 8 frames late: at rest in B %d frames, %zu cards resident, admitted "
                    "%llu | the teleport's read set %zu cards, %zu not resident in the late run\n", l.restB, l.cards.size(),
                    l.admitted, readSet, missL);
        CHECK(okL && l.restB >= 0 && readSet > 0u && missL == 0u,
              "(5) A LATE FIRST READER KEEPS THE DEMAND OPEN FOR ITS OWN SETTLE: the read set resident");
    }

    // (3) the values
    size_t common = 0, over = 0;
    double worst = 0.0;
    for (const auto &kv : inW) {
        auto it = inT.find(kv.first);
        if (it == inT.end() || it->second->size != kv.second->size) continue;
        ++common;
        const CardRadiance &a = *kv.second, &b = *it->second;
        for (size_t k = 0; k < a.rgb.size(); ++k) {
            const double o = std::max(double(b.rgb[k]), 5.9604645e-6);
            if (!(a.rgb[k] > 0.0f) && !(b.rgb[k] > 0.0f)) continue;
            const double step = std::ldexp(o, (k % 3u) == 2u ? -5 : -6);
            const double codes = std::max(0.0, std::fabs(double(a.rgb[k]) - double(b.rgb[k])) - step) / (0.01 * o);
            worst = std::max(worst, codes);
            over += codes > 1.0 ? 1u : 0u;
        }
    }
    std::printf("   (3) the values, %zu cards resident in both: worst %.3f codes, %zu texels over a code (the cone-shell "
                "commit's law)\n", common, worst, over);
    CHECK(common > 0u, "(3) the cards resident in both runs compared");

    // (4) THE COMPACTION: from the crowded room (its read set in quarter pages) to room B (whole pages): the arrivals find
    //     no free page, the evictions free scattered quarter-page slots, and the atlas merges the sub-cut pages to free
    //     whole ones - every card read at B resident, every moved card re-captured (none queued at the rest)
    size_t readC = 0, unreadAtB = 0;
    for (const CardRadiance &k : c.cards) if (readRecently(c, k)) ++readC;
    for (NodeId p : c.b.pillars)
        if (!std::any_of(c.cards.begin(), c.cards.end(), [p](const CardRadiance &k) { return k.node == p; })) ++unreadAtB;
    std::printf("   CROWDED -> B: at rest in A %d frames, in B %d | %zu cards resident, %u pages | admitted %llu, evicted %llu, "
                "compaction moves %llu | %zu read within the window, %zu of B's pillars uncarded\n", c.restA, c.restB,
                c.cards.size(), c.pagesUsed, c.admitted, c.evicted, c.compaction, readC, unreadAtB);
    CHECK(okC && c.restA >= 0 && c.restB >= 0 && c.compaction > 0u && c.evicted > 0u && unreadAtB == 0u,
          "(4) THE ATLAS COMPACTS: the crowded room's quarter pages merged to free whole ones, and room B carded at rest");

    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
