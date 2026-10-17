// THE RENDER THREAD'S CPU MAP, FRAME-COUNTED (ENGINE-UPGRADE-U4 stage 1, the deep read's premises P1-P3). Each row
// is one phase of the frame at scale, read off the engine itself — the snapshot (the drain) or the test-only inspect
// door (the sun trace, the GPU scene's walk) — never off the monitor, never sampled. LOCKSTEP, Debug, FRAMES.
//
// THE WORLD (the deep read's own, spikes/engine-upgrade/u4/raw/deep-reader-drain10k.js): N carded cubes (0.5 m, six
// box cards each) on a 100-wide grid at a 1.2 m pitch, every one a DOCUMENT node under ONE group node, on a carded
// 150 m floor; one shadow-casting sun; the camera at (0, 40, 90) looking at the origin (the card residency's viewer);
// GI at Epic with the card row on; the editor's eight scene workers. The writes are the HOST'S RECORDS — NodeRecords
// and the change set's `moved`, exactly what the mirror emits: the GROUP ARM writes the group alone (one record, an
// N-node subtree named), a K ARM writes K cubes (K records, K moved). One shared material (the phases measured here
// are per node, not per datablock).
//
//   frame.drain_cost      (--drain 2000, a guard): the drain (OgreEngine::drainFrameInput) and its three parts at
//                         K = 100 and 2,000 records in a 2,000-node world — the counts its law is (every record applied,
//                         every write named), the parts printed.
//   frame.drain_cost_10k  (--drain 10000, `measure`, by hand): the same at K = 100 / 2,000 / 10,000 and the group arm.
//                         THE BASE (3962c2f0b, this fixture): see the stage 1 evidence (spikes/engine-upgrade/u4/stage1);
//                         the deep read's app figures: group 19.7 ms, 100 % 26 ms (name 16 / records 7 / plan 3).
//                         Stage 3's bar: <= 5 ms on the group move, <= 8 ms at 100 %.
//   gi.trace_sun_cost     (--trace-sun, `measure`, by hand): SurfaceCache::traceSun's whole CPU per frame at the 10k
//                         group move and at 12 % movers (P1: every moved still caster against every card). The deep
//                         read's app figures: 64.5 ms (group) / 18.4 ms (12 %) / 64.6 ms (100 %). Stage 2's bar: <= 5 ms
//                         on the group move, <= 2 ms at 12 %.
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
    NodeId group = 0;
    std::vector<NodeId> cubes;
    unsigned long long step = 0;
    int frame = 0;   // the arm's frame counter (the writes alternate by it)
};

Vec3 cubeAt(int i, float lift)
{
    const float half = 0.5f * kPitch * float(kSide);
    return Vec3(float(i % kSide) * kPitch - half, kCube * 0.5f + lift, float(i / kSide) * kPitch - half);
}

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
    enginetest::addDirectionalLight(s, Vec3(-0.35f, -1.0f, -0.45f), 3.0f);
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
    for (int i = 0; i < n; ++i) {
        const NodeId c = s->createDocumentNode(reserveNodeId(), w.group);
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
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;
    gi.numBounces = 3;
    gi.cards = true;
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

/// One frame of an arm's writes: the group alone (k < 0) or the first k cubes, alternating by 1 cm in y.
void writeFrame(World &w, int k)
{
    FrameInput in = stepRecord(w);
    FrameInputScene part;
    part.scene = w.scene;
    const float lift = (w.frame++ % 2) ? 0.01f : 0.0f;
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;
    gi.numBounces = 3;
    gi.cards = true;
    part.gi = gi;
    if (k < 0) {
        NodeRecord r;
        r.id = w.group;
        r.fields = NodeRecord::Trs;
        r.position = Vec3(0.0f, lift, 0.0f);
        r.rotation = Quat(0, 0, 0, 1);
        part.nodes.push_back(r);
        part.changes.moved.push_back(w.group);
    } else {
        for (int i = 0; i < k && i < int(w.cubes.size()); ++i) {
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
    if (k < 0) std::snprintf(buf, n, "group");
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
    for (int k : arms) {
        std::vector<double> whole, name, records, plan;
        bool counts = true, parts = true;
        unsigned recs = 0, moved = 0;
        for (int f = 0; f < warm + measured; ++f) {
            writeFrame(w, k);
            const FrameInputStatus &st = w.e->published().input;
            if (f < warm) continue;
            whole.push_back(st.drainMicros);
            name.push_back(st.drainNameMicros);
            records.push_back(st.drainRecordsMicros);
            plan.push_back(st.drainPlanMicros);
            recs = st.nodeRecords;
            moved = st.changesMoved;
            const unsigned want = k < 0 ? 1u : unsigned(k);
            if (st.nodeRecords != want || st.changesMoved != want) counts = false;
            if (st.drainNameMicros + st.drainRecordsMicros + st.drainPlanMicros > st.drainMicros + 0.5) parts = false;
        }
        char b[32];
        const Stats a = stats(whole), nm = stats(name), rc = stats(records), pl = stats(plan);
        std::printf("CPU_MAP %s arm=%s world=%d frames=%d drain_ms=%.3f (min %.3f max %.3f) name_ms=%.3f records_ms=%.3f "
                    "plan_ms=%.3f other_ms=%.3f\n", row, armName(k, b, sizeof b), n, measured, a.median / 1000.0,
                    a.min / 1000.0, a.max / 1000.0, nm.median / 1000.0, rc.median / 1000.0, pl.median / 1000.0,
                    (a.median - nm.median - rc.median - pl.median) / 1000.0);
        CHECK(counts, "%s arm %s: every frame's drain applied every record and named every write (%u records, %u moved)",
              row, armName(k, b, sizeof b), recs, moved);
        CHECK(parts, "%s arm %s: the three parts are inside the drain's whole on every frame", row,
              armName(k, b, sizeof b));
    }
    return 0;
}

// ---- gi.trace_sun_cost -----------------------------------------------------------------------------------------------
int traceSunRow(World &w, int n)
{
    const inspect::SceneEcho *echo = inspect::scene(w.scene);
    if (!echo) { std::printf("FAIL: the scene's probes\n"); return 1; }
    const int warm = 3, measured = 15;
    for (int k : { -1, n * 12 / 100 }) {
        std::vector<double> us;
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
            moves = std::max(moves, t.casterMoves);
            cards = t.cards;
            hits = std::max(hits, t.casterHits);
            for (int b = 0; b < 8; ++b) { digest ^= (t.casterHitDigest >> (8 * b)) & 0xFFu; digest *= 1099511628211ull; }
        }
        char b[32];
        const Stats a = stats(us);
        std::printf("CPU_MAP gi.trace_sun_cost arm=%s world=%d frames=%d trace_sun_ms=%.3f (min %.3f max %.3f) "
                    "caster_moves=%u cards=%u hits=%u hit_digest=%016llx\n", armName(k, b, sizeof b), n, measured,
                    a.median / 1000.0, a.min / 1000.0, a.max / 1000.0, moves, cards, hits, digest);
        CHECK(ran, "gi.trace_sun_cost arm %s: the sun trace ran on every measured frame", armName(k, b, sizeof b));
        const unsigned want = k < 0 ? unsigned(n) : unsigned(k);
        CHECK(moves >= want && cards > 0u, "gi.trace_sun_cost arm %s: %u still casters moved a frame against %u cards "
              "(want %u moves)", armName(k, b, sizeof b), moves, cards, want);
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
        std::vector<double> name, compare, compose, notify;
        unsigned composed = 0, shares = 0;
        bool walked = true;
        for (int f = 0; f < warm + measured; ++f) {
            const unsigned long long walks0 = echo->gpuWalk().walks;
            writeFrame(w, k);
            const inspect::SceneEcho::GpuWalk g = echo->gpuWalk();
            if (f < warm) continue;
            if (g.walks == walks0) walked = false;
            name.push_back(g.nameMicros);
            compare.push_back(g.compareMicros);
            compose.push_back(g.composeMicros);
            notify.push_back(g.notifyMicros);
            composed = std::max(composed, g.slotsComposed);
            shares = g.shares;
        }
        char b[32];
        const Stats nm = stats(name), cm = stats(compare), co = stats(compose), no = stats(notify);
        const double total = nm.median + cm.median + co.median;
        std::printf("CPU_MAP engine.gpuscene_compose arm=%s world=%d frames=%d compose_ms=%.3f (min %.3f max %.3f) "
                    "notify_ms=%.3f (share of the compose %.2f) compare_ms=%.3f name_ms=%.3f serial_share=%.2f "
                    "slots_composed=%u shares=%u\n",
                    armName(k, b, sizeof b), n, measured, co.median / 1000.0, co.min / 1000.0, co.max / 1000.0,
                    no.median / 1000.0, co.median > 0.0 ? no.median / co.median : 0.0, cm.median / 1000.0,
                    nm.median / 1000.0, total > 0.0 ? co.median / total : 0.0, composed, shares);
        CHECK(walked, "engine.gpuscene_compose arm %s: the GPU scene walked on every measured frame",
              armName(k, b, sizeof b));
        CHECK(composed >= unsigned(n), "engine.gpuscene_compose arm %s: every cube's slot composed (%u of %d)",
              armName(k, b, sizeof b), composed, n);
        CHECK(shares == kWorkers + 1u, "engine.gpuscene_compose arm %s: the compare ran on the pool's %u shares",
              armName(k, b, sizeof b), shares);
    }
    return 0;
}

}   // namespace

int main(int argc, char **argv)
{
    std::string mode = argc > 1 ? argv[1] : "";
    const int n = argc > 2 ? std::atoi(argv[2]) : 10000;
    if (mode != "--drain" && mode != "--trace-sun" && mode != "--compose") {
        std::printf("usage: test_engine_cpu_map --drain|--trace-sun|--compose [nodes]\n");
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
    else rc = composeRow(w, n);
    w.view->setScene(nullptr);
    w.e->destroyScene(w.scene);
    w.e->destroyView(w.view);
    enginetest::frames(w.e, 1);
    std::printf(rc || failures ? "cpu_map %s: FAILED\n" : "cpu_map %s: PASS\n", mode.c_str());
    return rc || failures ? 1 : 0;
}
