// THE RIG-PERF BENCH (SPECS/AVATAR_RIG_PERF_SPEC.md P0 / P3).
//
// WHY IT EXISTS. The rig-perf program's whole claim is a set of NUMBERS about a
// character made of several skinned pieces (§1):
//
//   * SkeletonInstances evaluated per frame  = one per PIECE today, one per
//     CHARACTER after sharing (§3.5);
//   * setClipStates calls per frame          = one per PIECE today, one per
//     CHARACTER after (SceneMirror::clipStatePushes);
//   * bone matrices streamed to the Hlms tex buffer per pass = the identity
//     blend-index map makes every piece stream the WHOLE rig, so today it is
//     pieces x union; after the remap it is Sigma piece-local bones (§1 row 4).
//
// So the numbers are recorded FIRST, on the unmodified code, into
// `rigperf-baseline-linux.json`, and the same binary is what the P3 gate runs.
// A program whose acceptance is "faster" and whose before-number was never
// measured is an opinion.
//
// WHAT IT BUILDS. `--characters N` (default 4) copies of the synthetic
// five-piece character in tests/skeletal/multipiecerig.h (AVATAR_RIG_PERF_SPEC
// D6 — subsets, differing bone orders, one shared bind pose; no Mixamo file may
// live in the tree), each playing one clip, mirrored into a real engine scene
// and rendered offscreen. Four characters is the spec's own example scene.
//
// WHAT IT MEASURES
//   r.instances            SkeletonInstances alive on the rigged nodes
//   r.clip_pushes_frame    setClipStates calls per rendered tick
//   r.streamed_bones       Sigma over rigged nodes of the bone matrices the Hlms
//                          streams for that node per pass (its blend-index map)
//   t.sync_ms              SceneMirror::sync() on an animating scene
//   t.tick_ms              sync + renderOneFrame — the host's per-tick work
//   render.*               the engine's own RenderStats beside it, unmodified
//
// MODES
//   --assert               the gate. P0: sanity only (the scene really is
//                          rigged and animating, the dispersion is usable).
//                          P3 arms the structural equalities.
//   --no-share             THE BEFORE ARM for the per-frame cost. Nudges every
//                          piece a millimetre off its master, which is above the
//                          mirror's world-transform tolerance, so sharing is
//                          refused for all of them — by the shipped rule ("a
//                          piece the user MOVES un-shares itself"), not by a
//                          second code path kept alive to measure against. The
//                          result is one SkeletonInstance and one clip push PER
//                          PIECE, which is exactly what every character cost
//                          before this program. (The blend-index remap is not
//                          disabled by it: that half is a per-pass GPU upload,
//                          counted as `r.streamed_bones`, and its before-value
//                          is arithmetic — pieces x union, 20 x 8 = 160.)
//   --record <file>        write the JSON result/baseline file.
//   --characters N         scene size (default 4).
//   --quick                short budgets, for editing this file.
//
// RUN REQUIREMENTS: a reachable X display (Ogre's VulkanXcbSupport connects at
// plugin load) and a Vulkan driver (lavapipe is enough); the view is offscreen
// and QT_QPA_PLATFORM=offscreen, so nothing is ever shown.

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

#include "../skeletal/multipiecerig.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;
using Clock = std::chrono::steady_clock;

// THE STRUCTURAL GATE (AVATAR_RIG_PERF_SPEC §7 P3), armed by the phase that
// earns it. At P0 the bench only RECORDS: the three equalities below are false
// by construction on the unmodified code (five instances, five clip pushes and
// five whole-rig bone streams per character), which is the entire point of
// recording them first.
static constexpr bool kStructuralGateArmed = false;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---------------------------------------------------------------------------
struct Stats { size_t n = 0; double median = 0, mean = 0, min = 0, max = 0, rcv = 0; };

static double percentile(const std::vector<double> &sorted, double p)
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
    s.n = v.size(); s.min = v.front(); s.max = v.back();
    s.median = percentile(v, 0.5);
    double sum = 0; for (double x : v) sum += x;
    s.mean = sum / double(v.size());
    std::vector<double> dev; dev.reserve(v.size());
    for (double x : v) dev.push_back(std::fabs(x - s.median));
    std::sort(dev.begin(), dev.end());
    // The ROBUST dispersion only (1.4826*MAD/median): the classic cv is
    // dominated by one scheduler hiccup at these sample counts, and this bench
    // never gates a millisecond count — only whether the measurement is usable.
    s.rcv = s.median > 0 ? (1.4826 * percentile(dev, 0.5)) / s.median : 0.0;
    return s;
}

static double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static QJsonObject gMetrics;
static QJsonObject gCounters;

static void recordMetric(const char *id, const Stats &s)
{
    QJsonObject o;
    o["n"] = int(s.n); o["median_ms"] = s.median; o["mean_ms"] = s.mean;
    o["min_ms"] = s.min; o["max_ms"] = s.max; o["rcv"] = s.rcv;
    gMetrics[QString::fromLatin1(id)] = o;
    std::printf("BENCH %-22s n=%-4zu median_ms=%9.3f mean_ms=%9.3f min_ms=%9.3f "
                "max_ms=%9.3f rcv=%5.3f\n", id, s.n, s.median, s.mean, s.min, s.max, s.rcv);
}

static void recordCounter(const char *id, double v)
{
    gCounters[QString::fromLatin1(id)] = v;
    std::printf("BENCH %-22s %12.3f\n", id, v);
}

int main(int argc, char **argv)
{
    bool assertMode = false, quick = false, noShare = false;
    int characters = 4;
    std::string recordPath, note;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--assert") assertMode = true;
        else if (a == "--quick") quick = true;
        else if (a == "--record" && i + 1 < argc) recordPath = argv[++i];
        else if (a == "--characters" && i + 1 < argc) characters = std::atoi(argv[++i]);
        else if (a == "--no-share") noShare = true;
        else if (a == "--note" && i + 1 < argc) note = argv[++i];
        else { std::printf("bench_rigperf [--assert] [--record <file>] [--characters N] "
                           "[--note <text>] [--quick]\n"); return 2; }
    }
    if (characters < 1) characters = 1;

    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "bench_rigperf-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("bench", 320, 240, Colour(0, 0, 0.2f));
    CHECK(view != nullptr, "offscreen view");
    if (!view) return 1;
    // WALL-CLOCK is what the engine charges per frame, and an offscreen frame is
    // ~1 ms — every time-driven thing inside would see a clock no editor has.
    engine->setFixedFrameDelta(1.0f / 60.0f);
    { RenderStats warm; engine->renderStats(warm); }

    Scene *es = engine->createScene("rigperf");
    view->setScene(es);
    es->setAmbient(Colour(0.5f, 0.5f, 0.5f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::testCameraLookAt(view, Vec3(0, 1.5f, 8), Vec3(0, 1.2f, 0));

    // ---- the document ----------------------------------------------------
    auto doc = iris::Scene::create();
    std::vector<multipiece::Character> cast;
    for (int i = 0; i < characters; ++i) {
        multipiece::Character c = multipiece::buildCharacter(QString("char%1").arg(i));
        if (noShare) {
            // A millimetre apart: above the mirror's world-transform tolerance,
            // so no piece may share the master's instance (Ogre's shared bones
            // carry the MASTER's node transform, so a piece that is not where
            // the master is may not use them). One instance and one clip push
            // per piece — the pre-program cost.
            for (int p = 0; p < c.pieces.size(); ++p)
                c.pieces[p]->setLocalPos(iris::Vec3(0.001f * float(p + 1), 0, 0));
        }
        c.root->setLocalPos(iris::Vec3(float(i) * 1.5f - float(characters - 1) * 0.75f, 0, 0));
        auto clip = multipiece::buildCharacterClip();
        clip->setName("Walk");
        clip->setLooping(true);
        c.root->addAnimation(clip);
        c.root->setAnimation(clip);
        doc->getRootNode()->addChild(c.root, false);
        cast.push_back(c);
    }

    SceneMirror mirror(es);
    mirror.setSource(doc);
    doc->updateSceneAnimation(0.0f);
    mirror.sync();
    for (int i = 0; i < 4; ++i) engine->renderOneFrame();

    // ---- structural numbers ---------------------------------------------
    size_t rigged = 0, streamed = 0;
    for (const multipiece::Character &c : cast)
        for (const iris::MeshNodePtr &piece : c.pieces) {
            const NodeId n = mirror.engineNode(piece.data());
            if (!n || !es->hasSkeleton(n)) continue;
            ++rigged;
            streamed += es->streamedBoneCount(n);
        }
    const RigStats rs = es->rigStats();
    recordCounter("scene.characters", double(characters));
    recordCounter("scene.pieces", double(characters * multipiece::pieces().size()));
    recordCounter("r.rigged_nodes", double(rigged));
    recordCounter("r.instances", double(rs.instances));
    recordCounter("r.shared", double(rs.shared));
    recordCounter("r.streamed_bones", double(streamed));
    CHECK(rigged == size_t(characters) * size_t(multipiece::pieces().size()),
          "every piece of every character is rigged engine-side");
    std::printf("    arm: %s\n", noShare ? "BEFORE (one instance + one clip push per piece)"
                                          : "AFTER (one per character)");
    CHECK(rs.streamedBones == streamed, "rigStats agrees with the per-node stream counts");

    // ---- the per-tick measurement ---------------------------------------
    // ONE loop, two timings: a tick is sync + renderOneFrame, and the sync half
    // is timed inside it — measuring them in separate loops would measure two
    // different scene states.
    const int warmup = 5;
    const int iters = quick ? 30 : 240;
    std::vector<double> syncMs, tickMs;
    syncMs.reserve(size_t(iters)); tickMs.reserve(size_t(iters));
    const quint64 pushesBefore = mirror.clipStatePushes();
    float t = 0.0f;
    for (int i = 0; i < warmup + iters; ++i) {
        t += 1.0f / 60.0f;
        if (t > 0.98f) t -= 0.98f;
        doc->updateSceneAnimation(t);
        const auto tick0 = Clock::now();
        mirror.sync();
        const double sync = msSince(tick0);
        engine->renderOneFrame();
        const double tick = msSince(tick0);
        if (i < warmup) continue;
        syncMs.push_back(sync);
        tickMs.push_back(tick);
    }
    const double pushesPerFrame = double(mirror.clipStatePushes() - pushesBefore) / double(iters);

    recordMetric("t.sync_ms", computeStats(syncMs));
    recordMetric("t.tick_ms", computeStats(tickMs));
    recordCounter("r.clip_pushes_frame", pushesPerFrame);

    RenderStats stats;
    if (engine->renderStats(stats)) {
        recordCounter("render.frame_ms", stats.frameMs);
        recordCounter("render.draws", double(stats.draws));
        recordCounter("render.triangles", double(stats.triangles));
    }

    // ---- the gate --------------------------------------------------------
    if (assertMode) {
        CHECK(computeStats(tickMs).rcv < 0.35, "tick measurement is usable (rcv < 0.35)");
        CHECK(pushesPerFrame > 0.5, "the scene really is pushing clips every frame");
        if (kStructuralGateArmed && !noShare) {
            CHECK(rs.instances == size_t(characters),
                  "one SkeletonInstance per character (P1b)");
            CHECK(std::fabs(pushesPerFrame - double(characters)) < 0.02,
                  "one setClipStates call per character per frame (P1b)");
            CHECK(streamed == size_t(characters) * multipiece::pieceLocalBoneTotal(),
                  "streamed bones == characters * Sigma piece-local bones (P1a)");
        }
    }

    if (!recordPath.empty()) {
        QJsonObject root;
        root["schema"] = 1;
        root["recorded"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        root["commit"] = QString::fromLatin1(BENCH_GIT_COMMIT);
        root["build_type"] = QString::fromLatin1(BENCH_BUILD_TYPE);
        root["compiler"] = QString::fromLatin1(BENCH_COMPILER);
        root["characters"] = characters;
    root["arm"] = noShare ? "before (pieces nudged apart: one instance and one clip push per piece)"
                          : "after (character rig + one shared instance per character)";
        root["note"] = QString::fromStdString(note);
        root["metrics"] = gMetrics;
        root["counters"] = gCounters;
        QFile f(QString::fromStdString(recordPath));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            std::printf("wrote %s\n", recordPath.c_str());
        } else {
            std::printf("FAIL: could not write %s\n", recordPath.c_str());
            ++failures;
        }
    }

    mirror.setSource(nullptr);
    view->setScene(nullptr);
    engine->destroyScene(es);
    std::printf(failures ? "\nFAILURES: %d\n" : "\nall good\n", failures);
    return failures ? 1 : 0;
}
