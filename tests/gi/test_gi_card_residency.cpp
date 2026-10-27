// gi.card_residency (PHOTON-7 stage 5, 5-2; SPECS/PHOTON_7_SPEC.md stage 5) - THE CARDS RESIDENT ARE THE ONES THE READERS
// READ, AND A WALK EQUALS A TELEPORT ON THEM.
//
// THE FIXTURE: two sealed 8 x 4 x 8 m rooms of carded walls (0.2 m slabs), 24 m apart, each with nine carded pillars and a
// lamp; their cards want ~416 pages of the atlas's 256 and the faces a ray inside one can reach ~137 (the inner faces and
// the pillars) - so one room fits, both do not. The chain at High, the rays on, THE GATHER ON (its hits are the readers;
// a 256 x 256 view), the field off. Run W: the camera settles in room A, WALKS to room B in 60 frames (through the walls:
// the gap between the rooms reads their outer faces) and settles; run T: the TELEPORT WITH NO PAST, built and resting at
// B (it settled in A first before the rows' split: no frame of A is the law's). Both read the cache at B.
// THE LAWS:
//   (1) THE READ FACES ARE RESIDENT AND NO UNREACHABLE ONE IS: at B, every pillar of room B holds cards in both runs, and
//       no card of a wall's OUTER face (the face toward the outside of its room) was read within the window;
//   (2) THE ATLAS MADE ROOM: the walk's demand (both rooms and the outer faces of the gap) exceeds the atlas and evicted
//       the unread cards (the teleport's fits whole), and every FACE READ IN THE READERS' REST-16 AT B AT LEAST
//       lambda_min TIMES A FRAME (GiQualityFacts::cardDemandHitFloor) in either run (the measuring arm's per-face counts
//       from the gather's first rest frame to its 17th - every read of the face, its card's or the hit that marked it:
//       Scene::readCardFaceDemand; RESTATED WITH R3, the lead's ruling: the rest the cache's one decision reads) has its
//       card resident in the other - walk = teleport on THE RESIDENT SET (the spec's acceptance). The settling phase's
//       rates (the 30 frames after the arrival) are printed as information: a face read only while settling (the band
//       face 21/1 at 1.1 hits a frame after a 60-frame walk) is not the rest's demand;
//   (3) THE VALUES, printed: every lit texel of every card resident in both runs against the other run's, in codes
//       (1 % of its radiance per channel over the store's floor) past one step of the RGBA16F store (PHOTON-7 stage 10,
//       10-2d: the radiance store's quantum under the code) - A LAW since 10-2e (the lead's ruling): none over a code.
//       The cascades' placements at one pose are path-dependent (the scroll's hysteresis: after the walk c1-c3 trail the
//       eye), and a hit read by the box that held it read another level from another placement; a TLAS hit now reads
//       its level by its distance to the GI camera, inside every placement the engine can reach (giHitLevelRadius).
//   (6) THE HISTORY PART OF walk = teleport (PHOTON-7 stage 10, 10-2d; the lead's ruling: residency is path-independent
//       at rest): a fifth run, the teleport with cascades 1 and 2 PINNED to the walk's final placement (the placement
//       part removed: 10-3's), holds THE SAME RESIDENT SET as the walk (every card of the one resident in the other, by
//       the run-independent ids) and (3)'s values read 0 texels over a code. RED before 10-2d: the walk 264 cards, the
//       teleport 248 (the walk's cards read before its last move stayed while there was room), 93.5 codes - since the
//       cards read cards, an answer depends on which cards are resident (spikes/photon-7/stage10/10-2).
//   (5) A LATE FIRST READER KEEPS THE DEMAND OPEN FOR ITS OWN SETTLE (PHOTON-7 stage 5's read W3): a fourth run, the
//       teleport again with the test door cards.gatherLate = 8 (the gather's stamps within 8 frames of the arrival
//       dropped: it reads first 8 frames late, inside the settle's S = 10) - every face of the teleport's read set
//       (read at B at least lambda_min a frame) resident in it. RED under the readers' MOST clock (the demand closed S
//       frames after the change, before the late reader's first read-back: 106 of the 106 lost); the reflection never
//       reads here, so a least clock would never close it (the rule: each reader from its own first stamp).
//   (7) THE STORE AT REST HOLDS (PHOTON-7 stage 10; the lead's ruling on the TLAS route's orbit): in every run, at B's
//       rest, one more frame - every cascade's two sides read before and after it, each changed channel's move in STORE
//       STEPS at its own magnitude (the half's 2^-10 of the value, 2^-24 under its smallest normal) against its brick's
//       ROUNDING BOUND 0.5 / (1 - r) steps (r the brick's word 2: the measured contraction, the operator's bound before
//       one): round-to-nearest cannot bring a contraction closer than that to its fixed point, and the scheduler takes a
//       move within it as no move (about 1.5e-7 radiance at the floor at r 0.8). PRINTED: the channels that moved, the
//       bricks they lie in, the worst move over its bound; THE LAW: none over its bound - and THE CONVEX EDGE (the top
//       front edge of a court box, the walk's cascade 0 cell 67,93,84) within a code of the same placement's Jacobi
//       re-solve (gi.bounceJacobi, 64 frames; plus that achievable quantum). RED before the scheduler's rule and the share's precision (a period-2
//       store orbit at the subnormal floor, and the edge toggling 230-1725 steps through a bounce share taken at a
//       level-0 texel holding one quantum, kept the crowded room's B awake for good).
//   (8) L1 - A CARD'S PRESENCE MOVES ITS READERS BY AT MOST ITS FACE'S CARD-VOXEL DISAGREEMENT (PHOTON-7 stage 10; the
//       bar DERIVED, the lead's ruling): room B's pillar 8's +x band card held resident in one pinned run and absent in
//       the other (cards.force); every other card resident in both; and every reader texel's |delta L| (absolute, per
//       channel) at most the largest |L_card - L_voxel| over the band face's hits - a reader's radiance moves by its
//       albedo x its share of rays on the face x the face's move, both at most 1. The face's hits are the gather's own,
//       frozen (gather.freezeFrame + gather.hitsReadback) with the camera on the +x wall behind the face, matched ray by
//       ray across the two runs. A PROPAGATION LAW (10-3 R12, the lead's ruling): its input - the face's card-voxel
//       disagreement - is MEASURED here (printed), never a picked bar; the two caches' truth against physics is
//       gi.card_truth's (the analytic direct, the same rays on the CPU, a brute-force hemisphere). (Measured 2026-10-26
//       against the card estimator at 128 rays: both caches' fixed 16-ray quadrature, the voxel side's single surface
//       point the worse, -13 % at the band face.)
//       AND THE NO POP: at the held-out run's rest the card is let in, then held out again - no pixel moves more than 1
//       code a frame through either. DECLARED RED FOR STAGE 8 (its no-pop law for a cache refinement): a card's writes
//       move the readers' view-wide rest keys, so a held view drops to the settling EMA for as long as the rounds run
//       (measured: keyMoves 556 -> 637 over 81 frames, 2-3 codes a frame at the interior, 51 at a sub-lattice sliver).
//   (4) THE ATLAS COMPACTS: a third run teleports from a CROWDED room (a 7 x 3 x 7 grid of 0.8 m cubes: its read set
//       fills the atlas with quarter-page cards) to room B (whole pages): the arrivals find no free page and the atlas
//       merges its sub-cut pages (compaction moves > 0, each moved card re-captured), and B comes to rest carded.
// THE ROWS (PHOTON-7 stage 10, the lead's ruling: one binary, one row a law group, each within the fixture's 10 s, every
// run the fewest frames its law needs - AT REST is the cache's one rest decision since the stop and the cache owing its
// readers nothing, held kQuiet frames):
// gi.card_residency (argv walk: the walk and the teleport - (1) (2) (3) (7)), gi.card_residency_pinned (the walk and the
// pinned teleport - (6)), _crowded ((4)), _late (the teleport and the late reader - (5)), _held (the band card held in
// and out, both runs AT B - (8)); every row checks (7)'s store at rest over its own runs.
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
/// (7)'s census over every run: the channels that moved in the frame after B's rest, those over their brick's bound
static unsigned long long gOrbitMoved = 0ull, gOrbitOver = 0ull;
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
    /// each FACE's READ RATE at B over the 30 frames after the arrival (hits a frame, both readers, every read of it -
    /// its card's or, none resident, the hit that marked it: the measuring arm's counts, Scene::readCardFaceDemand), by
    /// its node and bucket - the settle law's lambda
    std::map<std::pair<NodeId, unsigned>, double> rate;
    /// ...and each face's read rate over THE READERS' REST-16 at B ((2)'s read set, restated with R3: the first 16
    /// frames of the gather's rest - the rest the cache's one decision reads), hits a frame
    std::map<std::pair<NodeId, unsigned>, double> restRate;
    bool restRateOk = false;
    unsigned long long frame = 0ull, newest = 0ull, evicted = 0ull, admitted = 0ull, compaction = 0ull;
    unsigned pagesUsed = 0u;
    /// the last rest decision's room and arrivals (CardCacheStatus::decision*)
    unsigned long long decisionUsed = 0ull, decisionFree = 0ull, decisions = 0ull;
    unsigned decisionAdmitted = 0u, decisionRefused = 0u;
    std::vector<CardCacheStatus::Refused> refusedCards;
    int restA = -1, restB = -1;
    Room a, b;
    /// each cascade's placement at B (the pinned arm (6) takes the walk's)
    std::vector<Vec3> builtAtB;
    /// (7)'s CONVEX EDGE (the toggle the readback named, PHOTON-7 stage 10): cascade 0's front at the world point of the
    /// walk's toggling cell c0 67,93,84 - the top front edge of a court box - at B's rest (ok: the cascade holds it)
    bool edgeOk = false, edgeRefOk = false;
    float edge[3] = { 0.f, 0.f, 0.f }, edgeRef[3] = { 0.f, 0.f, 0.f };
    /// (8)'s NO POP (the held-out run): the card's ARRIVAL then its DEPARTURE at B's rest - the largest change of any
    /// pixel in one frame through each (display codes), and whether the picture came to rest again after each
    int popIn = -1, popOut = -1;
    bool popInRest = false, popOutRest = false;
    /// (8)'s FACE HITS: the frozen gather's rays that hit the band face (pillar 8's +x face), by the ray's index in the
    /// readback - what answered each (GatherRay::Source) and its radiance
    struct HitRay { unsigned char src = 0; float rgb[3] = { 0.f, 0.f, 0.f }; };
    std::map<unsigned, HitRay> face;
    bool hitsOk = false;
};
/// gi.card_residency_held's runs start AT B (no room A, no move: (8)'s law needs the rest at B alone)
static bool gStartAtB = false;
/// the frames between turning the camera to the wall behind the band face and reading its hits
static const int kCaptureWait = 8;
/// (7)'s convex edge's Jacobi reference: the walk row's alone (64 frames the pinned row's walk does not need)
static bool gEdgeReference = false;
/// AT REST's frames (settle): the cache's own "owes nothing" (CardCacheStatus::readersHoldOwed false: the decision's
/// work done, no report in flight) held over 2 frames, since the published status is a frame late. It replaced 30 quiet
/// frames of no relight and no admission (PHOTON-7 stage 10: a heuristic that cost 28 frames a settle, and the row its
/// fixture's 10 s once the decision moved a due later).
static const int kQuiet = 2;
/// THE WALK's frames from A to B (24 m; 120 before the rows' split - the law reads B's rest, not the walk's pace)
static const int kWalkFrames = 60;
/// the edge's world point: the walk's cascade 0 cell 67,93,84 at B (the readback's, spikes/photon-7/stage10/10-2)
static const float kEdgeWorld[3] = { 12.30f, 4.02f, 4.10f };

bool runOnce(Engine *e, bool walk, Run &out, int &frames, bool crowded = false, int gatherLate = 0,
             const std::vector<Vec3> *pinTo = nullptr, int holdCard = 0) {
    const char *name = crowded ? "residency_crowded" : walk ? "residency_walk" : gatherLate ? "residency_late"
                                                     : holdCard > 0 ? "residency_held_in"
                                                     : holdCard < 0 ? "residency_held_out"
                                                     : pinTo ? "residency_pinned"
                                                             : "residency_teleport";
    View *view = e->createOffscreenView(name, 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene(name);
    if (!view || !scene) return false;
    view->setScene(scene);
    {
        PostFxDesc fx;   // the chain, so the gather runs (test_gi_gather_findings.cpp's armChain)
        fx.allowOffscreen = true;
        fx.ssr = 1;
        view->setPostFx(fx);
    }
    // A STILL PICTURE GATHERS (View::setOffscreenContract; PHOTON-7 7-5: the gather is recorded only for a view whose
    // chain gathers, and a Live view takes the field's answer)
    out.a = buildRoom(scene, Vec3(-12.0f, 0.0f, 0.0f), crowded);
    out.b = buildRoom(scene, Vec3(12.0f, 0.0f, 0.0f));
    // (8)'s ARMS: room B's pillar 8's +x face (its card 1, the band face the measurements found) HELD resident or absent
    if (holdCard && out.b.pillars.size() > 8u)
        e->setArm("cards.force", double(holdCard) * double(unsigned(out.b.pillars[8]) * 64u + 1u));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    const auto look = [&](float x) {
        enginetest::testCameraLookAt(view, Vec3(x, 1.7f, 2.5f), Vec3(x + 0.5f, 1.5f, -2.0f));
    };
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    // AT REST: the cache's ONE rest decision taken since the stop (R3 (b): its drains and admissions are the rest's last
    // residency move), and THE CACHE OWES ITS READERS NOTHING (readersHoldOwed false: no capture, relight or round to run,
    // no report in flight - so no listing can still come), no chain or feedback owed, an empty queue, held kQuiet frames
    // (90 quiet frames before the decision existed, then 30; the row's budget, the lead's ruling: the fewest frames the
    // law needs)
    // THE REST-16'S READS (B's settle): the faces' cumulative hits at the gather's FIRST rest frame and at its 17th - the
    // reads in between are the rest sequence's first 16 frames (the harvest's lag shifts both ends alike); a rest that
    // restarts before its 17th frame starts the window again
    std::vector<CardFaceDemand> rest0, rest1;
    int restPhase = 0;   // 0 none, 1 the first rest frame snapshotted, 2 the 17th
    const auto restWatch = [&](const GiStatus &g) {
        const unsigned rf = g.gather.restFrames;
        unsigned long long cf = 0ull;
        if (rf == 1u && restPhase < 2) { scene->readCardFaceDemand(rest0, cf); restPhase = 1; }
        else if (rf == 0u && restPhase == 1) restPhase = 0;
        else if (rf >= 17u && restPhase == 1) { scene->readCardFaceDemand(rest1, cf); restPhase = 2; }
    };
    const auto settle = [&](unsigned long long decisionsAtStop, bool watchRest) {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 1500 && quiet < kQuiet; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            if (watchRest) restWatch(g);
            const bool still = !g.chainOwed && !g.feedbackOwed && g.cards.built && g.cards.relights == relights &&
                               g.cards.admitted == admitted && g.cards.queueLength == 0u &&
                               g.cards.restDecisions > decisionsAtStop && !g.cards.readersHoldOwed;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        if (quiet < kQuiet) {
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
        return quiet >= kQuiet ? f - quiet : -1;
    };
    look(gStartAtB ? 12.0f : -12.0f);
    if (!scene->setGlobalIllumination(gi)) return false;
    // (6)'s ARM: cascades 1 and 2 pinned to the walk's final placement (built there, no scroll history), 0 and 3 free
    if (pinTo && pinTo->size() >= 4u) {
        GiPlacementPin pin;
        pin.on = true;
        pin.cascadeBuiltCam = *pinTo;
        pin.cascadeFree = { 1u, 0u, 0u, 1u };
        scene->setGiPlacementPin(pin);
    }
    e->waitForAsyncShaders();
    // A: THE WALK LEAVES ONCE A's CARDS ARE RESIDENT AND CAPTURED (the cache built, its queue empty, cards resident;
    // PHOTON-7 stage 10, the row's budget): every law reads B, so A's card-to-card rounds, its rest, its decision and the
    // quiet after them were frames no law read - and a walk that leaves while A's rounds still run is a history the
    // walk = teleport law must hold all the same. (Was: A at rest, as B: 345 frames.)
    const auto captured = [&]() {
        int f = 0;
        for (; f < 1500; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            if (g.cards.built && g.cards.cardsResident > 0u && g.cards.queueLength == 0u) break;
        }
        return f < 1500 ? f : -1;
    };
    out.restA = gStartAtB ? 0 : captured();
    if (gatherLate) e->setArm("cards.gatherLate", double(gatherLate));   // (5): the gather reads first N frames late
    if (walk) {
        for (int f = 1; f <= kWalkFrames; ++f) {
            look(-12.0f + 24.0f * float(f) / float(kWalkFrames));
            run(1);
        }
    } else if (!gStartAtB) {
        look(12.0f);
    }
    // (the stop: B's rest decision is the next one)
    const unsigned long long decisionsAtStop = gStartAtB ? 0ull : scene->published().gi.cards.restDecisions;
    // THE READ RATES AT B (the settle law's own quantity: GiQualityFacts::cardDemandSettleFrames catches every face
    // read at least once a frame): each face's hits over the 30 frames after the arrival, while the readers run (they
    // rest once the view holds) - EVERY read of the face, resident or not (RED as an instrument before: a count per
    // resident card's atlas CELL, cumulative over the cell's occupants, read a face that arrived inside the 30 frames as
    // every hit its cell had taken since the arm was set - the walk's pillar top at 5.4 a frame, never read in the
    // teleport)
    {
        std::vector<CardFaceDemand> d0, d1;
        unsigned long long f0 = 0ull, f1 = 0ull;
        scene->readCardFaceDemand(d0, f0);
        run(30);
        scene->readCardFaceDemand(d1, f1);
        std::map<std::pair<NodeId, unsigned>, unsigned long long> h0;
        for (const CardFaceDemand &k : d0) h0[{ k.node, k.bucket }] = k.hits[0] + k.hits[1];
        for (const CardFaceDemand &k : d1) {
            const unsigned long long n = k.hits[0] + k.hits[1];
            auto it = h0.find({ k.node, k.bucket });
            const unsigned long long base = it != h0.end() && n >= it->second ? it->second : 0ull;
            out.rate[{ k.node, k.bucket }] = double(n - base) / 30.0;
        }
    }
    out.restB = settle(decisionsAtStop, true);
    if (restPhase == 2) {
        std::map<std::pair<NodeId, unsigned>, unsigned long long> h0;
        for (const CardFaceDemand &k : rest0) h0[{ k.node, k.bucket }] = k.hits[0] + k.hits[1];
        for (const CardFaceDemand &k : rest1) {
            const unsigned long long n = k.hits[0] + k.hits[1];
            auto it = h0.find({ k.node, k.bucket });
            const unsigned long long base = it != h0.end() && n >= it->second ? it->second : 0ull;
            out.restRate[{ k.node, k.bucket }] = double(n - base) / 16.0;
        }
        out.restRateOk = true;
    }
    {
        const GiStatus gb = scene->published().gi;
        out.builtAtB.clear();
        for (const auto &cs : gb.cascades) out.builtAtB.push_back(cs.builtCam);
    }
    {
        // (7) THE STORE AT REST HOLDS: one frame more, every cascade's two sides before and after it against the bricks'
        // rounding bounds (the header)
        const int cascades = int(scene->published().gi.cascades.size());
        std::vector<GiVoxelVolume> v0(size_t(std::max(cascades, 0)));
        std::vector<GiFeedbackBricks> st(v0.size());
        for (int c = 0; c < cascades; ++c) {
            scene->giVoxelLight(c, v0[size_t(c)]);
            scene->giFeedbackBricks(c, st[size_t(c)]);
        }
        if (!v0.empty() && v0[0].available) {
            const GiVoxelVolume &z = v0[0];
            int q[3];
            bool inside = true;
            for (int k = 0; k < 3; ++k) {
                q[k] = int(std::floor((kEdgeWorld[k] - z.origin[k]) / z.cell[k]));
                const int n = k == 0 ? z.width : k == 1 ? z.height : z.depth;
                inside = inside && q[k] >= 0 && q[k] < n;
            }
            const size_t i = inside ? (size_t(q[2]) * size_t(z.height) + size_t(q[1])) * size_t(z.width) + size_t(q[0]) : 0;
            if (inside && z.lightFront.size() >= 4 * i + 3) {
                out.edgeOk = true;
                for (int k = 0; k < 3; ++k) out.edge[k] = z.lightFront[4 * i + size_t(k)];
            }
        }
        run(1);
        unsigned long long moved = 0ull, over = 0ull, measured = 0ull;
        size_t bricksMoved = 0;
        double worst = 0.0, worstAmp = 0.0, worstBound = 0.0, worstR = 0.0, mostSteps = 0.0;
        for (int c = 0; c < cascades; ++c) {
            GiVoxelVolume v1, full;
            const GiVoxelVolume &a = v0[size_t(c)];
            const GiFeedbackBricks &f = st[size_t(c)];
            if (!scene->giVoxelLight(c, v1) || !a.available || !v1.available || !f.available) continue;
            bool any = false;
            for (const std::vector<float> GiVoxelVolume::*side : { &GiVoxelVolume::lightFront, &GiVoxelVolume::lightBack })
                any = any || (a.*side) != (v1.*side);
            if (!any) continue;
            // (the voxels' OPERATOR BOUND - what a voxel re-emits of what it gathers: its albedo times its opacity, the
            // bounce's own factor - read for the moved cascades alone)
            const bool haveAlbedo = scene->giVoxelVolume(c, full) && full.albedo.size() == a.lightFront.size();
            const size_t W = size_t(a.width), H = size_t(a.height);
            std::vector<char> hit(f.r.size(), 0);
            for (const std::vector<float> GiVoxelVolume::*side : { &GiVoxelVolume::lightFront, &GiVoxelVolume::lightBack }) {
                const std::vector<float> &x = a.*side, &y = v1.*side;
                if (x.size() != y.size()) continue;
                for (size_t i = 0; i + 3 < x.size(); i += 4) {
                    const size_t q = i / 4, bx = (q % W) / 8u, by = ((q / W) % H) / 8u, bz = q / (W * H) / 8u;
                    const size_t b = bx + f.dims[0] * (by + size_t(f.dims[1]) * bz);
                    if (b >= f.r.size()) continue;
                    const double op = haveAlbedo ? std::min(0.999, double(std::max({ full.albedo[i], full.albedo[i + 1],
                                                                                     full.albedo[i + 2] })) *
                                                                       double(full.albedo[i + 3]))
                                                 : 0.999;
                    const bool isMeasured = (f.flags[b] & 2048u) != 0u;
                    const double rr = isMeasured ? std::min(std::min(std::max(double(f.r[b]), 0.0), 0.999), op) : op;
                    const double bound = 0.5 / (1.0 - rr);
                    for (int ch = 0; ch < 3; ++ch) {
                        const double d = std::fabs(double(y[i + ch]) - double(x[i + ch]));
                        if (!(d > 0.0)) continue;
                        const double m = std::max({ std::fabs(double(x[i + ch])), std::fabs(double(y[i + ch])), 6.1035156e-5 });
                        const double steps = d / std::ldexp(1.0, int(std::floor(std::log2(m))) - 10);
                        ++moved;
                        measured += isMeasured ? 1u : 0u;
                        mostSteps = std::max(mostSteps, steps);
                        const bool isOver = steps > std::max(1.0, bound) + 1e-9;
                        over += isOver ? 1u : 0u;
                        if (isOver && over <= 6u) {
                            const size_t qx = q % W, qy = (q / W) % H, qz = q / (W * H);
                            std::printf("     (7) over at test frame %d (parity %d): cascade %d %s cell %zu %zu %zu (%.2f %.2f %.2f) "
                                        "channel %d: %.6g -> %.6g (%.0f steps, bound %.2f; albedo x opacity %.3f; normal %.2f %.2f "
                                        "%.2f)\n", frames, frames & 1, c,
                                        side == &GiVoxelVolume::lightFront ? "front" : "back", qx, qy, qz,
                                        a.origin[0] + (qx + 0.5f) * a.cell[0], a.origin[1] + (qy + 0.5f) * a.cell[1],
                                        a.origin[2] + (qz + 0.5f) * a.cell[2], ch, x[i + ch], y[i + ch], steps, bound, op,
                                        a.normal.size() > i + 2 ? a.normal[i] : -1.f, a.normal.size() > i + 2 ? a.normal[i + 1] : -1.f,
                                        a.normal.size() > i + 2 ? a.normal[i + 2] : -1.f);
                        }
                        if (steps / bound > worst) {
                            worst = steps / bound;
                            worstAmp = steps;
                            worstBound = bound;
                            worstR = isMeasured ? double(f.r[b]) : -1.0;
                        }
                        if (!hit[b]) { hit[b] = 1; ++bricksMoved; }
                    }
                }
            }
        }
        if (over > 0u) {
            // (its time series: every over-bound channel of cascade 0 over the next four frames, by the frame's parity)
            {
                std::vector<GiVoxelVolume> ser(4);
                std::vector<int> fr(4);
                for (int k = 0; k < 4; ++k) {
                    scene->giVoxelLight(0, ser[size_t(k)]);
                    fr[size_t(k)] = frames;
                    run(1);
                }
                const GiVoxelVolume &a0 = v0[0];
                int shownS = 0;
                for (size_t i = 0; i + 3 < a0.lightFront.size() && shownS < 4; i += 4)
                    for (int ch = 0; ch < 3 && shownS < 4; ++ch) {
                        bool varies = false;
                        for (int k = 1; k < 4; ++k)
                            varies = varies || ser[size_t(k)].lightFront[i + ch] != ser[0].lightFront[i + ch];
                        if (!varies) continue;
                        const size_t W0 = size_t(a0.width), H0 = size_t(a0.height), q = i / 4;
                        std::printf("     (7) series c0 cell %zu %zu %zu ch %d:", q % W0, (q / W0) % H0, q / (W0 * H0), ch);
                        for (int k = 0; k < 4; ++k)
                            std::printf(" [frame %d p%d] %.4g / back %.4g", fr[size_t(k)], fr[size_t(k)] & 1,
                                        ser[size_t(k)].lightFront[i + ch],
                                        ser[size_t(k)].lightBack.size() > i + ch ? ser[size_t(k)].lightBack[i + ch] : -1.f);
                        std::printf("\n");
                        ++shownS;
                    }
            }
            // (the failure's evidence: the moved cascades' direct terms and totals one frame further - a toggle that
            // returns is a period-2 orbit; its direct term toggling with it names an injection, standing still the bounce)
            for (int c = 0; c < cascades; ++c) {
                GiVoxelVolume f1, f2;
                if (!scene->giVoxelVolume(c, f1)) continue;
                run(1);
                if (!scene->giVoxelVolume(c, f2)) continue;
                size_t dDirect = 0, dTotal = 0, dDirectBack = 0;
                for (size_t i = 0; i < f1.direct.size() && i < f2.direct.size(); ++i) dDirect += f1.direct[i] != f2.direct[i];
                for (size_t i = 0; i < f1.directBack.size() && i < f2.directBack.size(); ++i)
                    dDirectBack += f1.directBack[i] != f2.directBack[i];
                for (size_t i = 0; i < f1.lightFront.size() && i < f2.lightFront.size(); ++i)
                    dTotal += f1.lightFront[i] != f2.lightFront[i];
                std::printf("     (7) cascade %d over the next frame: %zu front channels moved, its direct %zu, its direct back "
                            "%zu\n", c, dTotal, dDirect, dDirectBack);
            }
        }
        gOrbitMoved += moved;
        gOrbitOver += over;
        std::printf("   (7) %s at B's rest, one frame more: %llu channels moved in %zu bricks (%llu in bricks with a measured "
                    "r), the largest move %.1f steps; %llu over their rounding bound (the worst %.1f steps against %.2f, its "
                    "brick's measured r %.3f: %.2f x)\n", name, moved, bricksMoved, measured, mostSteps, over, worstAmp,
                    worstBound, worstR, worst);
    }
    if (gatherLate) e->setArm("cards.gatherLate", 0.0);
    const GiStatus g = scene->published().gi;
    out.evicted = g.cards.evicted;
    out.admitted = g.cards.admitted;
    out.compaction = g.cards.compactionMoves;
    out.pagesUsed = g.cards.pagesUsed;
    out.decisionUsed = g.cards.decisionUsedTexels;
    out.decisionFree = g.cards.decisionFreeTexels;
    out.decisions = g.cards.restDecisions;
    out.decisionAdmitted = g.cards.decisionAdmitted;
    out.decisionRefused = g.cards.decisionRefused;
    out.refusedCards = g.cards.decisionRefusedCards;
    const bool ok = scene->readCardRadiance(out.cards, out.frame);
    for (const CardRadiance &c : out.cards) out.newest = std::max(out.newest, c.lastRead);
    if (holdCard < 0 && out.b.pillars.size() > 8u) {
        // (8)'s NO POP (after the held run's cards are read): at B's rest the held-out card is let IN, then held OUT again -
        // each transition 120 frames, every frame's picture against the frame before; the arrival and the departure must
        // never be a step (1 code a frame)
        const double card = double(unsigned(out.b.pillars[8]) * 64u + 1u);
        for (int phase = 0; phase < 2; ++phase) {
            e->setArm("cards.force", phase == 0 ? card : -card);
            Image prev, cur;
            bool havePrev = view->readPixels(prev);
            int worst = 0, quiet = 0;
            // (the transition ends EARLY once the picture has rested 30 frames AND the cache owes its readers nothing - no
            // pop can still come from it; PHOTON-7 stage 10, the row's budget: the fixed 120 frames ran on past both)
            for (int f = 0; f < 120; ++f) {
                if (quiet >= 30 && !scene->published().gi.cards.readersHoldOwed) break;
                run(1);
                if (!view->readPixels(cur)) continue;
                int frameMax = 0;
                if (havePrev && cur.rgba.size() == prev.rgba.size())
                    for (size_t i = 0; i < cur.rgba.size(); ++i)
                        if (i % 4u != 3u) frameMax = std::max(frameMax, std::abs(int(cur.rgba[i]) - int(prev.rgba[i])));
                worst = std::max(worst, frameMax);
                quiet = frameMax == 0 ? quiet + 1 : 0;
                prev.rgba.swap(cur.rgba);
                prev.width = cur.width;
                prev.height = cur.height;
                havePrev = true;
            }
            (phase == 0 ? out.popIn : out.popOut) = worst;
            (phase == 0 ? out.popInRest : out.popOutRest) = quiet >= 30;
        }
    }
    if (holdCard && out.b.pillars.size() > 8u) {
        // THE FACE HITS ((8)'s bar): the gather frozen (one ray set, the same in every run) with its ray readback - first
        // the view as it rests, then the camera on the +x wall behind pillar 8, whose probes' rays hit the band face; a ray
        // is kept where a card or the voxels answered it on the surface
        e->setArm("gather.freezeFrame", 1.0);
        e->setArm("gather.hitsReadback", 1.0);
        const auto take = [&](bool faceToo) {
            const GatherStatus &gs = scene->published().gi.gather;
            for (size_t i = 0; i < gs.rays.size(); ++i) {
                const GatherRay &r = gs.rays[i];
                if (r.distance < 0.0f || (r.source != GatherRay::Card && r.source != GatherRay::Voxel)) continue;
                Run::HitRay h;
                h.src = r.source;
                for (int c = 0; c < 3; ++c) h.rgb[c] = r.radiance[c];
                const float fx = out.b.centre.x + 2.4f + 0.2f, fz = out.b.centre.z + 2.4f;
                if (faceToo && std::fabs(r.hitPos[0] - fx) < 0.03f && std::fabs(r.hitPos[2] - fz) < 0.2f &&
                    r.hitPos[1] > 0.0f && r.hitPos[1] < 3.5f && r.hitNormal[0] > 0.9f)
                    out.face[unsigned(i)] = h;
            }
            return !gs.rays.empty();
        };
        run(8);
        const bool okWall = take(false);
        enginetest::testCameraLookAt(view, Vec3(out.b.centre.x + 3.3f, 1.75f, out.b.centre.z + 0.6f),
                                     Vec3(out.b.centre.x + 4.0f, 1.75f, out.b.centre.z + 2.6f));
        // (the frozen rays' readback is a few frames late; the held row reads the two runs' answers at one moment)
        run(kCaptureWait);
        out.hitsOk = okWall && take(true);
        e->setArm("gather.hitsReadback", 0.0);
        e->setArm("gather.freezeFrame", 0.0);
    }
    if (holdCard) e->setArm("cards.force", 0.0);
    if (gEdgeReference && walk && !crowded && !pinTo && !gatherLate && out.edgeOk) {
        // (7)'s REFERENCE for the convex edge: the same placement re-solved by THE JACOBI DOOR (gi.bounceJacobi: every
        // cascade restarted from its direct term, every brick a pass a frame) - the fixed point by another path, the
        // same grid; read once it holds (64 frames: bounce_series' Jacobi-64)
        e->setArm("gi.bounceJacobi", 4.0);
        run(64);
        GiVoxelVolume z;
        if (scene->giVoxelLight(0, z) && z.available) {
            int q[3];
            bool inside = true;
            for (int k = 0; k < 3; ++k) {
                q[k] = int(std::floor((kEdgeWorld[k] - z.origin[k]) / z.cell[k]));
                const int n = k == 0 ? z.width : k == 1 ? z.height : z.depth;
                inside = inside && q[k] >= 0 && q[k] < n;
            }
            const size_t i = inside ? (size_t(q[2]) * size_t(z.height) + size_t(q[1])) * size_t(z.width) + size_t(q[0]) : 0;
            if (inside && z.lightFront.size() >= 4 * i + 3) {
                out.edgeRefOk = true;
                for (int k = 0; k < 3; ++k) out.edgeRef[k] = z.lightFront[4 * i + size_t(k)];
            }
        }
        e->setArm("gi.bounceJacobi", 0.0);
        run(2);
    }
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

int main(int argc, char **argv)
{
    // THE ROW (one binary, one row a law group - each within the fixture's budget: the lead's ruling): walk = the walk
    // and the teleport, (1) (2) (3) (7); pinned = the walk and the pinned teleport, (6); crowded = (4); late = the
    // teleport and the late reader, (5); held = the band card held in and out at B, (8)
    const std::string mode = argc > 1 ? argv[1] : "walk";
    if (mode != "walk" && mode != "pinned" && mode != "crowded" && mode != "late" && mode != "held") {
        std::printf("FAIL: unknown row '%s' (walk | pinned | crowded | late | held)\n", mode.c_str());
        return 1;
    }
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-card-residency-" + mode + "-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    e->setArm("cards.demandCount", 1.0);   // the readers' hits counted: the read rates (2) reads
    // (THE MEASURING HOOK of 10-2e's margin: JAH_HIT_LEVEL_MARGIN=<cells> sets the door gi.hitLevelMargin for the run)
    if (const char *m = std::getenv("JAH_HIT_LEVEL_MARGIN")) e->setArm("gi.hitLevelMargin", std::atof(m));
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));

    gEdgeReference = mode == "walk";
    Run w, t, c, l, pnd, hin, hout;
    bool okW = true, okT = true, okP = true, okC = true, okL = true, okHin = true, okHout = true;
    // (the teleports START AT B - the teleport with no past: walk = teleport is path independence, and no frame of room
    // A is the law's; the walk keeps A, whose cards its arrival at B must evict)
    if (mode == "walk" || mode == "pinned") okW = runOnce(e, true, w, frames);
    gStartAtB = true;
    if (mode == "walk" || mode == "late") okT = runOnce(e, false, t, frames);
    if (mode == "pinned") okP = runOnce(e, false, pnd, frames, false, 0, &w.builtAtB);
    if (mode == "late") okL = runOnce(e, false, l, frames, false, 8);
    gStartAtB = false;
    if (mode == "crowded") okC = runOnce(e, false, c, frames, true);
    if (mode == "held") {
        // (8) L1: the band face's card held resident in one run and absent in another (cards.force), both AT B
        gStartAtB = true;
        okHin = runOnce(e, false, hin, frames, false, 0, nullptr, +1);
        okHout = runOnce(e, false, hout, frames, false, 0, nullptr, -1);
        gStartAtB = false;
    }
    if (mode == "walk") {
    std::printf("   WALK:     at rest in A %d frames, in B %d | %zu cards resident, %u pages | admitted %llu, evicted %llu, "
                "compaction moves %llu\n", w.restA, w.restB, w.cards.size(), w.pagesUsed, w.admitted, w.evicted,
                w.compaction);
    std::printf("   TELEPORT: at rest in A %d frames, in B %d | %zu cards resident, %u pages | admitted %llu, evicted %llu, "
                "compaction moves %llu\n", t.restA, t.restB, t.cards.size(), t.pagesUsed, t.admitted, t.evicted,
                t.compaction);
    CHECK(okW && okT && w.restA >= 0 && w.restB >= 0 && t.restB >= 0,
          "both runs come to rest (the walk in A and in B, the teleport at B) and their cards read back");
    }

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
    if (mode == "walk") {
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
    }

    // (2) the read set resident in both: every FACE read at B at least once a frame (the settle law's floor) in either
    //     run is resident in the other (a face's cards: the card of its bucket - every box face one card here)
    std::map<std::pair<int, unsigned>, const CardRadiance *> inW, inT;
    for (const CardRadiance &c : w.cards) if (idW.count(c.node)) inW[{ idW.at(c.node), c.card }] = &c;
    for (const CardRadiance &c : t.cards) if (idT.count(c.node)) inT[{ idT.at(c.node), c.card }] = &c;
    // each run's faces by the run-independent id: its read rate, and whether a card of it is resident
    // (2) RESTATED WITH R3 (the lead's ruling): the read set is the faces read in the readers' REST-16 at B - the rest the
    // cache's one decision reads; the settling phase's rate (the 30 frames after the arrival) is printed as information
    struct Face { double rate = 0.0, settling = 0.0; bool resident = false; };
    const auto faces = [](const Run &r, const std::map<NodeId, int> &id) {
        std::map<std::pair<int, unsigned>, Face> f;
        for (const auto &kv : r.restRate)
            if (id.count(kv.first.first)) f[{ id.at(kv.first.first), kv.first.second }].rate = kv.second;
        for (const auto &kv : r.rate)
            if (id.count(kv.first.first)) f[{ id.at(kv.first.first), kv.first.second }].settling = kv.second;
        for (const CardRadiance &c : r.cards)
            if (id.count(c.node)) f[{ id.at(c.node), c.bucket }].resident = true;
        return f;
    };
    const auto fW = faces(w, idW), fT = faces(t, idT);
    // THE FLOOR: the settle law's lambda_min (GiQualityFacts::cardDemandHitFloor), the one constant
    const double floor = double(giQualityFacts(GiQuality::High, GiViewProfile::Desktop).cardDemandHitFloor);
    size_t readW = 0, readT = 0, missW = 0, missT = 0, bandW = 0, bandT = 0;
    const auto side = [&](const std::map<std::pair<int, unsigned>, Face> &mine,
                          const std::map<std::pair<int, unsigned>, Face> &theirs, const char *name, const char *otherName,
                          size_t &read, size_t &miss, size_t &band) {
        for (const auto &kv : mine) {
            const double rt = kv.second.rate;
            auto o = theirs.find(kv.first);
            const bool there = o != theirs.end() && o->second.resident;
            if (rt >= floor) { ++read; miss += there ? 0u : 1u; }
            if (rt >= 0.5 * floor && rt < floor) ++band;
            if (rt >= floor && !kv.second.resident)   // the settle law's own claim in this run (printed)
                std::printf("   %s: id %d face %u read %.3f hits a frame in the rest-16 at B and NOT resident in it\n", name,
                            kv.first.first, kv.first.second, rt);
            if (kv.second.settling >= floor && !kv.second.resident)   // (information: read while settling only)
                std::printf("   %s: id %d face %u read %.3f hits a frame while SETTLING at B (%.3f in the rest-16), not "
                            "resident (information)\n", name, kv.first.first, kv.first.second, kv.second.settling, rt);
            if (!there && rt > 0.0 && kv.second.resident)
                std::printf("   %s only: id %d face %u read %.3f hits a frame at B (the %s: %.3f)%s\n", name,
                            kv.first.first, kv.first.second, rt, otherName, o != theirs.end() ? o->second.rate : 0.0,
                            rt >= floor ? " - AT THE FLOOR OR MORE" : rt >= 0.5 * floor ? " (the band floor/2 .. floor)" : "");
        }
    };
    if (mode == "walk") {
    side(fW, fT, "walk", "teleport", readW, missW, bandW);
    side(fT, fW, "teleport", "walk", readT, missT, bandT);
    std::printf("   the read set (faces read in the readers' rest-16 at B at least %.2f hits a frame, the settle law's floor): "
                "walk %zu (%zu not resident in the teleport), teleport %zu (%zu not resident in the walk) | in the band "
                "floor/2 .. floor (the law leaves to chance): walk %zu, teleport %zu\n", floor, readW, missW, readT, missT,
                bandW, bandT);
    CHECK(w.evicted > 0u && w.restRateOk && t.restRateOk && readW > 0u && readT > 0u && missW == 0u && missT == 0u,
          "(2) THE ATLAS MADE ROOM (the walk's unread cards evicted) AND THE READ SET IS RESIDENT IN BOTH");
    }

    // (5) THE LATE FIRST READER: the teleport's read set resident in the late run
    if (mode == "late") {
        const std::map<NodeId, int> idL = key(l);
        const auto fL = faces(l, idL);
        size_t readSet = 0, missL = 0;
        for (const auto &kv : fT)
            if (kv.second.rate >= floor) {
                ++readSet;
                auto o = fL.find(kv.first);
                missL += o != fL.end() && o->second.resident ? 0u : 1u;
            }
        std::printf("   (5) the gather first reading 8 frames late: at rest in B %d frames, %zu cards resident, admitted "
                    "%llu | the teleport's read set %zu faces, %zu not resident in the late run\n", l.restB, l.cards.size(),
                    l.admitted, readSet, missL);
        CHECK(okL && l.restB >= 0 && readSet > 0u && missL == 0u,
              "(5) A LATE FIRST READER KEEPS THE DEMAND OPEN FOR ITS OWN SETTLE: the read set resident");
    }

    // (3) the values
    if (mode == "walk") {
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
            const double step = std::ldexp(o, -10);   // the RGBA16F store's step (10-2d)
            const double codes = std::max(0.0, std::fabs(double(a.rgb[k]) - double(b.rgb[k])) - step) / (0.01 * o);
            worst = std::max(worst, codes);
            over += codes > 1.0 ? 1u : 0u;
        }
    }
    std::printf("   (3) the values, %zu cards resident in both: worst %.3f codes, %zu texels over a code\n", common, worst, over);
    CHECK(common > 0u && over == 0u, "(3) WALK = TELEPORT: every card resident in both within a code (10-2e: the hit's level by the camera)");
    }

    // (6) THE HISTORY PART: the walk against the teleport pinned to its placement - the same resident set, 0 texels over
    //     a code
    if (mode == "pinned") {
        const std::map<NodeId, int> idP = key(pnd);
        std::map<std::pair<int, unsigned>, const CardRadiance *> inP;
        for (const CardRadiance &k : pnd.cards) if (idP.count(k.node)) inP[{ idP.at(k.node), k.card }] = &k;
        size_t onlyW = 0, onlyP = 0, commonP = 0, overP = 0;
        double worstP = 0.0;
        // (each card in one set only, NAMED: its object, card, face, size, last read against its run's newest frame, and
        // its face's read rate over the readers' rest-16 in each run - a card at the demand window's edge reads late
        // and rare, a real residency difference reads inside the rest)
        const auto name = [&](const char *which, const Run &r, const Run &other, const CardRadiance &c, int obj) {
            const auto rr = r.restRate.find({ c.node, c.bucket });
            NodeId otherNode = 0;
            for (const auto &kv : (&other == &w ? idW : idP)) if (kv.second == obj) otherNode = kv.first;
            const auto ro = other.restRate.find({ otherNode, c.bucket });
            std::printf("     only in the %s: object %d card %u face %u size %u - last read %llu (its run's newest %llu: %lld "
                        "frames before), last relit %llu; the face's rest-16 read rate %.3f a frame here, %.3f in the other "
                        "run\n", which, obj, c.card, c.bucket, c.size, (unsigned long long)c.lastRead,
                        (unsigned long long)r.newest, (long long)r.newest - (long long)c.lastRead,
                        (unsigned long long)c.lastRelit, rr != r.restRate.end() ? rr->second : -1.0,
                        ro != other.restRate.end() ? ro->second : -1.0);
        };
        // (each run's last rest decision: its room, its admitted and refused arrivals, the refused by object and mark)
        for (const Run *r : { &w, &pnd }) {
            const std::map<NodeId, int> idR = key(*r);
            std::string refused;
            for (const CardCacheStatus::Refused &f : r->refusedCards) {
                char b[96];
                std::snprintf(b, sizeof(b), " [object %d card %u size %u mark %llu]",
                              idR.count(f.node) ? idR.at(f.node) : -1, f.index, f.size, (unsigned long long)f.mark);
                refused += b;
            }
            std::printf("     %s: %llu decision(s); the last one's room %llu texels used, %llu free; %u admitted, %u refused%s\n",
                        r == &w ? "the walk" : "the pinned teleport", (unsigned long long)r->decisions,
                        (unsigned long long)r->decisionUsed, (unsigned long long)r->decisionFree, r->decisionAdmitted,
                        r->decisionRefused, refused.c_str());
        }
        for (const auto &kv : inW)
            if (!inP.count(kv.first)) { ++onlyW; name("walk", w, pnd, *kv.second, kv.first.first); }
        for (const auto &kv : inP)
            if (!inW.count(kv.first)) { ++onlyP; name("pinned", pnd, w, *kv.second, kv.first.first); }
        for (const auto &kv : inW) {
            auto it = inP.find(kv.first);
            if (it == inP.end() || it->second->size != kv.second->size) continue;
            ++commonP;
            const CardRadiance &a = *kv.second, &b = *it->second;
            for (size_t k = 0; k < a.rgb.size(); ++k) {
                const double o = std::max(double(b.rgb[k]), 5.9604645e-6);
                if (!(a.rgb[k] > 0.0f) && !(b.rgb[k] > 0.0f)) continue;
                const double codes =
                    std::max(0.0, std::fabs(double(a.rgb[k]) - double(b.rgb[k])) - std::ldexp(o, -10)) / (0.01 * o);
                worstP = std::max(worstP, codes);
                overP += codes > 1.0 ? 1u : 0u;
            }
        }
        std::printf("   (6) the teleport pinned to the walk's c1 and c2 placement: at rest in A %d, in B %d | resident: the "
                    "walk %zu cards, the pinned teleport %zu (the free teleport %zu) - %zu only in the walk, %zu only in the "
                    "pinned | %zu in both: worst %.3f codes, %zu texels over a code\n",
                    pnd.restA, pnd.restB, w.cards.size(), pnd.cards.size(), t.cards.size(), onlyW, onlyP, commonP, worstP,
                    overP);
        CHECK(okP && pnd.restB >= 0 && onlyW == 0u && onlyP == 0u && commonP > 0u && overP == 0u,
              "(6) THE HISTORY PART: the teleport pinned to the walk's placement holds the walk's resident set and its "
              "values within a code");
    }

    // (8) L1 - A VALUE AT A WORLD POINT IS INDEPENDENT OF A CARD'S RESIDENCY (PHOTON-7 stage 10, the lead's ruling on (3)),
    //     TO WITHIN ITS FACE'S CARD-VOXEL DISAGREEMENT (the bar derived, the lead's ruling on (8)): room B's pillar 8's +x
    //     band card held resident in one pinned run and absent in the other (cards.force); every OTHER card resident in
    //     both, and every reader texel's |delta L| at most the largest |L_card - L_voxel| over the band face's hits (a
    //     reader's radiance moves by its albedo x its share of rays on the face x the face's move, both at most 1). (Red
    //     with the voxel read along the RAY: 98 codes, the pillar's lit -z face read into its +x face; then 5.2 codes with
    //     the read along the hit's normal - against a code, the bar before it was derived. The cause, measured against the
    //     card estimator at 128 rays: both caches' fixed 16-ray quadrature, the voxel side's single surface point -13 %.)
    if (mode == "held") {
        const std::map<NodeId, int> idI = key(hin), idO = key(hout);
        std::map<std::pair<int, unsigned>, const CardRadiance *> inI, inO;
        for (const CardRadiance &k : hin.cards) if (idI.count(k.node)) inI[{ idI.at(k.node), k.card }] = &k;
        for (const CardRadiance &k : hout.cards) if (idO.count(k.node)) inO[{ idO.at(k.node), k.card }] = &k;
        const int heldId = hin.b.pillars.size() > 8u && idI.count(hin.b.pillars[8]) ? idI.at(hin.b.pillars[8]) : -1;
        const std::pair<int, unsigned> heldKey{ heldId, 1u };
        const bool heldIn = inI.count(heldKey) != 0u, heldOut = inO.count(heldKey) == 0u;
        size_t onlyI = 0, onlyO = 0, both = 0, overH = 0;
        double worstH = 0.0, readerMove[3] = { 0.0, 0.0, 0.0 };
        for (const auto &kv : inI)
            if (kv.first != heldKey && !inO.count(kv.first)) ++onlyI;
        for (const auto &kv : inO)
            if (!inI.count(kv.first)) ++onlyO;
        for (const auto &kv : inO) {
            auto it = inI.find(kv.first);
            if (it == inI.end() || it->second->size != kv.second->size) continue;
            ++both;
            const CardRadiance &a = *kv.second, &b = *it->second;
            for (size_t k = 0; k < a.rgb.size(); ++k) {
                const double o = std::max(double(b.rgb[k]), 5.9604645e-6);
                if (!(a.rgb[k] > 0.0f) && !(b.rgb[k] > 0.0f)) continue;
                const double move = std::fabs(double(a.rgb[k]) - double(b.rgb[k]));
                readerMove[k % 3u] = std::max(readerMove[k % 3u], move);
                const double codes = std::max(0.0, move - std::ldexp(o, -10)) / (0.01 * o);
                worstH = std::max(worstH, codes);
                overH += codes > 1.0 ? 1u : 0u;
            }
        }
        // THE FACE'S DISAGREEMENT: the band card's own hits - the rays the held-out run's voxels answered, matched by index
        // to the held-in run's card answers
        double faceGap[3] = { 0.0, 0.0, 0.0 };
        size_t faceRays = 0;
        for (const auto &kv : hout.face) {
            if (kv.second.src != GatherRay::Voxel) continue;
            auto it = hin.face.find(kv.first);
            if (it == hin.face.end() || it->second.src != GatherRay::Card) continue;
            ++faceRays;
            for (int c = 0; c < 3; ++c)
                faceGap[c] = std::max(faceGap[c], std::fabs(double(kv.second.rgb[c]) - double(it->second.rgb[c])));
        }
        bool within = faceRays > 0u;
        for (int c = 0; c < 3; ++c) within = within && readerMove[c] <= faceGap[c];
        std::printf("   (8) the band face's card held resident / absent (pinned): %s / %s | the other cards: %zu only with it, "
                    "%zu only without, %zu in both: worst %.3f codes, %zu texels over a code\n", heldIn ? "resident" : "NOT resident",
                    heldOut ? "absent" : "NOT absent", onlyI, onlyO, both, worstH, overH);
        std::printf("   (8) THE DERIVED BAR: the readers' largest move %.5f %.5f %.5f against the band face's largest card-voxel gap "
                    "%.5f %.5f %.5f (%zu matched hits)\n", readerMove[0], readerMove[1], readerMove[2], faceGap[0], faceGap[1],
                    faceGap[2], faceRays);
        CHECK(okHin && okHout && hin.restB >= 0 && hout.restB >= 0 && heldIn && heldOut && onlyI == 0u && onlyO == 0u &&
                  both > 0u && hin.hitsOk && hout.hitsOk && within,
              "(8) L1: A CARD'S PRESENCE MOVES ITS READERS BY AT MOST ITS FACE'S CARD-VOXEL DISAGREEMENT (the same resident set "
              "otherwise)");
        std::printf("   (8) THE NO POP, at B's rest: the card let in - the largest pixel change in one frame %d codes (%s); held out "
                    "again - %d codes (%s)\n", hout.popIn, hout.popInRest ? "at rest again" : "NOT at rest in 120 frames",
                    hout.popOut, hout.popOutRest ? "at rest again" : "NOT at rest in 120 frames");
        // (DECLARED RED FOR STAGE 8: the readers' view-wide rest keys take a card's writes - its capture, its relights, the
        // departure's re-march - so a held view settles on its EMA while the rounds run; stage 8's per-texel resumes hold
        // a refined cell's picture and hand the re-rest over in N frames)
        CHECK(hout.popIn >= 0 && hout.popOut >= 0 && hout.popIn <= 1 && hout.popOut <= 1,
              "(8) THE NO POP: a card's arrival or departure at rest moves no pixel more than 1 code a frame");
    }

    // (4) THE COMPACTION: from the crowded room (its read set in quarter pages) to room B (whole pages): the arrivals find
    //     no free page, the evictions free scattered quarter-page slots, and the atlas merges the sub-cut pages to free
    //     whole ones - every card read at B resident, every moved card re-captured (none queued at the rest)
    if (mode == "crowded") {
    size_t readC = 0, unreadAtB = 0;
    for (const CardRadiance &k : c.cards) if (readRecently(c, k)) ++readC;
    for (NodeId p : c.b.pillars)
        if (!std::any_of(c.cards.begin(), c.cards.end(), [p](const CardRadiance &k) { return k.node == p; })) ++unreadAtB;
    std::printf("   CROWDED -> B: at rest in A %d frames, in B %d | %zu cards resident, %u pages | admitted %llu, evicted %llu, "
                "compaction moves %llu | %zu read within the window, %zu of B's pillars uncarded\n", c.restA, c.restB,
                c.cards.size(), c.pagesUsed, c.admitted, c.evicted, c.compaction, readC, unreadAtB);
    CHECK(okC && c.restA >= 0 && c.restB >= 0 && c.compaction > 0u && c.evicted > 0u && unreadAtB == 0u,
          "(4) THE ATLAS COMPACTS: the crowded room's quarter pages merged to free whole ones, and room B carded at rest");
    }

    if (mode == "walk") {
        // (7)'s CONVEX EDGE against its reference: the walk's toggling cell at B's rest against the same placement's
        // Jacobi re-solve, within a code (1 % over the store's floor) plus the store's achievable quantum under the
        // iteration (0.5 / (1 - r) steps: 1.5e-7 at the floor at r 0.8)
        bool within = w.edgeOk && w.edgeRefOk;
        for (int k = 0; k < 3 && within; ++k)
            within = std::fabs(double(w.edge[k]) - double(w.edgeRef[k])) <=
                     0.01 * std::max(double(std::fabs(w.edgeRef[k])), double(kGiBrickFloor)) + 1.5e-7;
        std::printf("   (7) the convex edge (%.2f %.2f %.2f) at B's rest: the walk %.6g %.6g %.6g, its Jacobi re-solve %.6g %.6g "
                    "%.6g\n", kEdgeWorld[0], kEdgeWorld[1], kEdgeWorld[2], w.edge[0], w.edge[1], w.edge[2], w.edgeRef[0],
                    w.edgeRef[1], w.edgeRef[2]);
        CHECK(within, "(7) THE CONVEX EDGE HOLDS: the walk's toggling cell within a code of its Jacobi re-solve");
    }
    CHECK(gOrbitOver == 0u,
          "(7) THE STORE AT REST HOLDS: every channel that moves after B's rest is within its brick's rounding bound");

    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
