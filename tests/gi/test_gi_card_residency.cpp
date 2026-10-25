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
//       the unread cards (the teleport's fits whole), and every FACE READ AT B AT LEAST lambda_min TIMES A FRAME
//       (GiQualityFacts::cardDemandHitFloor) in either run (the measuring arm's per-face counts over the 30 frames after
//       the arrival - every read of the face, its card's or the hit that marked it: Scene::readCardFaceDemand) has its
//       card resident in the other - walk = teleport on THE
//       RESIDENT SET (the spec's acceptance) down to the demand settle's law (GiQualityFacts::cardDemandSettleFrames:
//       a face read less may arrive in one run and not the other - counted, printed);
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
//   (4) THE ATLAS COMPACTS: a third run teleports from a CROWDED room (a 7 x 3 x 7 grid of 0.8 m cubes: its read set
//       fills the atlas with quarter-page cards) to room B (whole pages): the arrivals find no free page and the atlas
//       merges its sub-cut pages (compaction moves > 0, each moved card re-captured), and B comes to rest carded.
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
    unsigned long long frame = 0ull, newest = 0ull, evicted = 0ull, admitted = 0ull, compaction = 0ull;
    unsigned pagesUsed = 0u;
    int restA = -1, restB = -1;
    Room a, b;
    /// each cascade's placement at B (the pinned arm (6) takes the walk's)
    std::vector<Vec3> builtAtB;
    /// (7)'s CONVEX EDGE (the toggle the readback named, PHOTON-7 stage 10): cascade 0's front at the world point of the
    /// walk's toggling cell c0 67,93,84 - the top front edge of a court box - at B's rest (ok: the cascade holds it)
    bool edgeOk = false, edgeRefOk = false;
    float edge[3] = { 0.f, 0.f, 0.f }, edgeRef[3] = { 0.f, 0.f, 0.f };
};
/// the edge's world point: the walk's cascade 0 cell 67,93,84 at B (the readback's, spikes/photon-7/stage10/10-2)
static const float kEdgeWorld[3] = { 12.30f, 4.02f, 4.10f };

bool runOnce(Engine *e, bool walk, Run &out, int &frames, bool crowded = false, int gatherLate = 0,
             const std::vector<Vec3> *pinTo = nullptr) {
    const char *name = crowded ? "residency_crowded" : walk ? "residency_walk" : gatherLate ? "residency_late"
                                                                         : pinTo ? "residency_pinned"
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
    // A STILL PICTURE GATHERS (View::setOffscreenContract; PHOTON-7 7-5: the gather is recorded only for a view whose
    // chain gathers, and a Live view takes the field's answer)
    view->setOffscreenContract(OffscreenContract::StillPicture);
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
    // (6)'s ARM: cascades 1 and 2 pinned to the walk's final placement (built there, no scroll history), 0 and 3 free
    if (pinTo && pinTo->size() >= 4u) {
        GiPlacementPin pin;
        pin.on = true;
        pin.cascadeBuiltCam = *pinTo;
        pin.cascadeFree = { 1u, 0u, 0u, 1u };
        scene->setGiPlacementPin(pin);
    }
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
    out.restB = settle();
    if (!crowded && out.b.pillars.size() > 8u) {   // DIAG (to remove): room B pillar 8's +x face - its card, the voxels there
        const NodeId pn = out.b.pillars[8];
        const Vec3 c = out.b.centre;
        const float fx = c.x + 2.4f + 0.2f;   // the face's plane
        double cr[3] = {}, ci[3] = {};
        int nc = 0;
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) {
                CardSample cs;
                if (scene->readCardTexel(pn, 1u, 0.125f + 0.25f * float(a), 0.125f + 0.25f * float(b), cs) && cs.ok && cs.lit) {
                    for (int k = 0; k < 3; ++k) { cr[k] += cs.radiance[k]; ci[k] += cs.indirect[k]; }
                    ++nc;
                }
            }
        std::printf("   DIAGF card (node %u card 1): %d texels lit, mean radiance %.6f %.6f %.6f, indirect %.6f %.6f %.6f\n",
                    unsigned(pn), nc, nc ? cr[0] / nc : 0.0, nc ? cr[1] / nc : 0.0, nc ? cr[2] / nc : 0.0, nc ? ci[0] / nc : 0.0,
                    nc ? ci[1] / nc : 0.0, nc ? ci[2] / nc : 0.0);
        for (int casc = 0; casc < 2; ++casc) {
            GiVoxelVolume vl, va;
            if (!scene->giVoxelLight(casc, vl) || !scene->giVoxelVolume(casc, va) || !vl.available) continue;
            // the column of voxels holding the face: x the cell the plane falls in and the one inside, y 0.3..3.2, z the face
            const int qx = int(std::floor((fx - vl.origin[0]) / vl.cell[0]));
            double sF[3] = {}, sB[3] = {}, sL[3] = {}, sD[3] = {}, cov = 0.0;
            int n = 0;
            for (int dx = -1; dx <= 0; ++dx)
                for (float y = 0.3f; y < 3.2f; y += vl.cell[1])
                    for (float z = c.z + 2.25f; z < c.z + 2.55f; z += vl.cell[2]) {
                        const int q[3] = { qx + dx, int(std::floor((y - vl.origin[1]) / vl.cell[1])),
                                           int(std::floor((z - vl.origin[2]) / vl.cell[2])) };
                        if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[0] >= vl.width || q[1] >= vl.height || q[2] >= vl.depth) continue;
                        const size_t i = (size_t(q[2]) * size_t(vl.height) + size_t(q[1])) * size_t(vl.width) + size_t(q[0]);
                        const double a = va.albedo.size() > 4 * i + 3 ? va.albedo[4 * i + 3] : 0.0;
                        if (!(a > 0.0)) continue;
                        for (int k = 0; k < 3; ++k) {
                            sF[k] += vl.lightFront[4 * i + k] / a;
                            sB[k] += vl.lightBack[4 * i + k] / a;
                            sL[k] += vl.light[4 * i + k] / a;
                            sD[k] += va.direct.size() > 4 * i + 3 ? va.direct[4 * i + k] / a : 0.0;
                        }
                        cov += a;
                        ++n;
                    }
            {   // the face's centre voxels in rgb, and the +x wall's inner face beside it: its voxels and its card
                const int qy = int(std::floor((1.75f - vl.origin[1]) / vl.cell[1]));
                const auto rgbAt = [&](float x, float y, float z, const char *what) {
                    const int q[3] = { int(std::floor((x - vl.origin[0]) / vl.cell[0])), int(std::floor((y - vl.origin[1]) / vl.cell[1])),
                                       int(std::floor((z - vl.origin[2]) / vl.cell[2])) };
                    if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[0] >= vl.width || q[1] >= vl.height || q[2] >= vl.depth) return;
                    const size_t i = (size_t(q[2]) * size_t(vl.height) + size_t(q[1])) * size_t(vl.width) + size_t(q[0]);
                    const double a = va.albedo.size() > 4 * i + 3 ? va.albedo[4 * i + 3] : 0.0;
                    std::printf("   DIAGF c%d %s (%d,%d,%d) c %.2f front/c %.4f %.4f %.4f back/c %.4f %.4f %.4f direct/c %.4f %.4f %.4f n %.2f %.2f %.2f\n",
                                casc, what, q[0], q[1], q[2], a, a > 0 ? vl.lightFront[4 * i] / a : 0.0, a > 0 ? vl.lightFront[4 * i + 1] / a : 0.0,
                                a > 0 ? vl.lightFront[4 * i + 2] / a : 0.0, a > 0 ? vl.lightBack[4 * i] / a : 0.0,
                                a > 0 ? vl.lightBack[4 * i + 1] / a : 0.0, a > 0 ? vl.lightBack[4 * i + 2] / a : 0.0,
                                a > 0 && va.direct.size() > 4 * i + 2 ? va.direct[4 * i] / a : 0.0,
                                a > 0 && va.direct.size() > 4 * i + 2 ? va.direct[4 * i + 1] / a : 0.0,
                                a > 0 && va.direct.size() > 4 * i + 2 ? va.direct[4 * i + 2] / a : 0.0,
                                vl.normal[4 * i] * 2.0 - 1.0, vl.normal[4 * i + 1] * 2.0 - 1.0, vl.normal[4 * i + 2] * 2.0 - 1.0);
                };
                (void)qy;
                rgbAt(fx - 0.01f, 1.75f, c.z + 2.4f, "pillar +x face");
                rgbAt(c.x + 4.0f + 0.01f, 1.75f, c.z + 2.4f, "+x wall inner");
                rgbAt(c.x + 4.0f - 0.01f, 1.75f, c.z + 2.4f, "+x wall inner-air");
                if (casc == 0) {
                    CardSample ws;
                    if (scene->readCardAt(Vec3(c.x + 4.0f, 1.75f, c.z + 2.4f), Vec3(-1.0f, 0.0f, 0.0f), ws) && ws.ok)
                        std::printf("   DIAGF +x wall card at the pillar: radiance %.4f %.4f %.4f indirect %.4f %.4f %.4f albedo %.3f %.3f %.3f\n",
                                    ws.radiance[0], ws.radiance[1], ws.radiance[2], ws.indirect[0], ws.indirect[1], ws.indirect[2],
                                    ws.albedo[0], ws.albedo[1], ws.albedo[2]);
                    CardSample ps;
                    if (scene->readCardAt(Vec3(fx, 1.75f, c.z + 2.4f), Vec3(1.0f, 0.0f, 0.0f), ps) && ps.ok)
                        std::printf("   DIAGF pillar +x card centre: radiance %.4f %.4f %.4f indirect %.4f %.4f %.4f albedo %.3f %.3f %.3f\n",
                                    ps.radiance[0], ps.radiance[1], ps.radiance[2], ps.indirect[0], ps.indirect[1], ps.indirect[2],
                                    ps.albedo[0], ps.albedo[1], ps.albedo[2]);
                }
            }
            if (casc == 0) {   // per voxel at mid height, across the face in z, the face cell and the one inside, and one out
                const int qy = int(std::floor((1.75f - vl.origin[1]) / vl.cell[1]));
                for (int dx = -1; dx <= 1; ++dx) {
                    std::printf("   DIAGF c0 x %d:", qx + dx);
                    for (float z = c.z + 2.1f; z < c.z + 2.7f; z += vl.cell[2]) {
                        const int q[3] = { qx + dx, qy, int(std::floor((z - vl.origin[2]) / vl.cell[2])) };
                        if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[0] >= vl.width || q[1] >= vl.height || q[2] >= vl.depth) continue;
                        const size_t i = (size_t(q[2]) * size_t(vl.height) + size_t(q[1])) * size_t(vl.width) + size_t(q[0]);
                        const double a = va.albedo.size() > 4 * i + 3 ? va.albedo[4 * i + 3] : 0.0;
                        const float nx = vl.normal.size() > 4 * i ? vl.normal[4 * i] * 2.0f - 1.0f : 0.0f;
                        const float nz = vl.normal.size() > 4 * i + 2 ? vl.normal[4 * i + 2] * 2.0f - 1.0f : 0.0f;
                        std::printf(" [z%d c%.2f L%.3f D%.3f n(%.2f,%.2f) cP%.2f/%.2f/%.2f cN%.2f/%.2f/%.2f]", q[2], a,
                                    a > 0 ? vl.lightFront[4 * i] / a : 0.0, a > 0 && va.direct.size() > 4 * i ? va.direct[4 * i] / a : 0.0,
                                    nx, nz, va.coverageP[4 * i], va.coverageP[4 * i + 1], va.coverageP[4 * i + 2],
                                    va.coverageN[4 * i], va.coverageN[4 * i + 1], va.coverageN[4 * i + 2]);
                    }
                    std::printf("\n");
                }
            }
            std::printf("   DIAGF voxels c%d (cell %.3f, x cells %d..%d): %d occupied, mean c %.3f | light/c %.6f %.6f %.6f | front/c %.6f "
                        "%.6f %.6f | back/c %.6f %.6f %.6f | direct/c %.6f %.6f %.6f\n", casc, vl.cell[0], qx - 1, qx, n,
                        n ? cov / n : 0.0, n ? sL[0] / n : 0.0, n ? sL[1] / n : 0.0, n ? sL[2] / n : 0.0, n ? sF[0] / n : 0.0,
                        n ? sF[1] / n : 0.0, n ? sF[2] / n : 0.0, n ? sB[0] / n : 0.0, n ? sB[1] / n : 0.0, n ? sB[2] / n : 0.0,
                        n ? sD[0] / n : 0.0, n ? sD[1] / n : 0.0, n ? sD[2] / n : 0.0);
        }
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
    const bool ok = scene->readCardRadiance(out.cards, out.frame);
    for (const CardRadiance &c : out.cards) out.newest = std::max(out.newest, c.lastRead);
    if (walk && !crowded && !pinTo && !gatherLate && out.edgeOk) {
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

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-card-residency-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    e->setArm("cards.demandCount", 1.0);   // the readers' hits counted: the read rates (2) reads
    // (THE MEASURING HOOK of 10-2e's margin: JAH_HIT_LEVEL_MARGIN=<cells> sets the door gi.hitLevelMargin for the run)
    if (const char *m = std::getenv("JAH_HIT_LEVEL_MARGIN")) e->setArm("gi.hitLevelMargin", std::atof(m));
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));

    Run w, t, c, l, pnd;
    const bool okW = runOnce(e, true, w, frames);
    const bool okT = runOnce(e, false, t, frames);
    const bool okP = runOnce(e, false, pnd, frames, false, 0, &w.builtAtB);
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

    // (2) the read set resident in both: every FACE read at B at least once a frame (the settle law's floor) in either
    //     run is resident in the other (a face's cards: the card of its bucket - every box face one card here)
    std::map<std::pair<int, unsigned>, const CardRadiance *> inW, inT;
    for (const CardRadiance &c : w.cards) if (idW.count(c.node)) inW[{ idW.at(c.node), c.card }] = &c;
    for (const CardRadiance &c : t.cards) if (idT.count(c.node)) inT[{ idT.at(c.node), c.card }] = &c;
    // each run's faces by the run-independent id: its read rate, and whether a card of it is resident
    struct Face { double rate = 0.0; bool resident = false; };
    const auto faces = [](const Run &r, const std::map<NodeId, int> &id) {
        std::map<std::pair<int, unsigned>, Face> f;
        for (const auto &kv : r.rate)
            if (id.count(kv.first.first)) f[{ id.at(kv.first.first), kv.first.second }].rate = kv.second;
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
                std::printf("   %s: id %d face %u read %.3f hits a frame at B and NOT resident in it\n", name,
                            kv.first.first, kv.first.second, rt);
            if (!there && rt > 0.0 && kv.second.resident)
                std::printf("   %s only: id %d face %u read %.3f hits a frame at B (the %s: %.3f)%s\n", name,
                            kv.first.first, kv.first.second, rt, otherName, o != theirs.end() ? o->second.rate : 0.0,
                            rt >= floor ? " - AT THE FLOOR OR MORE" : rt >= 0.5 * floor ? " (the band floor/2 .. floor)" : "");
        }
    };
    side(fW, fT, "walk", "teleport", readW, missW, bandW);
    side(fT, fW, "teleport", "walk", readT, missT, bandT);
    std::printf("   the read set (faces read at B at least %.2f hits a frame, the settle law's floor): walk %zu (%zu not "
                "resident in the teleport), teleport %zu (%zu not resident in the walk) | in the band floor/2 .. floor "
                "(the law leaves to chance): walk %zu, teleport %zu\n", floor, readW, missW, readT, missT, bandW, bandT);
    CHECK(w.evicted > 0u && readW > 0u && readT > 0u && missW == 0u && missT == 0u,
          "(2) THE ATLAS MADE ROOM (the walk's unread cards evicted) AND THE READ SET IS RESIDENT IN BOTH");

    // (5) THE LATE FIRST READER: the teleport's read set resident in the late run
    {
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

    // (6) THE HISTORY PART: the walk against the teleport pinned to its placement - the same resident set, 0 texels over
    //     a code
    {
        const std::map<NodeId, int> idP = key(pnd);
        std::map<std::pair<int, unsigned>, const CardRadiance *> inP;
        for (const CardRadiance &k : pnd.cards) if (idP.count(k.node)) inP[{ idP.at(k.node), k.card }] = &k;
        size_t onlyW = 0, onlyP = 0, commonP = 0, overP = 0;
        double worstP = 0.0;
        for (const auto &kv : inW)
            if (!inP.count(kv.first)) ++onlyW;
        for (const auto &kv : inP)
            if (!inW.count(kv.first)) ++onlyP;
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
        {   // DIAG (to remove): the cards only in one, and the common cards moved most
            for (const auto &kv : inP)
                if (!inW.count(kv.first))
                    std::printf("   DIAG6 only in the pinned: node#%d card %u bucket %u size %u lastRelit %llu\n", kv.first.first,
                                kv.first.second, kv.second->bucket, kv.second->size, kv.second->lastRelit);
            for (const auto &kv : inW)
                if (!inP.count(kv.first))
                    std::printf("   DIAG6 only in the walk: node#%d card %u bucket %u size %u\n", kv.first.first, kv.first.second,
                                kv.second->bucket, kv.second->size);
            std::vector<std::pair<double, std::pair<int, unsigned>>> worstCards;
            for (const auto &kv : inW) {
                auto it = inP.find(kv.first);
                if (it == inP.end() || it->second->size != kv.second->size) continue;
                double wc = 0.0, sa = 0.0, sb = 0.0, wa = 0.0, wb = 0.0;
                size_t wk = 0;
                for (size_t k = 0; k < kv.second->rgb.size(); ++k) {
                    const double o = std::max(double(it->second->rgb[k]), 5.9604645e-6);
                    const double cc = std::fabs(double(kv.second->rgb[k]) - double(it->second->rgb[k])) / (0.01 * o);
                    if (cc > wc) { wc = cc; wa = kv.second->rgb[k]; wb = it->second->rgb[k]; wk = k; }
                    sa += kv.second->rgb[k]; sb += it->second->rgb[k];
                }
                if (wc > 2.0) {
                    const size_t S = kv.second->size, tx = (wk / 3) % S, ty = (wk / 3) / S, ch = wk % 3;
                    std::printf("   DIAG6 worst texel of node#%d card %u: (texel %zu,%zu ch %zu) walk %.6f pinned %.6f | neighbours walk/pinned:",
                                kv.first.first, kv.first.second, tx, ty, ch, wa, wb);
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const long X = long(tx) + dx, Y = long(ty) + dy;
                            if (X < 0 || Y < 0 || X >= long(S) || Y >= long(S) || (dx && dy)) continue;
                            const size_t j = (size_t(Y) * S + size_t(X)) * 3 + ch;
                            std::printf(" (%+d,%+d) %.4f/%.4f", dx, dy, kv.second->rgb[j], it->second->rgb[j]);
                        }
                    std::printf("\n");
                }
                worstCards.push_back({ wc, kv.first });
                if (wc > 1.0)
                    std::printf("   DIAG6 moved: node#%d card %u bucket %u worst %.1f codes, mean walk %.6f pinned %.6f\n",
                                kv.first.first, kv.first.second, kv.second->bucket, wc, sa / double(kv.second->rgb.size()),
                                sb / double(it->second->rgb.size()));
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

    {
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
