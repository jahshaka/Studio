// The SCENE-GRAPH BENCHMARK — a MEASURE (SPECS/SCENEGRAPH_SPEC.md; DOCS/PROCESS.md: a row over its budget is a
// `measure`, run at the sprint's close). It prints what the document -> engine scene graph costs at three scales and
// asserts only its OWN physics: every metric exists, its dispersion holds (cv/rcv), the settle slice's worst stays
// under kSettleSliceMaxMs. It compares against NO recorded file (ENGINE-UPGRADE-1 stage 2 deleted the §6 baseline
// comparison and baseline-linux.json: an old-file wall-clock bar is forbidden). Its COUNTS laws live in guards:
// mirror.reparent_local (a reparent costs the fragment, never the scene).
//
// WHAT IT MEASURES (§6's four metrics, plus two free ones worth having):
//
//   a.transform_update_dynamic   document edit on the DYNAMIC 20% of N nodes,
//                                through SceneMirror::sync(), to a rendered
//                                frame — i.e. "engine-visible", not "pushed".
//   a.transform_update_all       the same edit applied to ALL N nodes (§6 reads
//                                literally "a document edit on N nodes"; both
//                                shapes are recorded so v1 can compare either).
//   a.sync_dynamic / a.sync_all  the same two, sync only, render excluded — the
//                                document→engine half on its own, which is the
//                                half v1 deletes.
//   b.idle_tick                  work per tick on an IDLE scene: sync + render
//                                with no document edit at all. Today this is
//                                never free (audit F1: the mirror pushes every
//                                node's transform and visibility every frame,
//                                unconditionally) — which is the point.
//   b.idle_sync                  the sync half of that tick. Measured AFTER the
//                                mirror's static settle has drained: a settle
//                                in flight is warm-up, not steady state.
//   s.settle_slice               the settle itself, its OWN series: every sync
//                                that ran a settle slice while the scene went
//                                from all-dynamic (a') back to settled, as
//                                percentiles + max of the sync's THREAD CPU
//                                TIME, the worst bounded by kSettleSliceMaxMs
//                                (not by the cv/rcv bars — a settle is a
//                                transient, not a steady state).
//                                THE BOUND IS ON THE SETTLE'S COST PER SYNC: the
//                                work a slice adds to a frame, which the calling
//                                thread's CPU clock measures. Not on wall time:
//                                the wall max of ~1,600 Debug syncs on a
//                                non-realtime scheduler also counts preemptions
//                                and page faults that are not the settle's work
//                                (0.37-0.40 ms four runs, 0.52 once, p99 0.34),
//                                so a wall bar has a floor flake rate by
//                                construction. The WALL series is printed beside
//                                it (s.settle_slice_wall, not asserted) so a
//                                divergence shows in every record; the
//                                frame-hitch question is the idle dispersion
//                                bars' (wall, unchanged).
//   s.settle_render              the render after each slice (reported, unbounded).
//   u.unsettled_render           the COST OF NOT SETTLING: renderOneFrame alone
//                                with every node dynamic (after a'), beside the
//                                settled render (b.idle_tick - b.idle_sync).
//   b.render_stats               the engine's own RenderStats after ~90 idle
//                                frames (frameMs/p95/draws/batches/triangles).
//                                READ `draws` AS OGRE MEANS IT: with the Hlms
//                                pipeline's indirect drawing one draw command
//                                covers many items, so 50k cubes report ~11
//                                draws and 530k triangles. `triangles` is the
//                                geometry number to compare; `p95Ms` comes out
//                                of Ogre's 0.4-fps bucket histogram and is
//                                coarse by construction. Recorded unmodified.
//   c.reparent_500               a 500-node subtree moved between two parents,
//                                synced and rendered.
//   d.anim_rig_200               a 200-node property-animation rig: one
//                                updateAnimation(t) + sync + render per tick.
//   e.build_doc / e.first_sync   one-shot: building the document, and the first
//                                sync that creates every engine object. Not
//                                gated, recorded because scene-open cost is the
//                                number a user feels and v1 changes it.
//
// THE FRAMESTATS DEVIATION, STATED. §6 says "work-per-tick via `renderStats` on
// an idle scene". The application's honest work-per-tick number lives in
// `EngineRenderDriver::Stats::workMs` (src/viewport/enginerenderdriver.h) —
// Studio-side, behind Qt widgets and the whole app target, not linkable from a
// headless suite that links IrisGL + JahshakaEngine only. So b.idle_tick is the
// steady-clock equivalent measured around exactly what the driver times (the
// host's per-tick work: sync + renderOneFrame), and the engine's own
// `Engine::renderStats()` is recorded beside it, unmodified. Both surfaces are
// therefore in the file; neither is invented.
//
// THE SYNTHETIC SCENES. N ∈ {1000, 10000, 50000} nodes, 80% static-intent /
// 20% dynamic-intent, laid out on a cube lattice so the camera really sees them
// (real frustum culling, real draw calls) and shaped as an 8-ary tree so
// hierarchy depth is real too (depth 4 at 1k, 6 at 50k). Interior nodes are
// plain SceneNodes, leaves are MeshNodes sharing ONE iris::Mesh and ONE
// iris::Material (the mirror caches both by pointer, so the engine holds one
// mesh + one datablock and N items — the shape a real scene has).
//
//   STATIC/DYNAMIC TAGGING. On the CURRENT graph "static" has no
//   representation: it only means "this node is never moved by the benchmark".
//   The tag is the NODE NAME PREFIX — 's' for static-intent, 'd' for dynamic —
//   so the post-swap run can map every 's' node to Ogre's SCENE_STATIC without
//   guessing, and the 80/20 split stays identical across the comparison.
//   Selection is deterministic: index % 5 == 0 is dynamic.
//
// STABILITY, OR THE GATE IS NOISE. Every metric is a median over a loop that
// runs until either `maxIters` or a per-metric wall-clock budget is spent
// (never fewer than `minIters`), after a warm-up that is thrown away. Two
// dispersion numbers are computed and both are asserted in --assert mode:
//   cv   = stddev/mean          — the classic coefficient of variation (§6's ask)
//   rcv  = 1.4826*MAD / median  — the robust one, immune to a single scheduler
//                                 hiccup on a developer box
// The bounds are deliberately loose (see kCvMax/kRcvMax): this gate's job is to
// catch "the numbers are noise", not to police a percent.
//
// MODES
//   --assert            (default) run, print, assert the physics bars.
//   --record <file>     ...and write the JSON result file (the measure's evidence).
//   --scales 1000,10000 override the scale list.
//   --threads N         worker threads for each SCENE the sweep creates
//                       (Engine::createScene). 0 = the engine default, which is
//                       what the gate and every recorded baseline use; any
//                       other value is a MEASUREMENT run — the barrier cost of
//                       a pool is real, so "more threads" has to be shown to
//                       pay at each scale before the app adopts a number
//                       (fps audit F3; the app's choice lives in
//                       src/bridge/sceneworkerthreads.h).
//   --note <text>       free text stored in the recorded file (run conditions).
//   --quick             short budgets, for editing this file.
//
// RUN REQUIREMENTS: same as every engine suite — a reachable X display (Ogre's
// VulkanXcbSupport connects at plugin load) and a Vulkan driver; the view is
// offscreen and QT_QPA_PLATFORM=offscreen, so nothing is ever shown.

#include <QGuiApplication>

#include "../support/testmesh.h"
#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;
using Clock = std::chrono::steady_clock;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---------------------------------------------------------------------------
// The stability bounds.
//
// MEASURED, not guessed, AND the two dispersion numbers are not equals.
//
// On a QUIET box (RTX 4080 SUPER, i9-10900K, Debug, offscreen 320x240,
// private Xvfb) the worst any metric produced was cv 0.029 / rcv 0.015.
// Then a parallel worktree started a full app compile (load 16) DURING a
// recording run: two of ten samples of c.reparent_500@50000 took 15.9 s against
// a 5.1 s median, which is cv 0.466 — and rcv 0.021, unmoved. That is the whole
// argument for having both numbers:
//   * rcv (1.4826*MAD/median) describes the measurement. It is the bound with
//     teeth, and it is set close to what a quiet box produces.
//   * cv (stddev/mean) is dominated by a single outlier when n is 10, which is
//     what several of these metrics can afford. It is kept because §6 asks for
//     a coefficient-of-variation bound, and set loose enough that a neighbour's
//     compile does not redden somebody else's gate.
// Neither bounds PERFORMANCE: nothing here asserts a millisecond count. If a
// metric exceeds these, the measurement is not usable — the fix is more
// iterations or a quieter box, never a looser bound.
static constexpr double kCvMax  = 0.60;   // stddev/mean          (quiet-box worst 0.029)
static constexpr double kRcvMax = 0.25;   // 1.4826*MAD / median  (quiet-box worst 0.015)
// (c) and (d) alternate direction between iterations and get the fewest samples
// (a single 50k reparent is ~5 s), so both of their bounds carry more headroom.
static constexpr double kCvMaxReparent  = 1.00;
// THE SETTLE SLICE'S OWN BOUND (MIRROR-SETTLE-1 fix round 1): a sync that runs
// a slice of the static settle may cost at most 0.5 ms — 3 % of a 16.7 ms
// frame — at every scale. SceneMirror::kStaticSettleNodesPerSync is derived
// from it (scenemirror.h). The one millisecond bound in this file, and it is
// a bound on the walk's slice, never on the scene's steady state.
static constexpr double kSettleSliceMaxMs = 0.5;
static constexpr double kRcvMaxReparent = 0.35;


// ---------------------------------------------------------------------------
// Sample statistics.

struct Stats {
    size_t n = 0;
    double median = 0, mean = 0, min = 0, max = 0, p95 = 0, stddev = 0, cv = 0, rcv = 0;
};

static double percentile(std::vector<double> sorted, double p)
{
    if (sorted.empty()) return 0.0;
    const double idx = p * (double(sorted.size()) - 1.0);
    const size_t lo = size_t(std::floor(idx)), hi = size_t(std::ceil(idx));
    return sorted[lo] + (sorted[hi] - sorted[lo]) * (idx - double(lo));
}

static Stats computeStats(std::vector<double> v)
{
    Stats s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.n = v.size();
    s.min = v.front();
    s.max = v.back();
    s.median = percentile(v, 0.5);
    s.p95 = percentile(v, 0.95);
    double sum = 0; for (double x : v) sum += x;
    s.mean = sum / double(v.size());
    double acc = 0; for (double x : v) acc += (x - s.mean) * (x - s.mean);
    s.stddev = std::sqrt(acc / double(v.size()));
    s.cv = s.mean > 0 ? s.stddev / s.mean : 0.0;
    std::vector<double> dev;
    dev.reserve(v.size());
    for (double x : v) dev.push_back(std::fabs(x - s.median));
    std::sort(dev.begin(), dev.end());
    const double mad = percentile(dev, 0.5);
    s.rcv = s.median > 0 ? (1.4826 * mad) / s.median : 0.0;
    return s;
}

/// One measured metric: an id, the scale it was measured at, its samples.
struct Metric {
    std::string id;
    int scale = 0;
    Stats stats;
    double single = -1.0;      ///< >= 0 for one-shot metrics (e.build_doc etc.)
    bool reparentClass = false;///< uses the looser dispersion bounds
};

static std::vector<Metric> gMetrics;
static std::map<std::string, double> gCounters;   ///< render-stat style scalars

/// THE machine-readable line. One per metric, greppable, stable field order.
static void emitLine(const Metric &m)
{
    if (m.single >= 0.0) {
        std::printf("BENCH %-28s scale=%-6d n=1     median_ms=%9.3f\n",
                    m.id.c_str(), m.scale, m.single);
        return;
    }
    const Stats &s = m.stats;
    std::printf("BENCH %-28s scale=%-6d n=%-4zu median_ms=%9.3f mean_ms=%9.3f "
                "p95_ms=%9.3f min_ms=%9.3f max_ms=%9.3f cv=%5.3f rcv=%5.3f\n",
                m.id.c_str(), m.scale, s.n, s.median, s.mean, s.p95, s.min, s.max, s.cv, s.rcv);
}

static void addMetric(const std::string &id, int scale, const Stats &s, bool reparentClass = false)
{
    Metric m; m.id = id; m.scale = scale; m.stats = s; m.reparentClass = reparentClass;
    gMetrics.push_back(m);
    emitLine(m);
}

static void addSingle(const std::string &id, int scale, double ms)
{
    Metric m; m.id = id; m.scale = scale; m.single = ms;
    gMetrics.push_back(m);
    emitLine(m);
}

// ---------------------------------------------------------------------------
// The measurement loop.

/// Collects named sub-timings inside one iteration, so a single loop can yield
/// both "edit + sync" and "edit + sync + render" without running twice (the two
/// would otherwise be measured on different scene states).
class Recorder {
public:
    void add(const std::string &id, double ms) { mSeries[id].push_back(ms); }
    const std::map<std::string, std::vector<double>> &series() const { return mSeries; }
    void clear() { mSeries.clear(); }
private:
    std::map<std::string, std::vector<double>> mSeries;
};

struct LoopBudget {
    int warmup = 3;
    int minIters = 10;
    int maxIters = 400;
    double budgetMs = 1500.0;
};

static double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

/// Runs `body(iteration, rec)` warm-up times with the recorder cleared, then for
/// real until the budget or maxIters is reached (never fewer than minIters).
static std::map<std::string, Stats> runLoop(const LoopBudget &b,
                                            const std::function<void(int, Recorder &)> &body)
{
    Recorder rec;
    for (int i = 0; i < b.warmup; ++i) body(i, rec);
    rec.clear();
    const auto start = Clock::now();
    int i = 0;
    for (; i < b.maxIters; ++i) {
        body(b.warmup + i, rec);
        if (i + 1 >= b.minIters && msSince(start) >= b.budgetMs) { ++i; break; }
    }
    std::map<std::string, Stats> out;
    for (const auto &kv : rec.series()) out[kv.first] = computeStats(kv.second);
    return out;
}

// ---------------------------------------------------------------------------
// The synthetic document scene.

struct SynthScene {
    iris::ScenePtr doc;
    std::vector<iris::SceneNodePtr> all;        ///< every generated node, tree order
    std::vector<iris::SceneNodePtr> dynamic;    ///< the 20% the benchmark moves
    std::vector<iris::Vec3>         basePos;    ///< authored local pos of `all`
    std::vector<iris::Vec3>         dynBase;    ///< authored local pos of `dynamic`
    iris::SceneNodePtr subtree;                 ///< the 500-node fragment for (c)
    iris::SceneNodePtr hostA, hostB;            ///< its two parents
    int nodeCount = 0;                          ///< N (excludes the (c) fixture)
    int totalNodes = 0;                         ///< everything in the document
    float radius = 10.0f;                       ///< world half-extent, for framing
};

/// SCENE_STATIC, APPLIED (SPECS/SCENEGRAPH_SPEC.md §6). Until this lane the
/// static/dynamic tag was intent only — a name prefix nobody acted on, because
/// v1 had no way to put a subtree in Ogre's static memory manager and keep it
/// there. Now there is, so the benchmark marks the scene and the (b) metrics
/// measure a scene that really is 's' static / 'd' dynamic.
///
/// HOW MUCH OF THE 80% CAN LAND, and why it is not 80%. Ogre gives a node its
/// PARENT's memory-manager class on every reparent, and a static node's derived
/// transform is only refreshed on static dirties — so a static node under a
/// moving parent freezes at the parent's old world position (OgreNode.h calls
/// the configuration "probably a bug"). Static therefore has to be
/// DOWNWARD-CLOSED: a node can only be static if every ancestor is. The
/// lattice's tag is drawn per node independently (`i % 5 == 0` is dynamic), so
/// what actually lands is "every node whose whole ancestor chain is 's'" —
/// ~0.8^(depth+1) of the population, which this function measures and prints
/// rather than assuming. A real scene classifies the other way round (a static
/// building, a dynamic character) and keeps far more.
///
/// Called TWICE per scale: once after the build, and again before the (b) idle
/// block — because metric (a') moves every node in the scene, and moving a
/// static node demotes it by design (rule 4).
static size_t applyStaticTags(SynthScene &s)
{
    size_t marked = 0;
    // Parents come first: `parentIdx = i / branch - 1 < i` for every i, so a
    // single ascending pass is topological.
    for (size_t i = 0; i < s.all.size(); ++i) {
        const iris::SceneNodePtr &n = s.all[i];
        const bool wantStatic = !n->getName().startsWith(QLatin1Char('d'));
        if (wantStatic) {
            // canBeStatic() first, so an 's' node under a 'd' ancestor is
            // SKIPPED rather than refused — a refusal is a warning per node,
            // and there are thousands of them.
            if (iris::graph::canBeStatic(n->docNode())) n->_applyStaticHint(true);
        } else if (n->isStaticInGraph()) {
            // It inherited static from a static parent; the tag says it moves.
            n->_applyStaticHint(false);
        }
    }
    for (const iris::SceneNodePtr &n : s.all)
        if (n->isStaticInGraph()) ++marked;
    return marked;
}

/// One shared cube + one shared material for every mesh node in the process:
/// the mirror caches MeshData per iris::Mesh* and datablocks per iris::Material*,
/// so this is what makes N items cost N items and not N mesh uploads.
static iris::MeshPtr gCube;
static iris::MaterialPtr gMaterial;

static void loadSharedAssets()
{
    gCube = testmesh::load(":assets/models/cube.obj");
    auto mat = iris::PbrMaterial::create();
    mat->setBaseColor(QColor(204, 96, 51));
    gMaterial = mat;
}

/// Builds the N-node lattice tree described in the header comment.
static void buildLattice(SynthScene &s, int n)
{
    const int branch = 8;
    const int side = std::max(1, int(std::ceil(std::cbrt(double(n)))));
    const float spacing = 1.5f;
    const float half = 0.5f * spacing * float(side - 1);
    s.radius = std::max(4.0f, half * 1.7f);

    std::vector<iris::Vec3> world;
    world.resize(size_t(n));
    s.all.resize(size_t(n));
    s.basePos.resize(size_t(n));

    for (int i = 0; i < n; ++i) {
        const int gx = i % side, gy = (i / side) % side, gz = i / (side * side);
        world[size_t(i)] = iris::Vec3(float(gx) * spacing - half,
                                      float(gy) * spacing - half,
                                      float(gz) * spacing - half);
    }

    for (int i = 0; i < n; ++i) {
        const bool interior = (branch * (i + 1)) < n;      // has at least one child
        const bool dynamic  = (i % 5) == 0;                // exactly 20%
        iris::SceneNodePtr node;
        if (interior) {
            node = iris::SceneNode::create();
        } else {
            auto mn = iris::MeshNode::create();
            mn->setMesh(gCube);
            mn->setMaterial(gMaterial);
            mn->setLocalScale(iris::Vec3(0.5f, 0.5f, 0.5f));
            node = mn;
        }
        // THE STATIC/DYNAMIC TAG (see header): name prefix, deterministic.
        node->setName(QString(dynamic ? "d%1" : "s%1").arg(i));

        const int parentIdx = (i / branch) - 1;
        const iris::Vec3 parentWorld = parentIdx >= 0 ? world[size_t(parentIdx)] : iris::Vec3(0, 0, 0);
        const iris::Vec3 local = world[size_t(i)] - parentWorld;
        node->setLocalPos(local);
        s.basePos[size_t(i)] = local;
        s.all[size_t(i)] = node;

        if (parentIdx >= 0) s.all[size_t(parentIdx)]->addChild(node, false);
        else                s.doc->getRootNode()->addChild(node, false);

        if (dynamic) { s.dynamic.push_back(node); s.dynBase.push_back(local); }
    }
    s.nodeCount = n;
}

/// The (c) fixture: two host nodes and a 500-node mesh subtree under the first
/// one — the shape of an imported fragment (SCENEGRAPH_SPEC §2 "import a
/// 500-node fragment"), so its reparent pays the same registry and scene-link
/// costs a real one does.
static void buildReparentFixture(SynthScene &s, int subtreeSize)
{
    s.hostA = iris::SceneNode::create(); s.hostA->setName("s_hostA");
    s.hostB = iris::SceneNode::create(); s.hostB->setName("s_hostB");
    s.hostA->setLocalPos(iris::Vec3(-s.radius * 0.5f, 0, 0));
    s.hostB->setLocalPos(iris::Vec3(s.radius * 0.5f, 0, 0));
    s.doc->getRootNode()->addChild(s.hostA, false);
    s.doc->getRootNode()->addChild(s.hostB, false);

    std::vector<iris::SceneNodePtr> nodes;
    nodes.reserve(size_t(subtreeSize));
    const int branch = 8;
    for (int i = 0; i < subtreeSize; ++i) {
        auto mn = iris::MeshNode::create();
        mn->setMesh(gCube);
        mn->setMaterial(gMaterial);
        mn->setLocalScale(iris::Vec3(0.4f, 0.4f, 0.4f));
        mn->setName(QString("s_frag%1").arg(i));
        mn->setLocalPos(iris::Vec3(float(i % 10) * 0.6f, float((i / 10) % 10) * 0.6f, 0.0f));
        // An 8-ary tree ROOTED AT nodes[0] — every node but the root has a
        // parent inside the fragment, so all `subtreeSize` of them travel
        // together when the root is reparented. (Getting this wrong silently
        // orphans 85% of the fragment and the reparent measures nothing:
        // the "every synthetic node reached the engine" check below is what
        // catches it.)
        if (i > 0) nodes[size_t((i - 1) / branch)]->addChild(mn, false);
        nodes.push_back(mn);
    }
    s.subtree = nodes.front();
    s.hostA->addChild(s.subtree, false);
}

// ---------------------------------------------------------------------------
// The animated rig (metric d).

/// A 200-node chain, every node driven by a position PropertyAnim. A chain (not
/// a fan) on purpose: it is the skeleton shape, and it makes the document's
/// transform propagation walk 200 deep every tick — the cost v1 hands to Ogre's
/// threaded SIMD FK.
static iris::ScenePtr buildAnimRig(int n, std::vector<iris::SceneNodePtr> &out)
{
    auto doc = iris::Scene::create();
    iris::SceneNodePtr prev = doc->getRootNode();
    for (int i = 0; i < n; ++i) {
        auto mn = iris::MeshNode::create();
        mn->setMesh(gCube);
        mn->setMaterial(gMaterial);
        mn->setName(QString("d_bone%1").arg(i));
        mn->setLocalScale(iris::Vec3(0.25f, 0.25f, 0.25f));
        mn->setLocalPos(iris::Vec3(0.12f, 0.05f, 0.0f));

        auto anim = iris::Animation::create("bone");
        auto pa = new iris::Vector3DPropertyAnim();
        pa->setName("position");
        const float ph = float(i) * 0.05f;
        for (int k = 0; k <= 4; ++k) {
            const float t = float(k) * 0.5f;
            pa->getKeyFrame(0)->addKey(0.12f + 0.03f * std::sin(ph + t), t);
            pa->getKeyFrame(1)->addKey(0.05f + 0.03f * std::cos(ph + t), t);
            pa->getKeyFrame(2)->addKey(0.0f, t);
        }
        anim->addPropertyAnim(pa);
        anim->setLength(2.0f);
        anim->setLooping(true);
        mn->addAnimation(anim);
        mn->setAnimation(anim);

        prev->addChild(mn, false);
        prev = mn;
        out.push_back(mn);
    }
    return doc;
}

// ---------------------------------------------------------------------------
// Host fingerprint — the file is worthless for comparison without it.

static std::string readCpuModel()
{
    QFile f("/proc/cpuinfo");
    if (!f.open(QIODevice::ReadOnly)) return "unknown";
    // readAll(), NOT a readLine loop guarded by atEnd(): QFileDevice::atEnd()
    // is size-based and every /proc file reports size 0, so the loop exits
    // before reading a single line and the fingerprint silently says "unknown".
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("model name")) {
            const int c = line.indexOf(':');
            if (c > 0) return line.mid(c + 1).trimmed().toStdString();
        }
    }
    return "unknown";
}

/// Worker threads for the benchmark's engine scenes (--threads). 0 = the
/// engine's own default, which is what every recorded baseline used and what
/// the ctest gate must keep using — this flag exists to MEASURE the per-scene
/// pool (fps audit F3, src/bridge/sceneworkerthreads.h decides the app's
/// numbers), not to change what the gate compares.
static unsigned gWorkerThreads = 0;

// ---------------------------------------------------------------------------
// JSON: write the result file (the measure's evidence).

static QJsonObject metricToJson(const Metric &m)
{
    QJsonObject o;
    if (m.single >= 0.0) { o["median_ms"] = m.single; o["n"] = 1; return o; }
    o["median_ms"] = m.stats.median;
    o["mean_ms"]   = m.stats.mean;
    o["p95_ms"]    = m.stats.p95;
    o["min_ms"]    = m.stats.min;
    o["max_ms"]    = m.stats.max;
    o["cv"]        = m.stats.cv;
    o["rcv"]       = m.stats.rcv;
    o["n"]         = int(m.stats.n);
    return o;
}

static bool writeResults(const std::string &path, const std::string &gpu,
                         const std::string &note)
{
    QJsonObject root;
    root["schema"] = 1;
    root["spec"] = "SPECS/SCENEGRAPH_SPEC.md §6";
    root["recorded_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["commit"] = BENCH_GIT_COMMIT;
    root["note"] = QString::fromStdString(note);

    QJsonObject host;
    host["cpu"] = QString::fromStdString(readCpuModel());
    host["cores"] = int(std::thread::hardware_concurrency());
    host["gpu"] = QString::fromStdString(gpu);
    host["os"] = "Linux";
    root["host"] = host;

    QJsonObject cfg;
    cfg["build_type"] = BENCH_BUILD_TYPE;
    cfg["compiler"] = BENCH_COMPILER;
    // ASKED OF THE COMPILER, not asserted by a human: a sanitised binary's
    // numbers are not comparable with an unsanitised one's, and a baseline that
    // merely CLAIMED "asan: false" would be the easiest possible lie to tell.
#if defined(__SANITIZE_ADDRESS__)
    cfg["asan"] = true;
#else
    cfg["asan"] = false;
#endif
    cfg["qt"] = QT_VERSION_STR;
    cfg["view"] = "offscreen 320x240, MSAA 1x";
    // 0 = the engine default (2). Recorded because a comparison across
    // different pool sizes is not a comparison of the same thing.
    cfg["scene_worker_threads"] = int(gWorkerThreads);
    cfg["static_tag"] = "node name prefix: 's' = static-intent (never moved), "
                        "'d' = dynamic-intent (moved every iteration). APPLIED since the "
                        "consumer-conversion lane: every 's' node whose whole ancestor chain "
                        "is also 's' really sits in Ogre's SCENE_STATIC memory manager "
                        "(static must be downward-closed — see applyStaticTags). The realised "
                        "count is in counters static.nodes.<scale>.";
    cfg["mix"] = "80% static-intent / 20% dynamic-intent (index % 5 == 0 is dynamic)";
    root["config"] = cfg;

    QJsonObject metrics;
    for (const Metric &m : gMetrics) {
        const QString id = QString::fromStdString(m.id);
        QJsonObject byScale = metrics.value(id).toObject();
        byScale[QString::number(m.scale)] = metricToJson(m);
        metrics[id] = byScale;
    }
    root["metrics"] = metrics;

    QJsonObject counters;
    for (const auto &kv : gCounters) counters[QString::fromStdString(kv.first)] = kv.second;
    root["counters"] = counters;

    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::printf("FAIL: cannot write %s\n", path.c_str());
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    std::printf("recorded: %s\n", path.c_str());
    return true;
}


// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    std::string recordPath;
    std::string note = "recorded by tests/benchmarks/bench_scenegraph.cpp";
    std::vector<int> scales{1000, 10000, 50000};
    bool quick = false;
    // --smoke: the STRUCTURAL half of this benchmark, at the smallest scale,
    // with no timing verdict of any kind (TEST_GATE_AUDIT.md §3). The full
    // `--assert` run is five minutes and RUN_SERIAL, so a merge gate paid its
    // whole wall time for it; what a merge gate actually needs from this binary
    // is the answer to "did every document node reach the engine, did the
    // static tags land, did a frame render" — which is three CHECKs that cost
    // fifteen seconds and do not care how loaded the box is. The dispersion
    // bounds stay where they belong: on a quiet box, at the close, under --assert.
    bool smoke = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--assert") {}   // the default: run, print, assert the physics bars
        else if (a == "--record" && i + 1 < argc) recordPath = argv[++i];
        else if (a == "--note" && i + 1 < argc) note = argv[++i];
        else if (a == "--quick") quick = true;
        else if (a == "--smoke") { smoke = true; quick = true; scales = {1000}; }
        else if (a == "--threads" && i + 1 < argc) gWorkerThreads = unsigned(std::stoi(argv[++i]));
        else if (a == "--scales" && i + 1 < argc) {
            scales.clear();
            std::string list = argv[++i], cur;
            for (char c : list + ",") { if (c == ',') { if (!cur.empty()) scales.push_back(std::stoi(cur)); cur.clear(); } else cur += c; }
        } else if (a == "--help") {
            std::printf("bench_scenegraph [--assert] [--smoke] [--record <file>] "
                        "[--scales 1000,10000,50000] [--quick] [--threads N] [--note <text>]\n");
            return 0;
        }
    }


    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const auto cfg = enginetest::testEngineConfig("bench_scenegraph-ogre.log");
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("bench", 320, 240, Colour(0, 0, 1));
    CHECK(view != nullptr, "offscreen view");
    if (!view) return 1;
    // A fixed frame delta: the engine charges WALL-CLOCK time per frame and
    // offscreen frames are ~1 ms, so anything time-driven inside it would see a
    // different clock than a real editor. Deterministic here, like every other
    // headless suite (PFX2 fact, CLAUDE.md).
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    // ARM the geometry counters. Engine::renderStats is lazy by contract: the
    // FIRST call switches Ogre's per-draw counting on and reports zeros with
    // metricsRecording=false. Asking here, before anything is built, means
    // every later read in the sweep carries real draws/batches/triangles.
    { RenderStats warm; enginetest::renderStats(engine, warm); }

    loadSharedAssets();
    CHECK(!gCube.isNull(), "shared cube mesh loaded (no GL)");

    std::printf("\n== correctness probe: a document edit really is engine-visible ==\n");
    {
        Scene *es = engine->createScene("probe");
        view->setScene(es);
        es->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.2f, 0.2f, 0.2f));
        enginetest::testCameraLookAt(view, Vec3(0, 0, 4), Vec3(0, 0, 0));
        auto doc = iris::Scene::create();
        auto mn = iris::MeshNode::create();
        mn->setMesh(gCube);
        mn->setMaterial(gMaterial);
        const float r = mn->getMeshRadius();
        const float k = r > 0.0f ? 1.0f / r : 1.0f;
        mn->setLocalScale(iris::Vec3(k, k, k));
        doc->getRootNode()->addChild(mn, false);
        SceneMirror mirror(es);
        mirror.setSource(doc);
        mirror.sync();
        enginetest::frames(engine, 3);
        Image img;
        view->readPixels(img);
        const Colour at = img.at(img.width / 2, img.height / 2);
        std::printf("    cube at origin: centre %3.0f %3.0f %3.0f\n", at.r * 255, at.g * 255, at.b * 255);
        CHECK(at.r > 0.10f && at.r > at.b * 1.5f, "centre pixel shows the mirrored cube");
        mn->setLocalPos(iris::Vec3(50.0f, 0.0f, 0.0f));
        mirror.sync();
        enginetest::frames(engine, 2);
        view->readPixels(img);
        const Colour away = img.at(img.width / 2, img.height / 2);
        std::printf("    moved +50x:     centre %3.0f %3.0f %3.0f\n", away.r * 255, away.g * 255, away.b * 255);
        CHECK(away.b > 0.8f && away.r < 0.15f, "the edit reached the rendered frame (background)");
        mirror.setSource(nullptr);   // unbind before the engine scene dies
        view->setScene(nullptr);
        engine->destroyScene(es);
    }

    // ---- (f) THE TRIANGLE BUDGET (ATOM stage 1; NANITE_SPEC §12/§14) -----
    //
    //   f.tri_budget.<distance>.triangles_lod0   drawn triangles, LOD dial OFF
    //   f.tri_budget.<distance>.triangles        drawn triangles, dial at the reference
    //   f.tri_budget.<distance>.draws            draw COMMANDS, both ways (they must agree)
    //   f.tri_budget.<distance>.frame_ms         frame cost with the dial on
    //
    // THE COMPARISON IS WITHIN ONE RUN, AND THAT IS THE POINT. "Triangles at
    // four distances" on its own is not a measurement of LOD: moving the camera
    // changes what the frustum contains, so the four numbers differ for two
    // reasons at once. Setting the dial to 0 PINS every object at its finest
    // level, which is exactly the renderer this feature replaced — so at each
    // fixed pose the pair (dial off, dial on) isolates the LOD effect and
    // nothing else. No baseline file is needed for a ratio taken twice in one
    // second on one box, which is why this section asserts in --smoke too.
    //
    // FINDING B is asserted here as an EQUALITY, not a tendency: the levels
    // share one vertex buffer and one index pool, so they share a vaoName and
    // RenderQueue::render advances numDraws instead of emitting another draw
    // command. A LOD chain that split draw calls would trade a triangle win for
    // a draw-call loss, which on this renderer is the wrong trade.
    std::printf("\n== (f) triangle budget: the LOD chain at four distances ==\n");
    {
        // A mesh with a real chain: the shipped high-poly sphere (1,536
        // triangles), simplified by the SAME code the importer bakes with
        // (MeshBake::buildLodChain) rather than a second implementation of the
        // policy. Loaded from the source tree because IrisGL's own qrc carries
        // only the 12-triangle cube.
        iris::MeshPtr dense =
            testmesh::load(QString(JAHSHAKA_SOURCE_DIR "/app/content/primitives/hp_sphere.obj"));
        CHECK(!dense.isNull(), "(f) the dense fixture mesh loaded");
        if (!dense.isNull()) {
            iris::MeshBake::buildLodChain(dense);
            // ...AND ITS CLUSTER DAG, as the importer bakes both (ATOM-CLUSTER-CUT): the id
            // pass draws an Atom item's CLUSTER CUT, and a mesh with a chain but no DAG is
            // cut as a flat one — its level 0, whatever the dial says.
            iris::MeshBake::buildClusterDag(dense);
            const int levels = int(dense->lodIndices.size());
            std::printf("    fixture: %d triangles, %d LOD level(s)\n",
                        dense->getIndexBuffer() ? dense->getIndexBuffer()->dataSize / 12 : 0, levels);
            CHECK(levels >= 1, "(f) the fixture has a LOD chain to measure");
            gCounters["f.tri_budget.levels"] = double(levels);

            Scene *es = engine->createScene("tribudget", gWorkerThreads);
            CHECK(es != nullptr, "(f) engine scene");
            if (es && levels >= 1) {
                es->setAmbient(Colour(0.35f, 0.35f, 0.35f), Colour(0.2f, 0.2f, 0.2f));
                view->setScene(es);

                // 256 instances on an 8x4x8 grid: enough that the triangle
                // count is the number that matters and enough instances that
                // the draw-call equality is a real assertion (they auto-instance
                // into a handful of draw commands).
                auto doc = iris::Scene::create();
                const float spacing = 3.0f;
                for (int i = 0; i < 256; ++i) {
                    const int gx = i % 8, gy = (i / 8) % 4, gz = i / 32;
                    auto mn = iris::MeshNode::create();
                    mn->setMesh(dense);
                    mn->setMaterial(gMaterial);
                    mn->setLocalPos(iris::Vec3((float(gx) - 3.5f) * spacing,
                                               (float(gy) - 1.5f) * spacing,
                                               (float(gz) - 3.5f) * spacing));
                    mn->setName(QString("s%1").arg(i));
                    doc->getRootNode()->addChild(mn, false);
                }
                SceneMirror mirror(es);
                mirror.setSource(doc);
                mirror.sync();

                struct Reading { unsigned long long triangles = 0, draws = 0; double frameMs = 0.0; };
                const char *names[4] = { "near", "mid", "far", "extreme" };
                const float dists[4] = { 12.0f, 40.0f, 120.0f, 400.0f };
                Reading off[4], on[4];
                for (int d = 0; d < 4; ++d) {
                    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, dists[d]), Vec3(0, 0, 0));
                    for (int pass = 0; pass < 2; ++pass) {
                        es->setLodBias(pass == 0 ? 0.0f : 1.0f);
                        // Ogre's LOD update runs inside the scene pass, so the first
                        // frame after a dial change still draws the old level; three
                        // frames is settled and cheap...
                        enginetest::frames(engine, 3);
                        RenderStats rs;
                        if (!enginetest::renderStats(engine, rs)) continue;
                        // ...and THE COUNTS' OWN LAG on top (RenderStats::gpuCountLagFrames).
                        // Where the visibility buffer draws the grid (ATOM S3-DRAW) its
                        // triangles are chosen by the GPU cull and counted from a readback
                        // of the cull's counters once the frame has retired: the count trails
                        // the picture by that many frames (the three above read the PREVIOUS
                        // pose's count: "1365 %" was the level-0 count of the pass before).
                        // 0 on the CPU path, where Ogre counts what it submits.
                        if (rs.gpuCountLagFrames) {
                            enginetest::frames(engine, rs.gpuCountLagFrames);
                            if (!enginetest::renderStats(engine, rs)) continue;
                        }
                        Reading &r = (pass == 0 ? off[d] : on[d]);
                        r.triangles = rs.triangles;
                        r.draws = rs.draws;
                        r.frameMs = rs.frameMs;
                    }
                    const double ratio = off[d].triangles ? double(on[d].triangles) / double(off[d].triangles) : 1.0;
                    std::printf("BENCH %-28s dist=%-6.0f triangles %8llu -> %8llu (%5.1f%%)  draws %llu -> %llu\n",
                                (std::string("f.tri_budget.") + names[d]).c_str(), dists[d],
                                off[d].triangles, on[d].triangles, ratio * 100.0,
                                off[d].draws, on[d].draws);
                    const std::string key = std::string("f.tri_budget.") + names[d] + ".";
                    gCounters[key + "triangles_lod0"] = double(off[d].triangles);
                    gCounters[key + "triangles"] = double(on[d].triangles);
                    gCounters[key + "draws"] = double(on[d].draws);
                    gCounters[key + "frame_ms"] = on[d].frameMs;
                    gCounters[key + "ratio"] = ratio;
                    // The EQUALITY assumes every level's index buffer sits in the SAME
                    // immutable VBO pool as level 0's (the pool's VkBuffer is part of the
                    // vaoName); a chain that straddles a pool boundary costs one more
                    // command without a defect — a dense fixture would need a tolerance.
                    CHECK(on[d].draws == off[d].draws,
                          (std::string("(f) ") + names[d] +
                           ": the LOD chain costs no extra draw command").c_str());
                }
                // THE ACCEPTANCE (§7.3): at the far distance the chain must shed
                // at least 40% of the triangles the un-LOD'd scene draws.
                const double farRatio = off[2].triangles ? double(on[2].triangles) / double(off[2].triangles) : 1.0;
                CHECK(farRatio <= 0.60,
                      ("(f) far: at least 40% fewer triangles with the dial on (" +
                       std::to_string(int(farRatio * 100.0)) +
                       "% of the un-LOD'd count)").c_str());
                CHECK(off[0].triangles >= on[0].triangles,
                      "(f) near: the dial never ADDS triangles");
                es->setLodBias(1.0f);
                mirror.setSource(nullptr);
                view->setScene(nullptr);
                engine->destroyScene(es);
            }
        }
    }

    // ---- the scale sweep: metrics (a), (b), (c) --------------------------
    for (int n : scales) {
        std::printf("\n== scale %d ==\n", n);
        Scene *es = engine->createScene(("bench" + std::to_string(n)).c_str(), gWorkerThreads);
        if (!es) { std::printf("FAIL: engine scene for %d\n", n); ++failures; continue; }
        es->setAmbient(Colour(0.35f, 0.35f, 0.35f), Colour(0.2f, 0.2f, 0.2f));
        view->setScene(es);

        SynthScene s;
        s.doc = iris::Scene::create();
        const auto tBuild = Clock::now();
        buildLattice(s, n);
        buildReparentFixture(s, 500);
        const double buildMs = msSince(tBuild);
        s.totalNodes = n + 502;

        enginetest::testCameraLookAt(view, Vec3(s.radius * 1.4f, s.radius * 1.1f, s.radius * 1.8f),
                                     Vec3(0, 0, 0));

        SceneMirror mirror(es);
        mirror.setSource(s.doc);
        const SceneChangeStats fs0 = es->published().changes;
        const auto tFirst = Clock::now();
        const int mirrored = mirror.sync();
        const double firstSyncMs = msSince(tFirst);
        enginetest::frames(engine, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
        std::printf("    document %d nodes (%zu dynamic), mirror reported %d, first sync %.1f ms\n",
                    s.totalNodes, s.dynamic.size(), mirrored, firstSyncMs);
        // THE FIRST SYNC'S COUNTS (SCENEGRAPH-BASELINE-2): its cost is the counts' — one visit per node, one material
        // build per distinct material, one Item per mesh node, ONE GI invalidation for the whole adoption (the
        // structural edit), never a count that grows as nodes x users.
        {
            const SceneChangeStats fs1 = es->published().changes;
            std::printf("COUNT e.first_sync scale=%d visited=%d dirty=%llu pushes=%llu materials=%llu attaches=%llu "
                        "walk=%s itemsCreated=%llu giInvalidations=%llu probeRestarts=%llu reclaimSweeps=%llu\n",
                        n, mirrored, (unsigned long long)mirror.dirtyNodeCount(),
                        (unsigned long long)mirror.visitPushCount(), (unsigned long long)mirror.materialBuildCount(),
                        (unsigned long long)mirror.meshAttachCount(), mirror.walkMode(),
                        fs1.itemsCreated - fs0.itemsCreated, fs1.giInvalidations - fs0.giInvalidations,
                        fs1.probeBuildRestarts - fs0.probeBuildRestarts,
                        (unsigned long long)mirror.reclaimSweepCount());
        }
        CHECK(mirrored == s.totalNodes, "every synthetic node reached the engine");

        addSingle("e.build_doc", n, buildMs);
        addSingle("e.first_sync", n, firstSyncMs);

        // THE EXTRACT (ENGINE-UPGRADE-U2, U3's bar): the mirror's sync with K nodes WRITTEN (their own value — the
        // picture holds still) — 1 (a drag), 100 (a physics pile), every node (a load, a paste) — and nothing else
        // changed: the frame record carries one NodeRecord per written node (their LOCAL transform; the engine
        // composes the rest), and the sync costs what extracting them costs. The render that drains it is outside
        // the timed region; the records it applied are counted (d.extract_records.<K>.<n>).
        {
            const int ks[3] = { 1, 100, int(s.all.size()) };
            const char *names[3] = { "d.extract_1", "d.extract_100", "d.extract_n" };
            for (int k = 0; k < 3; ++k) {
                const int K = std::min(ks[k], int(s.all.size()));
                unsigned lastRecords = 0;
                const auto st = runLoop(LoopBudget{ 3, 10, 200, 1200.0 }, [&](int, Recorder &rec) {
                    for (int i = 0; i < K; ++i) {
                        iris::SceneNode *w = (k < 2 && i < int(s.dynamic.size())) ? s.dynamic[std::size_t(i)].data()
                                                                                 : s.all[std::size_t(i)].data();
                        w->setLocalPos(w->getLocalPos());
                    }
                    const auto t0 = Clock::now();
                    mirror.sync();
                    rec.add(names[k], msSince(t0));
                    enginetest::frames(engine, 1);
                    lastRecords = engine->published().input.nodeTransforms;
                });
                addMetric(names[k], n, st.at(names[k]), true);
                gCounters[std::string("d.extract_records.") + std::to_string(K) + "." + std::to_string(n)] =
                    double(lastRecords);
                std::printf("    extract: %d written -> %u transform records applied by the drain\n", K, lastRecords);
                CHECK(lastRecords == unsigned(K), "(d) the record carries ONE transform per written node, no more "
                                                  "(the settle's class records beside them are not writes)");
            }
        }

        // SCENE_STATIC, applied and REPORTED. Outside every measured region:
        // the marking itself is authoring-time work (one memory-manager
        // migration per node), which is exactly what makes the idle ticks
        // below cheaper. Marked AFTER the first sync so the engine's Items
        // already exist and switch class with their nodes — the other order
        // works too (attachMesh reads the node's class), this one exercises
        // the harder half.
        {
            const auto tMark = Clock::now();
            const size_t marked = applyStaticTags(s);
            const double markMs = msSince(tMark);
            std::printf("    SCENE_STATIC: %zu / %d lattice nodes really are in Ogre's static "
                        "memory manager (%.0f%%), applied in %.1f ms\n",
                        marked, n, 100.0 * double(marked) / double(n ? n : 1), markMs);
            gCounters["static.nodes." + std::to_string(n)] = double(marked);
            gCounters["static.fraction." + std::to_string(n)] =
                double(marked) / double(n ? n : 1);
            gCounters["static.apply_ms." + std::to_string(n)] = markMs;
            CHECK(marked > 0, "SCENE_STATIC: some of the static-intent population landed");
        }

        const LoopBudget bA{ 3, 10, quick ? 12 : 300, quick ? 300.0 : 2000.0 };

        // (a) document edit -> engine-visible, on the dynamic 20%.
        {
            auto st = runLoop(bA, [&](int it, Recorder &rec) {
                const float d = (it % 2) ? 0.35f : -0.35f;
                const auto t0 = Clock::now();
                for (size_t i = 0; i < s.dynamic.size(); ++i)
                    s.dynamic[i]->setLocalPos(s.dynBase[i] + iris::Vec3(d, 0, 0));
                mirror.sync();
                const double syncMs = msSince(t0);
                enginetest::frames(engine, 1);
                rec.add("a.sync_dynamic", syncMs);
                rec.add("a.transform_update_dynamic", msSince(t0));
            });
            addMetric("a.sync_dynamic", n, st["a.sync_dynamic"]);
            addMetric("a.transform_update_dynamic", n, st["a.transform_update_dynamic"]);
        }

        // (a') the same edit applied to EVERY node.
        {
            auto st = runLoop(bA, [&](int it, Recorder &rec) {
                const float d = (it % 2) ? 0.2f : -0.2f;
                const auto t0 = Clock::now();
                for (size_t i = 0; i < s.all.size(); ++i)
                    s.all[i]->setLocalPos(s.basePos[i] + iris::Vec3(0, d, 0));
                mirror.sync();
                const double syncMs = msSince(t0);
                enginetest::frames(engine, 1);
                rec.add("a.sync_all", syncMs);
                rec.add("a.transform_update_all", msSince(t0));
            });
            addMetric("a.sync_all", n, st["a.sync_all"]);
            addMetric("a.transform_update_all", n, st["a.transform_update_all"]);
        }

        // (u) THE COST OF NOT SETTLING. (a') moved EVERY node, and moving a
        // static node demotes it (rule 4): the whole scene is SCENE_DYNAMIC
        // now, and Ogre updates every dynamic node's transform and bounds every
        // frame. Render alone (no sync, so the settle does not advance) — the
        // number the settle's speed is worth.
        {
            size_t dyn = 0;
            for (const iris::SceneNodePtr &x : s.all) if (!x->isStaticInGraph()) ++dyn;
            std::vector<double> r;
            enginetest::frames(engine, 5);
            for (int i = 0; i < (quick ? 20 : 60); ++i) {
                const auto t0 = Clock::now();
                enginetest::frames(engine, 1);
                r.push_back(msSince(t0));
            }
            const Stats us = computeStats(r);
            std::printf("BENCH %-28s scale=%-6d n=%-4zu median_ms=%9.3f dynamic=%zu\n",
                        "u.unsettled_render", n, us.n, us.median, dyn);
            gCounters["u.unsettled_render." + std::to_string(n) + ".median_ms"] = us.median;
            gCounters["u.unsettled_render." + std::to_string(n) + ".dynamic"] = double(dyn);
        }

        // (s) THE SETTLE, DRAINED AND MEASURED AS ITS OWN SERIES. The mirror
        // re-promotes the scene once every node has been quiet for
        // kStaticSettleFrames syncs, kStaticSettleNodesPerSync visits a sync.
        // A suite warms up before it measures: (b) starts after this.
        {
            // `renders`: the frame AFTER each slice — the promotions it made
            // reach Ogre's static update there (notifyStaticDirty), so the
            // settle's whole price is the slice plus that render. Reported,
            // not bounded: the bound is the slice's (the mirror's own work).
            // `slices` = the sync's THREAD CPU time (the bound); `walls` = its
            // wall time at the same points (printed only).
            auto threadCpuMs = []() {
                timespec ts{};
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
                return double(ts.tv_sec) * 1e3 + double(ts.tv_nsec) * 1e-6;
            };
            std::vector<double> slices, walls, renders;
            int syncs = 0, worstAt = -1, over1 = 0;
            quint32 worstVisits = 0;
            double worstMs = -1.0;
            for (; syncs < 200000 && mirror.staticSettleInProgress(); ++syncs) {
                const auto t0 = Clock::now();
                const double c0 = threadCpuMs();
                mirror.sync();
                const double ms = threadCpuMs() - c0;
                const double wallMs = msSince(t0);
                const bool slice = mirror.staticSettleVisits() > 0;
                if (slice) {
                    walls.push_back(wallMs);
                    if (ms > worstMs) {
                        worstMs = ms;
                        worstAt = int(slices.size());
                        worstVisits = mirror.staticSettleVisits();
                    }
                    if (ms > 1.0) ++over1;
                    slices.push_back(ms);
                }
                const auto t1 = Clock::now();
                enginetest::frames(engine, 1);
                if (slice) renders.push_back(msSince(t1));
            }
            {
                std::vector<double> rs = renders;
                std::sort(rs.begin(), rs.end());
                std::printf("BENCH %-28s scale=%-6d n=%-4zu median_ms=%9.3f p99_ms=%9.3f max_ms=%9.3f\n",
                            "s.settle_render", n, rs.size(), percentile(rs, 0.5), percentile(rs, 0.99),
                            rs.empty() ? 0.0 : rs.back());
                gCounters["s.settle_render." + std::to_string(n) + ".median_ms"] = percentile(rs, 0.5);
                gCounters["s.settle_render." + std::to_string(n) + ".max_ms"] = rs.empty() ? 0.0 : rs.back();
            }
            std::vector<double> sorted = slices;
            std::sort(sorted.begin(), sorted.end());
            const double med = percentile(sorted, 0.5), p95 = percentile(sorted, 0.95),
                         p99 = percentile(sorted, 0.99), mx = sorted.empty() ? 0.0 : sorted.back();
            std::printf("BENCH %-28s scale=%-6d n=%-4zu median_ms=%9.3f p95_ms=%9.3f p99_ms=%9.3f "
                        "max_ms=%9.3f syncs=%d budget=%u worst_at=%d/%zu worst_visits=%u over_1ms=%d (thread CPU)\n",
                        "s.settle_slice", n, slices.size(), med, p95, p99, mx, syncs,
                        SceneMirror::kStaticSettleNodesPerSync, worstAt, slices.size(), worstVisits, over1);
            {
                std::vector<double> ws = walls;
                std::sort(ws.begin(), ws.end());
                const double wmx = ws.empty() ? 0.0 : ws.back();
                std::printf("BENCH %-28s scale=%-6d n=%-4zu median_ms=%9.3f p99_ms=%9.3f max_ms=%9.3f (wall, "
                            "not asserted)\n", "s.settle_slice_wall", n, ws.size(), percentile(ws, 0.5),
                            percentile(ws, 0.99), wmx);
                const std::string kw = "s.settle_slice_wall." + std::to_string(n);
                gCounters[kw + ".median_ms"] = percentile(ws, 0.5);
                gCounters[kw + ".p99_ms"] = percentile(ws, 0.99);
                gCounters[kw + ".max_ms"] = wmx;
            }
            const std::string k = "s.settle_slice." + std::to_string(n);
            gCounters[k + ".median_ms"] = med;
            gCounters[k + ".p99_ms"] = p99;
            gCounters[k + ".max_ms"] = mx;
            gCounters[k + ".slices"] = double(slices.size());
            gCounters[k + ".syncs"] = double(syncs);
            char buf[160];
            std::snprintf(buf, sizeof buf, "s.settle_slice@%d drained (%d syncs)", n, syncs);
            CHECK(!mirror.staticSettleInProgress() && !slices.empty(), buf);
            // A new CHECK on a time here goes under this guard: --smoke asserts nothing about time.
            if (!smoke) {
                std::snprintf(buf, sizeof buf, "s.settle_slice@%d worst slice (thread CPU) %.3f ms <= %.1f ms", n, mx,
                              kSettleSliceMaxMs);
                CHECK(mx <= kSettleSliceMaxMs, buf);
            }
        }

        // (b) work per tick with NOTHING edited, on the SETTLED scene. Long
        // enough (>= 90 frames) that Ogre's rolling FrameStats window describes
        // idle frames and not the edit loops above.
        {
            size_t settledStatic = 0;
            for (const iris::SceneNodePtr &x : s.all) if (x->isStaticInGraph()) ++settledStatic;
            gCounters["static.nodes_idle." + std::to_string(n)] = double(settledStatic);
            const LoopBudget bIdle{ 5, 90, quick ? 90 : 400, quick ? 400.0 : 2000.0 };
            auto st = runLoop(bIdle, [&](int, Recorder &rec) {
                const auto t0 = Clock::now();
                mirror.sync();
                const double syncMs = msSince(t0);
                enginetest::frames(engine, 1);
                rec.add("b.idle_sync", syncMs);
                rec.add("b.idle_tick", msSince(t0));
            });
            addMetric("b.idle_sync", n, st["b.idle_sync"]);
            addMetric("b.idle_tick", n, st["b.idle_tick"]);
            // The settled render, for (u)'s comparison: the idle tick minus its sync.
            gCounters["b.settled_render." + std::to_string(n) + ".median_ms"] =
                st["b.idle_tick"].median - st["b.idle_sync"].median;

            RenderStats rs;
            if (enginetest::renderStats(engine, rs)) {
                std::printf("BENCH %-28s scale=%-6d frame_ms=%7.3f p95_ms=%7.3f fps=%6.1f "
                            "draws=%llu batches=%llu triangles=%llu recording=%d\n",
                            "b.render_stats", n, rs.frameMs, rs.p95Ms, rs.fps,
                            rs.draws, rs.batches, rs.triangles, int(rs.metricsRecording));
                gCounters["b.render_stats." + std::to_string(n) + ".frame_ms"] = rs.frameMs;
                gCounters["b.render_stats." + std::to_string(n) + ".p95_ms"] = rs.p95Ms;
                gCounters["b.render_stats." + std::to_string(n) + ".draws"] = double(rs.draws);
                gCounters["b.render_stats." + std::to_string(n) + ".batches"] = double(rs.batches);
                gCounters["b.render_stats." + std::to_string(n) + ".triangles"] = double(rs.triangles);
                CHECK(rs.frameMs > 0.0, "renderStats reports a frame time");
            } else {
                std::printf("FAIL: renderStats unavailable at scale %d\n", n);
                ++failures;
            }
        }

        // (c) reparent a 500-node subtree, alternating between the two hosts.
        {
            const LoopBudget bRep{ 2, 10, quick ? 10 : 60, quick ? 400.0 : 2500.0 };
            // THE SPLIT (ENGINE-UPGRADE-1 stage 2): `_sync` is the two document calls AND the mirror's sync —
            // each timed on its own, and the first iteration's COUNTS printed (the mirror's walk, the engine's
            // structural edits): the cause of a cost that grows with the scene is a count that does.
            auto st = runLoop(bRep, [&](int it, Recorder &rec) {
                iris::SceneNodePtr from = (it % 2) ? s.hostB : s.hostA;
                iris::SceneNodePtr to   = (it % 2) ? s.hostA : s.hostB;
                const quint64 pushes0 = mirror.visitPushCount();
                const SceneChangeStats cs0 = es->published().changes;
                const auto t0 = Clock::now();
                from->removeChild(s.subtree);
                const auto t1 = Clock::now();
                to->addChild(s.subtree, false);
                const auto t2 = Clock::now();
                const int visited = mirror.sync();
                const auto t3 = Clock::now();
                const double syncMs = msSince(t0);
                enginetest::frames(engine, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
                const SceneChangeStats cs1 = es->published().changes;
                enginetest::frames(engine, 1);
                rec.add("c.reparent_500_remove", std::chrono::duration<double, std::milli>(t1 - t0).count());
                rec.add("c.reparent_500_add", std::chrono::duration<double, std::milli>(t2 - t1).count());
                rec.add("c.reparent_500_mirror", std::chrono::duration<double, std::milli>(t3 - t2).count());
                rec.add("c.reparent_500_sync", syncMs);
                rec.add("c.reparent_500", msSince(t0));
                if (it < 3)   // two warm-ups and the first measured iteration
                    std::printf("COUNT c.reparent_500 scale=%d visited=%d dirty=%llu evicted=%llu pushes=%llu "
                                "materials=%llu settleVisits=%u walk=%s itemsDestroyed=%llu itemsCreated=%llu "
                                "giInvalidations=%llu probeRestarts=%llu\n",
                                n, visited, (unsigned long long)mirror.dirtyNodeCount(),
                                (unsigned long long)mirror.evictedNodeCount(),
                                (unsigned long long)(mirror.visitPushCount() - pushes0),
                                (unsigned long long)mirror.materialBuildCount(), unsigned(mirror.staticSettleVisits()),
                                mirror.walkMode(), cs1.itemsDestroyed - cs0.itemsDestroyed,
                                cs1.itemsCreated - cs0.itemsCreated, cs1.giInvalidations - cs0.giInvalidations,
                                cs1.probeBuildRestarts - cs0.probeBuildRestarts);
            });
            addMetric("c.reparent_500_remove", n, st["c.reparent_500_remove"], true);
            addMetric("c.reparent_500_add", n, st["c.reparent_500_add"], true);
            addMetric("c.reparent_500_mirror", n, st["c.reparent_500_mirror"], true);
            addMetric("c.reparent_500_sync", n, st["c.reparent_500_sync"], true);
            addMetric("c.reparent_500", n, st["c.reparent_500"], true);
        }

        // UNBIND before the engine scene dies. Since the scene-graph swap the
        // document's nodes ARE this scene manager's nodes (SCENEGRAPH_SPEC D2),
        // so destroying it first would leave the document holding stale
        // handles. Outside every measured region.
        mirror.setSource(nullptr);
        view->setScene(nullptr);
        engine->destroyScene(es);
    }

    // ---- (d) the animated 200-node rig ----------------------------------
    {
        std::printf("\n== animated rig (200 nodes, property anims) ==\n");
        Scene *es = engine->createScene("rig");
        if (es) {
            es->setAmbient(Colour(0.35f, 0.35f, 0.35f), Colour(0.2f, 0.2f, 0.2f));
            view->setScene(es);
            std::vector<iris::SceneNodePtr> bones;
            auto doc = buildAnimRig(200, bones);
            enginetest::testCameraLookAt(view, Vec3(14, 10, 18), Vec3(6, 3, 0));
            SceneMirror mirror(es);
            mirror.setSource(doc);
            mirror.sync();
            enginetest::frames(engine, 1);

            const LoopBudget bRig{ 5, 30, quick ? 40 : 400, quick ? 400.0 : 2000.0 };
            float t = 0.0f;
            int rigIt = 0;
            auto st = runLoop(bRig, [&](int, Recorder &rec) {
                t += 1.0f / 60.0f;
                const quint64 pushes0 = mirror.visitPushCount();
                const SceneChangeStats cs0 = es->published().changes;
                const auto t0 = Clock::now();
                doc->getRootNode()->updateAnimation(t);
                const int visited = mirror.sync();
                const double syncMs = msSince(t0);
                enginetest::frames(engine, 1);
                rec.add("d.anim_rig_200_sync", syncMs);
                rec.add("d.anim_rig_200", msSince(t0));
                // THE ANIMATED FRAME'S COUNTS (SCENEGRAPH-BASELINE-2): 200 written nodes = 200 transform records,
                // each served by the journal's fast path (no walk visit), no push, no material build, no structural edit.
                if (rigIt++ < 3) {
                    const SceneChangeStats cs1 = es->published().changes;
                    std::printf("COUNT d.anim_rig_200 visited=%d fastServed=%d dirty=%llu pushes=%llu materials=%llu "
                                "records=%u walk=%s itemsCreated=%llu giInvalidations=%llu\n",
                                visited, mirror.fastPathServedCount(), (unsigned long long)mirror.dirtyNodeCount(),
                                (unsigned long long)(mirror.visitPushCount() - pushes0),
                                (unsigned long long)mirror.materialBuildCount(),
                                engine->published().input.nodeTransforms, mirror.walkMode(),
                                cs1.itemsCreated - cs0.itemsCreated, cs1.giInvalidations - cs0.giInvalidations);
                }
            });
            addMetric("d.anim_rig_200_sync", 200, st["d.anim_rig_200_sync"], true);
            addMetric("d.anim_rig_200", 200, st["d.anim_rig_200"], true);

            mirror.setSource(nullptr);   // see the note in the scale loop
            view->setScene(nullptr);
            engine->destroyScene(es);
        } else { std::printf("FAIL: engine scene for the rig\n"); ++failures; }
    }

    // ---- stability + "numbers exist" ------------------------------------
    // DISPERSION IS A QUIET-BOX MEASUREMENT. --smoke runs 12 iterations at one
    // scale on a box with three other suites on it; a cv bound over that is a
    // coin flip, which is exactly the thing this mode exists to stop paying for.
    if (smoke) std::printf("\n== stability == skipped (--smoke measures nothing)\n");
    else std::printf("\n== stability ==\n");
    for (const Metric &m : gMetrics) {
        if (smoke) break;
        if (m.single >= 0.0) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s@%d produced a number", m.id.c_str(), m.scale);
            CHECK(m.single > 0.0, buf);
            continue;
        }
        char buf[200];
        const double cvMax = m.reparentClass ? kCvMaxReparent : kCvMax;
        const double rcvMax = m.reparentClass ? kRcvMaxReparent : kRcvMax;
        std::snprintf(buf, sizeof buf, "%s@%d n=%zu median=%.3f ms > 0", m.id.c_str(), m.scale, m.stats.n, m.stats.median);
        CHECK(m.stats.n >= 10 && m.stats.median > 0.0, buf);
        std::snprintf(buf, sizeof buf, "%s@%d cv %.3f <= %.2f", m.id.c_str(), m.scale, m.stats.cv, cvMax);
        CHECK(m.stats.cv <= cvMax, buf);
        std::snprintf(buf, sizeof buf, "%s@%d rcv %.3f <= %.2f", m.id.c_str(), m.scale, m.stats.rcv, rcvMax);
        CHECK(m.stats.rcv <= rcvMax, buf);
    }

    if (!recordPath.empty()) {
        std::string gpu = "unknown";
        // The backend names the device in its own log ("Device Name: ..."),
        // which is the exact string the driver reported. Anchored on that one
        // key: a looser match picks up the log's CPU ID line instead.
        QFile log("bench_scenegraph-ogre.log");
        if (log.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> lines = log.readAll().split('\n');
            for (const QByteArray &line : lines) {
                const int k = line.indexOf("Device Name:");
                if (k >= 0) { gpu = line.mid(k + 12).trimmed().toStdString(); break; }
            }
        }
        if (!writeResults(recordPath, gpu, note)) ++failures;
    }

    engine->destroyView(view);
    engine.reset();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
