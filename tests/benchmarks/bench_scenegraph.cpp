// The SCENE-GRAPH BENCHMARK GATE — SPECS/SCENEGRAPH_SPEC.md §6.
//
// WHY THIS EXISTS AT ALL. The scene-graph adoption program (owner decision D1)
// accepts a performance case: Ogre-Next's SoA/threaded/SIMD graph replaces the
// iris:: document graph, and "capturing those wins is a REQUIREMENT, not a
// hope". A requirement nobody measured before the swap is a claim. So this
// suite is built and RUN FIRST, on `ogre` HEAD, with v1 not written yet: it
// records what the current graph costs into `baseline-linux.json`, and the same
// binary re-run in v1's gate is what turns §6's acceptance line
//
//     "(a) and (b) improve at 10k+; nothing regresses >10% at 1k"
//
// into a number instead of an opinion.
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
//   b.idle_sync                  the sync half of that tick.
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
// RUN-TO-RUN REPRODUCIBILITY, measured rather than assumed: the committed
// baseline was recorded twice back to back on a quiet box, and every one of the
// 33 medians agreed within ±4.5% (most within ±2%, worst case
// a.transform_update_all@10000 at -4.5% and e.build_doc@10000 at +3.9%). §6's
// "nothing regresses >10% at 1k" therefore has roughly 3x margin over the
// measurement itself — but only on this box, in this build type, quiet. A v1
// comparison run while another worktree compiles is not evidence of anything.
//
// MODES
//   --assert            (default) run, print, assert stability + numbers exist.
//   --record <file>     run and write the JSON baseline/result file.
//   --compare <file>    load a baseline and print the §6 verdict per metric.
//                       DISARMED: it reports, it cannot fail the run. v1's lane
//                       flips kBaselineComparisonArmed (see below).
//   --scales 1000,10000 override the scale list.
//   --note <text>       free text stored in the recorded file (run conditions).
//   --quick             short budgets, for editing this file.
//
// RUN REQUIREMENTS: same as every engine suite — a reachable X display (Ogre's
// VulkanXcbSupport connects at plugin load) and a Vulkan driver; the view is
// offscreen and QT_QPA_PLATFORM=offscreen, so nothing is ever shown.

#include <QGuiApplication>
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
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
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
static constexpr double kRcvMaxReparent = 0.35;

// THE V1 HOOK (SPECS/SCENEGRAPH_SPEC.md §6 acceptance) — ARMED by the v1 lane
// that landed the handle layer, exactly as this comment promised. The gate now
// enforces §6 verbatim:
//   * at scales >= kImproveFromScale, every a.* and b.* metric must IMPROVE
//     (median lower than the baseline's) — §6 "(a) and (b) improve at 10k+";
//   * at scale 1000, no metric may regress by more than kMaxRegressPct — §6
//     "nothing regresses >10% at 1k".
static constexpr bool   kBaselineComparisonArmed = true;
// WHAT THE ARMED GATE SAYS TODAY (v1 lane, 2026-09-05), recorded here because a
// red gate whose reason is not written down gets tuned by the next person who
// meets it:
//
//   §6's two clauses split. "(a) and (b) improve at 10k+" is met by a wide
//   margin at every scale (a.* -19% to -37%, b.* -19% to -27%, d.anim_rig
//   -50%). "Nothing regresses >10% at 1k" FAILS on exactly one metric:
//   e.build_doc, at +17% (measured over six runs of one identical build:
//   9.07 / 9.21 / 9.34 / 9.36 / 9.43 / 10.01 ms against a 7.956 ms baseline).
//
//   The reason is structural, not a defect: building a document now creates a
//   real Ogre scene node per document node — ~1.9 us of scene-manager and SoA
//   machinery where the old graph wrote three C++ members. About 40% of that is
//   the RE-PARENT (Node::setParent migrates the node between depth levels of
//   the NodeMemoryManager), which the universal "create it, configure it, THEN
//   addChild it" call shape forces. Removing that needs deferred realization —
//   holding a transient TRS on the handle until it has a parent — which is
//   exactly the second transform store the design deleted, so it is a decision
//   above this file, not a tweak.
//
//   What it costs is also handed straight back: e.first_sync falls by the same
//   order (the mirror no longer creates a node per document node), so
//   END-TO-END document build + first sync is flat — 1k +0.8%, 10k +0.6%,
//   50k -2.2%.
//
//   Note also that e.build_doc and e.first_sync are SINGLE-SAMPLE metrics
//   (addSingle, n = 1): they carry no dispersion, so the stability bounds above
//   cannot say whether their measurement is usable at all.
static constexpr int    kImproveFromScale = 10000;
static constexpr double kMaxRegressPct    = 10.0;

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
            if (iris::graph::canBeStatic(n->graphNode())) n->setStaticHint(true);
        } else if (n->isStaticInGraph()) {
            // It inherited static from a static parent; the tag says it moves.
            n->setStaticHint(false);
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
    gCube = iris::Mesh::loadMesh(":assets/models/cube.obj");
    auto mat = iris::DefaultMaterial::create();
    mat->setDiffuseColor(QColor(204, 96, 51));
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

// ---------------------------------------------------------------------------
// JSON: write the result file, and read a baseline back.

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

/// §6's acceptance, computed. Report-only until kBaselineComparisonArmed.
static void compareToBaseline(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly)) {
        std::printf("note: no baseline at %s — comparison skipped\n", path.c_str());
        return;
    }
    const QJsonObject base = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject bm = base.value("metrics").toObject();
    std::printf("\n--- §6 comparison against %s (commit %s, %s)%s ---\n", path.c_str(),
                base.value("commit").toString().toUtf8().constData(),
                base.value("config").toObject().value("build_type").toString().toUtf8().constData(),
                kBaselineComparisonArmed ? "" : "   [REPORT ONLY — not armed]");
    int violations = 0;
    for (const Metric &m : gMetrics) {
        const QJsonObject byScale = bm.value(QString::fromStdString(m.id)).toObject();
        const QJsonValue v = byScale.value(QString::number(m.scale));
        if (!v.isObject()) continue;
        const double b = v.toObject().value("median_ms").toDouble();
        const double now = m.single >= 0.0 ? m.single : m.stats.median;
        if (b <= 0.0) continue;
        const double pct = (now - b) / b * 100.0;
        const bool gated = m.id.compare(0, 2, "a.") == 0 || m.id.compare(0, 2, "b.") == 0;
        // e.build_doc is EXEMPT from the regress clause — OWNER DECISION,
        // 2026-09-06 (SCENEGRAPH_SPEC §6a): creating a real engine node per
        // document node costs ~+17% at 1k over three member writes, end-to-end
        // build+first_sync is flat-to-better, and "we are all in on ogre...
        // accept the costs, we can streamline over time." The setParent
        // depth-migration (~40% of the cost) is the recorded streamlining
        // candidate. Every other metric keeps the full 10% clause.
        const bool regressExempt = m.id == "e.build_doc";
        const char *verdict = "     ";
        if (gated && m.scale >= kImproveFromScale && pct >= 0.0) { verdict = "MUST-IMPROVE"; ++violations; }
        else if (!regressExempt && m.scale < kImproveFromScale && pct > kMaxRegressPct) { verdict = "REGRESSED>10%"; ++violations; }
        else if (regressExempt && pct > kMaxRegressPct) { verdict = "accepted-§6a"; }
        std::printf("  %-28s scale=%-6d base=%9.3f now=%9.3f  %+7.1f%%  %s\n",
                    m.id.c_str(), m.scale, b, now, pct, verdict);
    }
    std::printf("--- %d violation(s) ---\n", violations);
    if (kBaselineComparisonArmed) failures += violations;
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    std::string recordPath, comparePath;
    std::string note = "recorded by tests/benchmarks/bench_scenegraph.cpp";
    std::vector<int> scales{1000, 10000, 50000};
    bool quick = false;
    bool assertMode = true;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--assert") assertMode = true;
        else if (a == "--record" && i + 1 < argc) { recordPath = argv[++i]; assertMode = false; }
        else if (a == "--compare" && i + 1 < argc) comparePath = argv[++i];
        else if (a == "--note" && i + 1 < argc) note = argv[++i];
        else if (a == "--quick") quick = true;
        else if (a == "--scales" && i + 1 < argc) {
            scales.clear();
            std::string list = argv[++i], cur;
            for (char c : list + ",") { if (c == ',') { if (!cur.empty()) scales.push_back(std::stoi(cur)); cur.clear(); } else cur += c; }
        } else if (a == "--help") {
            std::printf("bench_scenegraph [--assert] [--record <file>] [--compare <file>] "
                        "[--scales 1000,10000,50000] [--quick] [--note <text>]\n");
            return 0;
        }
    }

    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "bench_scenegraph-ogre.log";
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
    engine->setFixedFrameDelta(1.0f / 60.0f);
    // ARM the geometry counters. Engine::renderStats is lazy by contract: the
    // FIRST call switches Ogre's per-draw counting on and reports zeros with
    // metricsRecording=false. Asking here, before anything is built, means
    // every later read in the sweep carries real draws/batches/triangles.
    { RenderStats warm; engine->renderStats(warm); }

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
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        Image img;
        view->readPixels(img);
        const Colour at = img.at(img.width / 2, img.height / 2);
        std::printf("    cube at origin: centre %3.0f %3.0f %3.0f\n", at.r * 255, at.g * 255, at.b * 255);
        CHECK(at.r > 0.10f && at.r > at.b * 1.5f, "centre pixel shows the mirrored cube");
        mn->setLocalPos(iris::Vec3(50.0f, 0.0f, 0.0f));
        mirror.sync();
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const Colour away = img.at(img.width / 2, img.height / 2);
        std::printf("    moved +50x:     centre %3.0f %3.0f %3.0f\n", away.r * 255, away.g * 255, away.b * 255);
        CHECK(away.b > 0.8f && away.r < 0.15f, "the edit reached the rendered frame (background)");
        mirror.setSource(nullptr);   // unbind before the engine scene dies
        view->setScene(nullptr);
        engine->destroyScene(es);
    }

    // ---- the scale sweep: metrics (a), (b), (c) --------------------------
    for (int n : scales) {
        std::printf("\n== scale %d ==\n", n);
        Scene *es = engine->createScene(("bench" + std::to_string(n)).c_str());
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
        const auto tFirst = Clock::now();
        const int mirrored = mirror.sync();
        const double firstSyncMs = msSince(tFirst);
        engine->renderOneFrame();
        std::printf("    document %d nodes (%zu dynamic), mirror reported %d, first sync %.1f ms\n",
                    s.totalNodes, s.dynamic.size(), mirrored, firstSyncMs);
        CHECK(mirrored == s.totalNodes, "every synthetic node reached the engine");

        addSingle("e.build_doc", n, buildMs);
        addSingle("e.first_sync", n, firstSyncMs);

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
                engine->renderOneFrame();
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
                engine->renderOneFrame();
                rec.add("a.sync_all", syncMs);
                rec.add("a.transform_update_all", msSince(t0));
            });
            addMetric("a.sync_all", n, st["a.sync_all"]);
            addMetric("a.transform_update_all", n, st["a.transform_update_all"]);
        }

        // (b) work per tick with NOTHING edited. Long enough (>= 90 frames) that
        // Ogre's rolling FrameStats window describes idle frames and not the
        // edit loops above.
        {
            // Metric (a') above moved EVERY node, and moving a static node
            // demotes it (rule 4) — so the static population has to be put
            // back before the idle measurement, or (b) would measure a fully
            // dynamic scene and report the win as zero.
            const size_t remarked = applyStaticTags(s);
            gCounters["static.nodes_idle." + std::to_string(n)] = double(remarked);
            const LoopBudget bIdle{ 5, 90, quick ? 90 : 400, quick ? 400.0 : 2000.0 };
            auto st = runLoop(bIdle, [&](int, Recorder &rec) {
                const auto t0 = Clock::now();
                mirror.sync();
                const double syncMs = msSince(t0);
                engine->renderOneFrame();
                rec.add("b.idle_sync", syncMs);
                rec.add("b.idle_tick", msSince(t0));
            });
            addMetric("b.idle_sync", n, st["b.idle_sync"]);
            addMetric("b.idle_tick", n, st["b.idle_tick"]);

            RenderStats rs;
            if (engine->renderStats(rs)) {
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
            auto st = runLoop(bRep, [&](int it, Recorder &rec) {
                iris::SceneNodePtr from = (it % 2) ? s.hostB : s.hostA;
                iris::SceneNodePtr to   = (it % 2) ? s.hostA : s.hostB;
                const auto t0 = Clock::now();
                from->removeChild(s.subtree);
                to->addChild(s.subtree, false);
                mirror.sync();
                const double syncMs = msSince(t0);
                engine->renderOneFrame();
                rec.add("c.reparent_500_sync", syncMs);
                rec.add("c.reparent_500", msSince(t0));
            });
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
            engine->renderOneFrame();

            const LoopBudget bRig{ 5, 30, quick ? 40 : 400, quick ? 400.0 : 2000.0 };
            float t = 0.0f;
            auto st = runLoop(bRig, [&](int, Recorder &rec) {
                t += 1.0f / 60.0f;
                const auto t0 = Clock::now();
                doc->getRootNode()->updateAnimation(t);
                mirror.sync();
                const double syncMs = msSince(t0);
                engine->renderOneFrame();
                rec.add("d.anim_rig_200_sync", syncMs);
                rec.add("d.anim_rig_200", msSince(t0));
            });
            addMetric("d.anim_rig_200_sync", 200, st["d.anim_rig_200_sync"], true);
            addMetric("d.anim_rig_200", 200, st["d.anim_rig_200"], true);

            mirror.setSource(nullptr);   // see the note in the scale loop
            view->setScene(nullptr);
            engine->destroyScene(es);
        } else { std::printf("FAIL: engine scene for the rig\n"); ++failures; }
    }

    // ---- stability + "numbers exist" ------------------------------------
    std::printf("\n== stability ==\n");
    for (const Metric &m : gMetrics) {
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

    if (!comparePath.empty()) compareToBaseline(comparePath);
    else if (assertMode) compareToBaseline(BENCH_BASELINE_FILE);

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
