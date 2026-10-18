// THE RENDER THREAD'S CPU MAP, FRAME-COUNTED (ENGINE-UPGRADE-U4 stage 1, the deep read's premises P1-P3). Each row
// is one phase of the frame at scale, read off the engine itself — the snapshot (the drain) or the test-only inspect
// door (the sun trace, the GPU scene's walk) — never off the monitor, never sampled. LOCKSTEP, Debug, FRAMES.
//
// THE WORLD (the deep read's own, spikes/engine-upgrade/u4/raw/deep-reader-drain10k.js): N carded cubes (0.5 m, six
// box cards each) on a 100-wide grid at a 1.2 m pitch, every one a DOCUMENT node under ONE group node, on a carded
// 150 m floor; one shadow-casting sun, low enough that a cube shades its neighbours' cards; the camera at (0, 40, 90) looking at the origin (the card residency's viewer);
// GI at Epic with the card row on; the editor's eight scene workers. The writes are the HOST'S RECORDS — NodeRecords
// and the change set's `moved`, exactly what the mirror emits: the GROUP ARM writes the group alone (one record, an
// N-node subtree named), a K ARM writes K cubes (K records, K moved). One shared material (the phases measured here
// are per node, not per datablock).
//
//   frame.drain_cost      (--drain 2000, a guard): the drain (OgreEngine::drainFrameInput) and its parts at
//                         K = 100 and 2,000 records in a 2,000-node world — the counts its law is (every record applied,
//                         every write named), the parts printed; and the plan (`engine.plan`, after the scene graph since
//                         stage 3: the geometry signature, planFrame's walk / boxes / pick) with its two guards (the
//                         incremental signature equals the whole sum; one box per moved root marks no cascade fewer).
//   frame.drain_cost_10k  (--drain 10000, `measure`, by hand): the same at K = 100 / 2,000 / 10,000 and the group arm.
//                         Stage 1 (this fixture): group 16.09 ms, 100 % 23.60 ms. Stage 3: planFrame <= 5 ms on the group
//                         move; the drain's bars are the lead's, from the per-record table (stage 3's report).
//   gi.trace_sun_cost     (--trace-sun, `measure`, by hand): SurfaceCache::traceSun's whole CPU per frame at the 10k
//                         group move and at 12 % movers (P1: every moved still caster against every card). The deep
//                         read's app figures: 64.5 ms (group) / 18.4 ms (12 %) / 64.6 ms (100 %). Stage 2's bar: <= 5 ms
//                         on the group move, <= 2 ms at 12 %; and the arm cards.sunGridParity's 0 misses on every arm
//                         (the grid's searches equal the exhaustive search card for card).
//   engine.gpuscene_compose (--compose, `measure`, by hand): the GPU scene's walk by phase (the named slots, the pool's
//                         compare, the frame thread's compose) at the 10k group move and at 100 % movers (P2: the
//                         compose's serial share). The deep read's app figures: 30.7 ms (group) / 90.4 ms (100 %), 83-88 %
//                         of it the serial compose. Stage 4's bar: the compose <= 10 ms (group), <= 25 ms (100 %).
//
// Each prints `CPU_MAP <row> arm=<arm> ...` lines with the median of the measured frames (and min/max), after warm-up
// frames that are never counted. The rows skip cleanly where the device has no ray tier (the cards need it).
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

namespace {

constexpr int kSide = 100;          // the grid's width (the deep read's 100 x 100)
constexpr float kPitch = 1.2f;
constexpr float kCube = 0.5f;
constexpr unsigned kWorkers = 8u;   // the editor's scene (bridge/sceneworkerthreads.h, the Primary tier)

struct Stats {
    double median = 0.0, min = 0.0, max = 0.0;
};
Stats stats(std::vector<double> v)
{
    Stats s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.min = v.front();
    s.max = v.back();
    return s;
}

struct World {
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *scene = nullptr;
    NodeId group = 0, subgroup = 0;
    std::vector<NodeId> cubes;
    unsigned long long step = 0;
    int frame = 0;   // the arm's frame counter (the writes alternate by it)
};

Vec3 cubeAt(int i, float lift)
{
    const float half = 0.5f * kPitch * float(kSide);
    return Vec3(float(i % kSide) * kPitch - half, kCube * 0.5f + lift, float(i / kSide) * kPitch - half);
}

/// The scene's GI, every record (the editor's: Epic, the card row on; the scheduler plans on every record that carries it).
GiParams epicGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;
    gi.numBounces = 3;
    gi.cards = true;
    return gi;
}

/// THE CHECKERBOARD ARM's key: every cube whose grid cell is "black" moves; its white neighbours stand, their cards
/// landed, so the moved casters' shadows fall on cards the sun trace must re-trace (a non-empty hit set).
constexpr int kChecker = -2;
bool black(int i) { return ((i % kSide) + (i / kSide)) % 2 == 0; }
/// THE MOVERS' ARM: the black cubes of the 24 rows nearest the viewer (1,200) declared MOVABLE (the movers' channel:
/// steps 3 and 3b of the sun trace, their footprints against the landed cards of their white neighbours) and written
/// every frame.
constexpr int kMovers = -3;
constexpr int kMoverCount = 1200;
bool moverCube(int i, int n) { return i >= n - 2 * kMoverCount && black(i); }
/// THE SUBGROUP ARM (decision 11 b): the movers' 1,200 cubes hang under their OWN group node (a child of the world's
/// group, in the 10k world), and the arm writes that group alone — a group move over a landed, standing world: the
/// white neighbours' cards and the wide plate's stay landed, so the still casters' search is a real one.
constexpr int kSubgroup = -4;

FrameInput stepRecord(World &w)
{
    FrameInput in;
    in.frameDelta = 1.0f / 60.0f;
    in.stepSerial = ++w.step;
    in.stepPeriod = 1.0f / 60.0f;
    return in;
}

/// The world's build: the group and its N cubes as document nodes placed by ONE record (parent + transform), the
/// floor and the sun, GI with the card row; then frames until the GI is at rest and — where the world reaches the
/// viewer's residency (`cards`: the 10k grid's near rows do, a 2,000-node grid's far corner does not) — the cards are
/// resident and the capture queue is empty.
bool build(World &w, int n, bool cards)
{
    Engine *e = w.e;
    w.view = e->createOffscreenView("cpumap", 256u, 256u, Colour(0, 0, 0));
    w.scene = e->createScene("cpumap", kWorkers);
    if (!w.view || !w.scene) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return false; }
    w.view->setScene(w.scene);
    if (!e->rayQueryAvailable() || !e->rayTracing()) return true;   // main() says so and skips
    Scene *s = w.scene;
    // A LOW SUN (14 degrees): a cube's shadow reaches its neighbours' cards across the 0.7 m gap, so the still casters'
    // hit set is not empty — the identity a faster search is held to has something to hold.
    enginetest::addDirectionalLight(s, Vec3(-1.0f, -0.25f, -0.3f), 3.0f);
    w.view->setShadows(true);
    // THE VIEWPORT'S POST CHAIN on the offscreen view (the editor screenshot's door): the ray tier runs, and with it the
    // card cache (the cards run only where rays run).
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    w.view->setPostFx(fx);
    PbrParams p;
    p.albedo = Colour(0.6f, 0.55f, 0.5f);
    p.roughness = 0.8f;
    const MaterialId mat = s->createPbrMaterial(p);
    MeshData md = enginetest::unitCubeMesh();
    md.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(md);
    {
        const NodeId floor = s->createNode();
        s->attachMesh(floor, mesh, mat);
        enginetest::setNodeScale(s, floor, Vec3(150.0f, 0.2f, 150.0f));
        enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    }
    if (n >= 10000) {
        // THE WIDE PLATE (decision 11 b): a 40 m x 30 m carded slab under the near rows, inside the residency — its
        // cards are far wider than the median card (the grid's `wide` list), and the moved cubes shade them.
        const NodeId plate = s->createNode();
        s->attachMesh(plate, mesh, mat);
        enginetest::setNodeScale(s, plate, Vec3(40.0f, 0.1f, 30.0f));
        enginetest::setNodePosition(s, plate, Vec3(0.0f, -0.05f, 52.0f));
    }
    w.group = s->createDocumentNode(reserveNodeId());
    FrameInput in = stepRecord(w);
    FrameInputScene part;
    part.scene = s;
    {
        NodeRecord r;
        r.id = w.group;
        r.fields = NodeRecord::Trs;
        r.position = Vec3(0, 0, 0);
        r.rotation = Quat(0, 0, 0, 1);
        part.nodes.push_back(r);
    }
    if (n >= 10000) {
        w.subgroup = s->createDocumentNode(reserveNodeId(), w.group);
        NodeRecord r;
        r.id = w.subgroup;
        r.fields = NodeRecord::Trs;
        r.position = Vec3(0, 0, 0);
        r.rotation = Quat(0, 0, 0, 1);
        part.nodes.push_back(r);
    }
    for (int i = 0; i < n; ++i) {
        const NodeId parent = (w.subgroup && moverCube(i, n)) ? w.subgroup : w.group;
        const NodeId c = s->createDocumentNode(reserveNodeId(), parent);
        if (!c) { std::printf("FAIL: document node %d: %s\n", i, e->lastError().c_str()); return false; }
        s->attachMesh(c, mesh, mat);
        w.cubes.push_back(c);
        NodeRecord r;
        r.id = c;
        r.fields = NodeRecord::Trs;
        r.position = cubeAt(i, 0.0f);
        r.rotation = Quat(0, 0, 0, 1);
        r.scale = Vec3(kCube, kCube, kCube);
        part.nodes.push_back(r);
    }
    part.changes.complete = false;   // the first record: everything is new
    const GiParams gi = epicGi();
    part.gi = gi;
    in.scenes.push_back(std::move(part));
    enginetest::testCameraLookAt(w.view, Vec3(0.0f, 40.0f, 90.0f), Vec3(0.0f, 0.0f, 0.0f));
    enginetest::frames(e, 1, in);
    int f = 0;
    for (; f < 900; ++f) {
        FrameInput still = stepRecord(w);
        FrameInputScene sp;
        sp.scene = s;
        sp.gi = gi;
        still.scenes.push_back(std::move(sp));
        enginetest::frames(e, 1, still);
        const GiStatus &g = s->published().gi;
        if (f >= 30 && g.giAtRest && (!cards || g.cards.cardsResident > 0u) && g.cards.queueLength == 0u) break;
    }
    const GiStatus &g = s->published().gi;
    std::printf("WORLD: %d carded cubes under one group + a carded floor, GI Epic + cards: settled in %d frames "
                "(giAtRest %d, cards resident %u on %u instances, queue %u)\n",
                n, f, int(g.giAtRest), g.cards.cardsResident, g.cards.instancesResident, g.cards.queueLength);
    return true;
}


/// Still frames until the cards are resident and the card queue is empty (every card a moved instance threw back is
/// captured again; a mobility edge's GI rebuild re-made the cache): each arm starts from a landed state.
void settleCards(World &w)
{
    for (int f = 0; f < 900; ++f) {
        const CardCacheStatus &c = w.scene->published().gi.cards;
        if (f >= 2 && c.cardsResident > 0u && c.queueLength == 0u) return;
        FrameInput still = stepRecord(w);
        FrameInputScene sp;
        sp.scene = w.scene;
        sp.gi = epicGi();
        still.scenes.push_back(std::move(sp));
        enginetest::frames(w.e, 1, still);
    }
}

/// One frame of an arm's writes: the group alone (k = -1), the black cubes (kChecker) or the first k cubes,
/// alternating by 1 cm in y.
void writeFrame(World &w, int k)
{
    FrameInput in = stepRecord(w);
    FrameInputScene part;
    part.scene = w.scene;
    const float lift = (w.frame++ % 2) ? 0.01f : 0.0f;
    const GiParams gi = epicGi();
    part.gi = gi;
    if (k == -1 || k == kSubgroup) {
        NodeRecord r;
        r.id = k == -1 ? w.group : w.subgroup;
        r.fields = NodeRecord::Trs;
        r.position = Vec3(0.0f, lift, 0.0f);
        r.rotation = Quat(0, 0, 0, 1);
        part.nodes.push_back(r);
        part.changes.moved.push_back(r.id);
    } else {
        for (int i = 0; i < int(w.cubes.size()); ++i) {
            // (the grid's last rows face the viewer)
            if (k == kChecker ? !black(i) : k == kMovers ? !moverCube(i, int(w.cubes.size())) : i >= k) continue;
            NodeRecord r;
            r.id = w.cubes[size_t(i)];
            r.fields = NodeRecord::Trs;
            r.position = cubeAt(i, lift);
            r.rotation = Quat(0, 0, 0, 1);
            r.scale = Vec3(kCube, kCube, kCube);
            part.nodes.push_back(r);
            part.changes.moved.push_back(r.id);
        }
    }
    in.scenes.push_back(std::move(part));
    enginetest::frames(w.e, 1, in);
}

const char *armName(int k, char *buf, size_t n)
{
    if (k == kChecker) std::snprintf(buf, n, "checker");
    else if (k == kMovers) std::snprintf(buf, n, "movers%d", kMoverCount);
    else if (k == kSubgroup) std::snprintf(buf, n, "subgroup%d", kMoverCount);
    else if (k < 0) std::snprintf(buf, n, "group");
    else std::snprintf(buf, n, "k%d", k);
    return buf;
}

// ---- frame.drain_cost ------------------------------------------------------------------------------------------------
int drainRow(World &w, int n)
{
    const char *row = n >= 10000 ? "frame.drain_cost_10k" : "frame.drain_cost";
    std::vector<int> arms;
    for (int k : { 100, 2000, 10000 }) if (k <= n) arms.push_back(k);
    if (n >= 10000) arms.push_back(-1);
    const int warm = 3, measured = n >= 10000 ? 15 : 20;
    const inspect::SceneEcho *echo = inspect::scene(w.scene);
    if (!echo) { std::printf("FAIL: the scene's probes\n"); return 1; }
    for (int k : arms) {
        std::vector<double> whole, name, records, plan, planFrame, boxesUs, pickUs;
        unsigned boxes = 0;
        unsigned long long planDigest = 1469598103934665603ull;   // FNV-1a over the frames' pending sets and picks
        bool counts = true, parts = true, lookups = true;
        unsigned recs = 0, moved = 0, recLookups = 0, visits = 0, nameLookups = 0;
        // THE NODES THE FRAME'S RECORDS MOVE: each record's own node at a k arm; the group's whole subtree (the group,
        // the subgroup where the world has one, every cube) on the group arm.
        const unsigned wantVisits = k < 0 ? unsigned(n) + 1u + (w.subgroup ? 1u : 0u) : unsigned(k);
        for (int f = 0; f < warm + measured; ++f) {
            writeFrame(w, k);
            const FrameInputStatus &st = w.e->published().input;
            if (f < warm) continue;
            whole.push_back(st.drainMicros);
            name.push_back(st.drainNameMicros);
            records.push_back(st.drainRecordsMicros);
            const inspect::SceneEcho::GiPlan gp = echo->giPlan();
            plan.push_back(gp.signatureMicros);
            planFrame.push_back(gp.micros);
            boxesUs.push_back(gp.boxesMicros);
            pickUs.push_back(gp.pickMicros);
            boxes = std::max(boxes, gp.boxes);
            for (unsigned long long v : { (unsigned long long)gp.pending, (unsigned long long)(long long)gp.rebuild })
                for (int b = 0; b < 8; ++b) { planDigest ^= (v >> (8 * b)) & 0xFFu; planDigest *= 1099511628211ull; }
            recs = st.nodeRecords;
            moved = st.changesMoved;
            const unsigned want = k < 0 ? 1u : unsigned(k);
            if (st.nodeRecords != want || st.changesMoved != want) counts = false;
            recLookups = st.drainRecordLookups;
            visits = st.drainNameVisits;
            nameLookups = st.drainNameLookups;
            if (st.drainRecordLookups != st.nodeRecords || st.drainNameVisits != wantVisits ||
                st.drainNameLookups != st.drainNameVisits) lookups = false;
            if (st.drainNameMicros + st.drainRecordsMicros > st.drainMicros + 0.5) parts = false;
        }
        char b[32];
        const Stats a = stats(whole), nm = stats(name), rc = stats(records), pl = stats(plan), pf = stats(planFrame);
        const Stats bx = stats(boxesUs), pk = stats(pickUs);
        const double per = k < 0 ? double(n) : double(std::max(k, 1));   // the records' nodes (the group's subtree)
        std::printf("CPU_MAP %s arm=%s world=%d frames=%d drain_ms=%.3f (min %.3f max %.3f) name_ms=%.3f records_ms=%.3f "
                    "other_ms=%.3f | per node: name_us=%.3f records_us=%.3f | plan: signature_ms=%.3f planFrame_ms=%.3f "
                    "(boxes %.3f, pick %.3f) boxes=%u plan_digest=%016llx\n", row, armName(k, b, sizeof b), n, measured,
                    a.median / 1000.0, a.min / 1000.0, a.max / 1000.0, nm.median / 1000.0, rc.median / 1000.0,
                    (a.median - nm.median - rc.median) / 1000.0, nm.median / per, rc.median / double(k < 0 ? 1 : k),
                    pl.median / 1000.0, pf.median / 1000.0, bx.median / 1000.0, pk.median / 1000.0, boxes, planDigest);
        CHECK(counts, "%s arm %s: every frame's drain applied every record and named every write (%u records, %u moved)",
              row, armName(k, b, sizeof b), recs, moved);
        CHECK(parts, "%s arm %s: the drain's parts are inside its whole on every frame", row,
              armName(k, b, sizeof b));
        // THE COUNTS AT EVERY ARM (decision 15 — what catches a scan creeping back, whatever the box's load): one
        // index lookup per record, each node of the moved subtrees visited once by the naming pass, one lookup per
        // visit.
        CHECK(lookups, "%s arm %s: one lookup per record (%u for %u records), every moved node visited once (%u of %u), "
              "one lookup per visit (%u)", row, armName(k, b, sizeof b), recLookups, recs, visits, wantVisits,
              nameLookups);
        // THE µs BARS, PER ARM (decision 15): the quiet-box medians x 1.25 (the load spread), measured 2026-10-18
        // 09:00-09:57 at load 3.0-4.5 — k100 name 0.284-0.289 / records 0.182-0.190 -> 0.36 / 0.24; k2000 0.284-0.302 /
        // 0.177-0.189 -> 0.355 / 0.221 (the lead's); k10000 0.377-0.383 / 0.301-0.308 -> 0.48 / 0.385; the group move
        // 0.298-0.301 per node -> 0.37. (The old "10k <= 1.25 x the 2k cost" clause is DELETED: its premise was false.
        // The group arm walks the same 10k nodes from one record flat at 0.30 us/node, so the k10000 rise is the
        // working set of 10k separate records leaving the caches, not more work per record. Applying the records in
        // SoA slot order instead of arrival order measured 0.49 vs 0.30 us/record at k10000 — arrival order is 88 %
        // slot order already and the sort costs more than it saves — so it is not built.)
        // ON THE MEASURE ROW ONLY (frame.drain_cost_10k, run by hand on a quiet box): the 2k guard row in the gate keeps
        // the counts above — a µs bar there would read the box's load, not the drain.
        if (n >= 10000) {
            const double nameBar = k < 0 ? 0.37 : k <= 100 ? 0.36 : k <= 2000 ? 0.355 : 0.48;
            const double recBar = k <= 100 ? 0.24 : k <= 2000 ? 0.221 : 0.385;
            const double nameUs = nm.median / per, recUs = rc.median / double(k < 0 ? 1 : k);
            CHECK(nameUs <= nameBar, "%s arm %s: naming %.3f us/node <= %.3f", row, armName(k, b, sizeof b), nameUs,
                  nameBar);
            if (k > 0)
                CHECK(recUs <= recBar, "%s arm %s: the records %.3f us/record <= %.3f", row, armName(k, b, sizeof b),
                      recUs, recBar);
        }
        // THE PLAN'S GUARDS (stage 3), three more frames of the arm with both arms on: the incremental geometry
        // signature equals the whole sum, and one box per moved subtree root marks no cascade the items' own
        // boxes reach fewer (the union's extra printed).
        const inspect::SceneEcho::GiPlan g0 = echo->giPlan();
        w.e->setArm("gi.geomSigParity", 1.0);
        w.e->setArm("gi.rootBoxParity", 1.0);
        for (int f = 0; f < 3; ++f) writeFrame(w, k);
        w.e->setArm("gi.geomSigParity", 0.0);
        w.e->setArm("gi.rootBoxParity", 0.0);
        writeFrame(w, k);
        const inspect::SceneEcho::GiPlan g1 = echo->giPlan();
        if (k < 0) {
            const inspect::SceneEcho::GpuWalk gw = echo->gpuWalk();
            std::printf("CPU_MAP %s the direct indexes: Ogre ids reach %llu (%llu pages of 4096), registry ids reach %llu "
                        "(%llu pages)\n", row, gw.ogreIdReach, gw.ogreIdPages, gw.nodeIdReach, gw.nodeIdPages);
        }
        std::printf("CPU_MAP %s arm=%s guards: signature %llu passes checked, %llu misses | roots %llu marks checked, "
                    "%llu misses, %llu extra cascades\n", row, armName(k, b, sizeof b), g1.sigChecks - g0.sigChecks,
                    g1.sigMisses - g0.sigMisses, g1.rootChecks - g0.rootChecks, g1.rootMisses - g0.rootMisses,
                    g1.rootExtras - g0.rootExtras);
        CHECK(g1.sigChecks >= g0.sigChecks + 3u && g1.sigMisses == g0.sigMisses, "%s arm %s: the incremental "
              "geometry signature equals the whole sum on every pass", row, armName(k, b, sizeof b));
        CHECK(g1.rootMisses == g0.rootMisses, "%s arm %s: the roots' boxes mark every cascade the items' own boxes "
              "reach (%llu marks checked)", row, armName(k, b, sizeof b), g1.rootChecks - g0.rootChecks);
        // planFrame's bars: <= 5 ms on the group move (decision 8), the plan reading the boxes the scene graph made;
        // at k10000 the median + 0.5 = 8.6 ms (decision 15: measured 7.83-8.10 at load 3-4.5, the 10k moved leaves'
        // boxes folding into the collapsed union).
        if (k < 0) CHECK(pf.median / 1000.0 <= 5.0, "%s arm %s: planFrame %.3f ms <= 5 ms", row, armName(k, b, sizeof b),
                         pf.median / 1000.0);
        if (k == 10000) CHECK(pf.median / 1000.0 <= 8.6, "%s arm %s: planFrame %.3f ms <= 8.6 ms", row,
                              armName(k, b, sizeof b), pf.median / 1000.0);
    }
    return 0;
}

// ---- gi.trace_sun_cost -----------------------------------------------------------------------------------------------
int traceSunRow(World &w, int n)
{
    const inspect::SceneEcho *echo = inspect::scene(w.scene);
    if (!echo) { std::printf("FAIL: the scene's probes\n"); return 1; }
    const int warm = 3, measured = 15;
    bool deathDone = false;
    // checker: the hit set's arm (the still casters' shadows on their standing neighbours' cards); 12 %: the deep
    // read's movers12 shape (1,200 still casters written, far from the landed cards); the movers' channel (steps 3 and
    // 3b); the subgroup: a group move over a landed, standing world; the group move last (every cube's instance moves:
    // their cards back on the queue, the wide plate's standing).
    for (int k : { kChecker, n * 12 / 100, kMovers, kSubgroup, -1 }) {
        if (k == kMovers) {
            for (int i = 0; i < int(w.cubes.size()); ++i)
                if (moverCube(i, int(w.cubes.size()))) w.scene->setNodeMovable(w.cubes[size_t(i)], true);
        }
        settleCards(w);
        std::vector<double> us, hook, stepOne, step3, step3b, still, step2, step4, job;
        unsigned moves = 0, cards = 0, hits = 0;
        unsigned long long digest = 1469598103934665603ull;   // FNV-1a over every measured frame's hit-set digest
        bool ran = true;
        for (int f = 0; f < warm + measured; ++f) {
            const unsigned long long calls0 = echo->traceSun().calls;
            writeFrame(w, k);
            const inspect::SceneEcho::TraceSun t = echo->traceSun();
            if (f < warm) continue;
            if (t.calls == calls0) ran = false;
            us.push_back(t.micros);
            hook.push_back(t.hookMicros);
            stepOne.push_back(t.stepOneMicros);
            step3.push_back(t.step3Micros);
            step3b.push_back(t.step3bMicros);
            still.push_back(t.stillMicros);
            step2.push_back(t.step2Micros);
            step4.push_back(t.step4Micros);
            job.push_back(t.traceJobMicros);
            moves = std::max(moves, t.casterMoves);
            cards = t.cards;
            hits = std::max(hits, t.casterHits);
            for (int b = 0; b < 8; ++b) { digest ^= (t.casterHitDigest >> (8 * b)) & 0xFFu; digest *= 1099511628211ull; }
        }
        // THE GRID'S GUARD: three more frames of the arm with `cards.sunGridParity` on — every search of the frame
        // repeated exhaustively, every card whose answer differs a miss — and `cards.nullMove` on (a BOX_NULL still
        // caster: the unbounded and unplaceable searches); on the subgroup arm also a DEATH (a standing cube removed:
        // its last box is a still caster's move). 0 misses is the law.
        const inspect::SceneEcho::TraceSun p0 = echo->traceSun();
        unsigned wide = 0;
        bool deathFrame = false;
        // THE DOOMED CUBE (the subgroup arm): a white cube of the near rows (standing, landed, shading its
        // neighbours), marked one frame before its death with a light mask the inspect door reads back — the door
        // answers a missing record with the default mask, so the death is proven by the read, not by the post.
        int doomed = -1;
        constexpr unsigned kDoomedMask = 0x3u;
        if (k == kSubgroup && !deathDone)
            for (int i = int(w.cubes.size()) - 1; i >= 0 && doomed < 0; --i)
                if (!moverCube(i, int(w.cubes.size())) && i >= int(w.cubes.size()) - 2 * kMoverCount) doomed = i;
        NodeId doomedId = doomed >= 0 ? w.cubes[size_t(doomed)] : NodeId(0);
        unsigned long long scans0 = 0ull, scansFull0 = 0ull;
        w.e->setArm("cards.sunGridParity", 1.0);
        w.e->setArm("cards.nullMove", 1.0);
        for (int f = 0; f < 3; ++f) {
            if (doomed >= 0 && f == 0) w.scene->setNodeLightMask(doomedId, kDoomedMask);
            if (doomed >= 0 && f == 1) {
                CHECK(echo->nodeLightMask(doomedId) == kDoomedMask, "gi.trace_sun_cost: the doomed cube's record "
                      "reads back before its death (mask %#x)", echo->nodeLightMask(doomedId));
                scans0 = w.scene->published().gi.giScans;
                scansFull0 = w.scene->published().gi.giScansFull;
                w.scene->removeNode(doomedId);
                w.cubes.erase(w.cubes.begin() + doomed);
                deathFrame = true;
            }
            writeFrame(w, k);
            wide = std::max(wide, echo->traceSun().wideCards);
            if (deathFrame) {
                // THE DEATH, ASSERTED (decision 13.7): the record is gone (the door's default answer), and the GI scan
                // walked this frame over the names alone (one scan, not a full one).
                const GiStatus &gd = w.scene->published().gi;
                deathDone = echo->nodeLightMask(doomedId) == 0xFFFFFFFFu;
                CHECK(deathDone, "gi.trace_sun_cost: the doomed cube's record is gone after its frame (mask %#x)",
                      echo->nodeLightMask(doomedId));
                CHECK(gd.giScans == scans0 + 1u && gd.giScansFull == scansFull0,
                      "gi.trace_sun_cost: the death frame's GI scan walked the names (%llu scans, %llu full)",
                      gd.giScans - scans0, gd.giScansFull - scansFull0);
                // THE DEATH'S FRAME (decision 12.5): the record leaves the one name list and no consumer owes a full
                // pass for it — the GPU scene's walk compares the named slots, not the whole index.
                const inspect::SceneEcho::GpuWalk gw = echo->gpuWalk();
                const double scanMs = w.scene->published().gi.giScanMicros / 1000.0;
                const double gpuMs = (gw.nameMicros + gw.compareMicros + gw.composeMicros) / 1000.0;
                std::printf("CPU_MAP gi.trace_sun_cost the death frame: GI scan %.3f ms + the GPU scene's walk %.3f ms "
                            "(%u slots compared of %d) = %.3f ms\n", scanMs, gpuMs, gw.slotsCompared, n,
                            scanMs + gpuMs);
                CHECK(gw.slotsCompared < unsigned(n) / 2u, "gi.trace_sun_cost: a death owes no full walk (%u slots "
                      "compared, the arm's own names)", gw.slotsCompared);
                deathFrame = false;
            }
        }
        w.e->setArm("cards.nullMove", 0.0);
        w.e->setArm("cards.sunGridParity", 0.0);
        writeFrame(w, k);
        const inspect::SceneEcho::TraceSun p1 = echo->traceSun();
        if (k == kMovers) {
            for (int i = 0; i < int(w.cubes.size()); ++i)
                if (moverCube(i, int(w.cubes.size()))) w.scene->setNodeMovable(w.cubes[size_t(i)], false);
        }
        char b[32];
        const Stats a = stats(us), hk = stats(hook), s1 = stats(stepOne), s3 = stats(step3), s3b = stats(step3b);
        const Stats st = stats(still), s2 = stats(step2), s4 = stats(step4), jb = stats(job);
        const GiStatus &g = w.scene->published().gi;
        const double named = hk.median + s1.median + st.median + s2.median + s3.median + s3b.median + s4.median + jb.median;
        std::printf("CPU_MAP gi.trace_sun_cost arm=%s world=%d frames=%d trace_sun_ms=%.3f (min %.3f max %.3f) "
                    "(producer %.3f, step 1 %.3f, still trace %.3f [%.3f us/hit], step 2 %.3f, step 3 %.3f, step 3b %.3f, "
                    "step 4 %.3f, trace job %.3f; unnamed %.3f) caster_moves=%u cards=%u hits=%u hit_digest=%016llx "
                    "mover_casters=%u mover_traced_last=%u\n",
                    armName(k, b, sizeof b), n, measured, a.median / 1000.0, a.min / 1000.0, a.max / 1000.0,
                    hk.median / 1000.0, s1.median / 1000.0, st.median / 1000.0, hits ? st.median / double(hits) : 0.0,
                    s2.median / 1000.0, s3.median / 1000.0, s3b.median / 1000.0, s4.median / 1000.0, jb.median / 1000.0,
                    (a.median - named) / 1000.0, moves, cards, hits, digest, g.cards.moverCasters,
                    g.cards.moverTracedLastFrame);
        CHECK(ran, "gi.trace_sun_cost arm %s: the sun trace ran on every measured frame", armName(k, b, sizeof b));
        CHECK(p1.parityChecks >= p0.parityChecks + 3u && p1.parityMisses == p0.parityMisses,
              "gi.trace_sun_cost arm %s: the sun-space grid's searches equal the exhaustive search card for card, a "
              "BOX_NULL move included (%llu calls checked, %llu misses, %u wide cards in its grids)",
              armName(k, b, sizeof b), p1.parityChecks - p0.parityChecks, p1.parityMisses - p0.parityMisses, wide);
        if (k == kSubgroup) CHECK(wide > 0u, "gi.trace_sun_cost arm %s: the wide plate's cards were searched as "
                                  "wide (%u)", armName(k, b, sizeof b), wide);
        if (k == kSubgroup) CHECK(deathDone, "gi.trace_sun_cost arm %s: a standing caster died under the guard",
                                  armName(k, b, sizeof b));
        // THE BARS — ONE FORMULA FOR EVERY ARM (decision 12.3): floor = producer + step 1 + hits x c_hit (+ the mover
        // path's named steps), bar = floor + 0.5. c_hit = 0.75 us/hit (the still trace's per-hit term: 1.08 ms / 1428
        // hits on the checker and the subgroup, measured 0.74-0.75 at load 4, 2026-10-18 09:0x). k1200 0.63 + 0.5 =
        // 1.13; subgroup 4.46 + 1.07 + 0.5 = 6.03; checker 5.83 + 1.07 + 0.5 = 7.40; group 5.54 + 0.04 + 0.5 = 6.08;
        // the movers: producer + steps 2, 3, 3b, 4 + the trace job, named = 8.16 (unnamed 0.15) + 0.5 = 8.66.
        {
            const double bar = k == kChecker ? 7.40 : k == kMovers ? 8.66 : k == kSubgroup ? 6.03 : k == -1 ? 6.08 : 1.13;
            CHECK(a.median / 1000.0 <= bar, "gi.trace_sun_cost arm %s: the sun trace's CPU %.3f ms <= %.2f ms",
                  armName(k, b, sizeof b), a.median / 1000.0, bar);
        }
        if (k == kMovers) {
            CHECK(g.cards.moverCasters >= unsigned(kMoverCount) && cards > 0u, "gi.trace_sun_cost arm %s: %u movers "
                  "on the movers' channel against %u cards", armName(k, b, sizeof b), g.cards.moverCasters, cards);
        } else {
            const unsigned want = k == kChecker ? unsigned(n / 2) : k == kSubgroup ? unsigned(kMoverCount)
                                : k < 0 ? unsigned(n) - 1u : unsigned(k);
            CHECK(moves >= want && cards > 0u, "gi.trace_sun_cost arm %s: %u still casters moved a frame against %u "
                  "cards (want %u moves)", armName(k, b, sizeof b), moves, cards, want);
        }
    }
    return 0;
}

// ---- engine.gpuscene_compose -----------------------------------------------------------------------------------------
int composeRow(World &w, int n)
{
    const inspect::SceneEcho *echo = inspect::scene(w.scene);
    if (!echo) { std::printf("FAIL: the scene's probes\n"); return 1; }
    const int warm = 3, measured = 15;
    for (int k : { -1, n }) {
        // THE NOTIFY SHARE AS A PAIRED ARM (decision 11 d): blocks of frames with the change feed's observers told and
        // muted (gpuscene.observersOff), paired A B B A in one process (the measuring rule); the observers' cost = the medians'
        // difference. The notify timer (gpuscene.walkTiming) runs in its own block only: never a clock read per
        // notify in the measured ones.
        std::vector<double> name, compare, compose, composeOff, notify, walkUs, replay;
        unsigned composed = 0, shares = 0;
        bool walked = true;
        for (int f = 0; f < warm; ++f) writeFrame(w, k);
        const int blocks = 4, perBlock = measured / 3 + 1;
        for (int blk = 0; blk < 2 * blocks; ++blk) {
            const bool muted = (blk % 4) == 1 || (blk % 4) == 2;   // A B B A, A B B A
            w.e->setArm("gpuscene.observersOff", muted ? 1.0 : 0.0);
            writeFrame(w, k);   // the arm latches at this frame's top: one settling frame, not counted
            for (int f = 0; f < perBlock; ++f) {
                const unsigned long long walks0 = echo->gpuWalk().walks;
                writeFrame(w, k);
                const inspect::SceneEcho::GpuWalk g = echo->gpuWalk();
                if (g.walks == walks0) walked = false;
                (muted ? composeOff : compose).push_back(g.composeMicros);
                if (!muted) {
                    name.push_back(g.nameMicros);
                    compare.push_back(g.compareMicros);
                    replay.push_back(g.replayMicros);
                    walkUs.push_back(g.compareMicros + g.composeMicros);   // the task (compare + the shares' compose) + the serial part
                }
                composed = std::max(composed, g.slotsComposed);
                shares = g.shares;
            }
        }
        w.e->setArm("gpuscene.observersOff", 0.0);
        w.e->setArm("gpuscene.walkTiming", 1.0);
        for (int f = 0; f < perBlock + 1; ++f) {
            writeFrame(w, k);
            if (f) notify.push_back(echo->gpuWalk().notifyMicros);
        }
        w.e->setArm("gpuscene.walkTiming", 0.0);
        writeFrame(w, k);
        char b[32];
        const Stats nm = stats(name), cm = stats(compare), co = stats(compose), cf = stats(composeOff),
                    no = stats(notify), wk = stats(walkUs), rp = stats(replay);
        const double total = nm.median + cm.median + co.median;
        const double observers = co.median - cf.median;
        std::printf("CPU_MAP engine.gpuscene_compose arm=%s world=%d frames=%d+%d compose_ms=%.3f (min %.3f max %.3f) "
                    "compose_observers_off_ms=%.3f observers_ms=%.3f (share of the compose %.2f; the timed arm's "
                    "notify_ms %.3f) compare_ms=%.3f name_ms=%.3f serial_share=%.2f slots_composed=%u shares=%u\n",
                    armName(k, b, sizeof b), n, int(compose.size()), int(composeOff.size()), co.median / 1000.0,
                    co.min / 1000.0, co.max / 1000.0, cf.median / 1000.0, observers / 1000.0,
                    co.median > 0.0 ? observers / co.median : 0.0, no.median / 1000.0, cm.median / 1000.0,
                    nm.median / 1000.0, total > 0.0 ? co.median / total : 0.0, composed, shares);
        std::printf("CPU_MAP engine.gpuscene_compose arm=%s the walk (stage 4): compare + compose %.3f ms (min %.3f max "
                    "%.3f) = the task %.3f (the shares' compare and compose) + the serial part %.3f (the replay %.3f)\n",
                    armName(k, b, sizeof b), wk.median / 1000.0, wk.min / 1000.0, wk.max / 1000.0, cm.median / 1000.0,
                    co.median / 1000.0, rp.median / 1000.0);
        CHECK(walked, "engine.gpuscene_compose arm %s: the GPU scene walked on every measured frame",
              armName(k, b, sizeof b));
        // STAGE 4'S BAR (decision 16.4: the serial replay is the floor): floor + (the parallel part) / 8 + 2 ms, from the
        // before numbers (decision 15, load 3-4.5): the serial compose 15.1 ms of which the observers 8.6 (the replay's
        // floor) -> 8.6 + (15.1 - 8.6) / 8 + 2 = 11.4 ms for the walk's compare + compose, at the group move and k10000.
        CHECK(wk.median / 1000.0 <= 11.4, "engine.gpuscene_compose arm %s: the walk's compare + compose %.3f ms <= 11.4",
              armName(k, b, sizeof b), wk.median / 1000.0);
        CHECK(composed >= unsigned(n), "engine.gpuscene_compose arm %s: every cube's slot composed (%u of %d)",
              armName(k, b, sizeof b), composed, n);
        CHECK(shares == kWorkers + 1u, "engine.gpuscene_compose arm %s: the compare ran on the pool's %u shares",
              armName(k, b, sizeof b), shares);
    }
    return 0;
}

// ---- the drag (CASCADE-DRAG-STARVE-1's premise, a measurement) ------------------------------------------------------
// One cube brought in front of the camera (inside every cascade), then dragged 1 cm a frame for 120 frames by the
// host's records (one record, one moved root a frame); each cascade's rebuilds over the drag printed. The debt: the
// scheduler spends one rebuild a frame on the innermost pending cascade, and a drag marks every cascade that holds it
// each frame — so the outer ones starve until the release.
int dragArm(World &w)
{
    const NodeId cube = w.cubes.front();
    const auto place = [&](float x) {
        FrameInput in = stepRecord(w);
        FrameInputScene part;
        part.scene = w.scene;
        part.gi = epicGi();
        NodeRecord r;
        r.id = cube;
        r.fields = NodeRecord::Trs;
        r.position = Vec3(x, 38.0f, 84.0f);   // 7.5 m in front of the camera at (0, 40, 90)
        r.rotation = Quat(0, 0, 0, 1);
        r.scale = Vec3(kCube, kCube, kCube);
        part.nodes.push_back(r);
        part.changes.moved.push_back(cube);
        in.scenes.push_back(std::move(part));
        enginetest::frames(w.e, 1, in);
    };
    place(0.0f);
    settleCards(w);
    for (int f = 0; f < 120; ++f) {   // the settle after the placement: every cascade rebuilt for it
        FrameInput still = stepRecord(w);
        FrameInputScene sp;
        sp.scene = w.scene;
        sp.gi = epicGi();
        still.scenes.push_back(std::move(sp));
        enginetest::frames(w.e, 1, still);
    }
    const std::vector<GiStatus::CascadeStatus> c0 = w.scene->published().gi.cascades;
    for (int f = 1; f <= 120; ++f) place(0.01f * float(f));
    const std::vector<GiStatus::CascadeStatus> c1 = w.scene->published().gi.cascades;
    unsigned starved = 0;
    std::printf("CPU_MAP drag frames=120 rebuilds per cascade:");
    for (size_t i = 0; i < c1.size() && i < c0.size(); ++i) {
        const unsigned long long r = c1[i].rebuilds - c0[i].rebuilds;
        std::printf(" c%zu=%llu (pending %d)", i, r, c1[i].pending);
        if (r == 0ull) ++starved;
    }
    std::printf(" | starved %u of %zu\n", starved, c1.size());
    return 0;
}

}   // namespace

int main(int argc, char **argv)
{
    std::string mode = argc > 1 ? argv[1] : "";
    const int n = argc > 2 ? std::atoi(argv[2]) : 10000;
    if (mode != "--drain" && mode != "--trace-sun" && mode != "--compose" && mode != "--drag") {
        std::printf("usage: test_engine_cpu_map --drain|--trace-sun|--compose|--drag [nodes]\n");
        return 2;
    }
    std::printf("== the render thread's CPU map, %s at %d nodes\n", mode.c_str(), n);
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-engine-cpu-map-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    World w;
    w.e = engine.get();
    w.e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    if (!build(w, n, n >= 10000)) return 1;
    if (!w.e->rayQueryAvailable() || !w.e->rayTracing()) {
        std::printf("ok: no ray-query device here — the cards need the ray tier; the row skips cleanly\n");
        return 0;
    }
    int rc = 0;
    if (mode == "--drain") rc = drainRow(w, n);
    else if (mode == "--trace-sun") rc = traceSunRow(w, n);
    else if (mode == "--drag") rc = dragArm(w);
    else rc = composeRow(w, n);
    w.view->setScene(nullptr);
    w.e->destroyScene(w.scene);
    w.e->destroyView(w.view);
    enginetest::frames(w.e, 1);
    std::printf(rc || failures ? "cpu_map %s: FAILED\n" : "cpu_map %s: PASS\n", mode.c_str());
    return rc || failures ? 1 : 0;
}
