// physics.mt_determinism / physics.step_cost / physics.step_overlap — THE PHYSICS STEP ON BULLET'S MULTI-THREADED WORLD
// (ENGINE-UPGRADE-U4 stage 6, decisions 20/21/23).
//
// A pile of N boxes dropped in layers onto a ground, stepped by the document (Scene::advance at the fixed 1/60 step:
// the rigid-body solve and the write-back), the frames COUNTED. Bullet's own task scheduler is bound ONCE on this
// thread (Environment::bindGameThread: this thread is the game thread, Bullet's index 0) with `--threads T`; the arms:
//
//   seq         the worlds stepped on Bullet's sequential scheduler (one thread) — the single-threaded reference
//   mt          the worlds stepped on the bound scheduler (T threads) — the SHIPPED world: Bullet's in-island batched
//               solve on (BULLET-BATCHING-1)
//   mt-nobatch  the mt arm with the in-island batching OFF — the pair (batching on / off) in one process
//
// Per arm: the step's median per frame, its share of a 60 Hz frame, the bodies Bullet integrated, the process CPU per
// frame (Bullet's spinning workers show there), and every body's final pose. THE DETERMINISM IS BOUNDED, never
// bit-equal (decision 23.3: Bullet MT merges new manifolds in thread-index order, btCollisionDispatcherMt.cpp:150-161,
// and sorts islands with an unstable quickSort, btSimulationIslandManagerMt.cpp:470 — the contact order a step solves
// in moves with the threads' timing): the mt arm against the seq arm, and the mt arm against itself across `--runs`.
//
//   --bodies N --frames F --threads T --runs R (the mt arm's repeats, default 1)
//   --threads budget  Bullet's threads from the ONE core budget (irisgl/core/threadbudget.h: the game thread + the
//                  foreground share — what the app binds; THREADING-1)
//   --warmup W     the medians skip the first W frames (the pile's first contacts, the mirror's first pushes)
//   --bar MS       THE MANY MOVERS' BAR (physics.many_movers, THREADING-1 stage 2): the mt arm's step + mirror sync,
//                  per frame, median at most MS (with --mirror --mt-only)
//   --beside-import FILE  THE CORE BUDGET'S PREMISE (THREADING-1 stage 1, by hand, once): the mt arm idle, then the
//                  same arm while FILE is imported and baked over and over on Qt's global pool and the bake pool (the
//                  import's two pools, capped by the budget as the app caps them) — the pair in ONE process
//   --no-sleep     the mt arm with Bullet's workers NOT sent to sleep after the step (Bullet's 100 us spin runs on)
//   --mirror       the physics frame the editor runs: the document in PLAY, a SceneMirror syncing the moved bodies to
//                  an engine scene after each step (the transform-only fast path, decision 22.1) — the sync timed too
//   --overlap      THE CORE BUDGET'S QUESTION: an engine on its own render thread draws 10,000 cubes moving every frame
//                  while this thread steps; the step's time inside the render thread's pool spans is the overlap
//   --bound S      THE GUARD (physics.mt_determinism): through checkpoint S, the seq-vs-mt and run-to-run differences
//                  held to the derived bound — bit-equal before the first contact, then at most the landing's
//                  sqrt(2gh) dt once and g dt^2 per step (see the law below)
//   --mt-only      the mt arm alone (a thread count's own process: the sweep)
//   --no-island-batching  every arm with Bullet's in-island batched solve OFF (the comparison arm; the shipped world
//                  batches: Environment::setIslandBatching); the mt-nobatch arm is then not run (it would be the mt arm)
//   --penetrating  the stress start: every layer interpenetrating (the pile explodes apart on its first steps)
#include <QGuiApplication>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>

#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../support/testmesh.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/core/threadbudget.h"
#include "irisgl/mirror/scenemirror.h"
#include "btBulletDynamicsCommon.h"
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"

static int gFailures = 0;
#define CHECK(cond, ...) do { char m_[512]; std::snprintf(m_, sizeof m_, __VA_ARGS__); \
    if (cond) std::printf("ok: %s\n", m_); else { std::printf("FAIL: %s\n", m_); ++gFailures; } } while (0)

namespace {
constexpr float kDt = 1.0f / 60.0f;
using Clock = std::chrono::steady_clock;
namespace je = jahshaka::engine;

iris::MeshPtr gCube;
bool gPenetrating = false;   ///< --penetrating: the stress start (0.8 m boxes 0.5 m apart: every layer interpenetrating)
int gWarmup = 0;             ///< --warmup: the frames the medians skip

iris::SceneNodePtr box(const QString &guid, const iris::Vec3 &pos, float mass, const iris::Vec3 &scale)
{
    auto n = iris::MeshNode::create();
    n->setGUID(guid);
    if (gCube) { n->setMesh(gCube); n->setMaterial(iris::PbrMaterial::create()); }
    n->setLocalPos(pos);
    n->setLocalScale(scale);
    n->isPhysicsBody = true;
    n->physicsProperty.objectMass = mass;
    n->physicsProperty.shape = iris::PhysicsCollisionShape::Cube;
    n->physicsProperty.type = mass > 0.0f ? iris::PhysicsType::RigidBody : iris::PhysicsType::Static;
    return n;
}

struct Pile {
    iris::ScenePtr scene;
    std::vector<iris::SceneNodePtr> bodies;
};

/// N boxes of 0.4 m (Bullet's Cube shape is a half-extent-1 box scaled by the node's scale: 0.2) in four layers of
/// side x side columns at a 0.9 m pitch, each layer 5 cm above the one below it and 0.1 m along x and z (the stacks
/// lean, some topple into their neighbours: the pile stays alive), nothing touching at the start, dropped onto a
/// 200 m ground (its top at y = 0).
Pile buildPile(int n)
{
    Pile p;
    p.scene = iris::Scene::create();
    auto root = p.scene->getRootNode();
    root->addChild(box("ground", iris::Vec3(0, -0.5f, 0), 0.0f, iris::Vec3(100, 0.5f, 100)));
    const int layers = 4;
    const int side = int(std::ceil(std::sqrt(double(n) / layers)));
    int made = 0;
    for (int l = 0; l < layers && made < n; ++l)
        for (int i = 0; i < side && made < n; ++i)
            for (int j = 0; j < side && made < n; ++j, ++made) {
                const float off = 0.1f * float(l);
                const float pitchY = gPenetrating ? 0.5f : 0.45f, half = gPenetrating ? 0.4f : 0.2f;
                const iris::Vec3 at(float(i - side / 2) * 0.9f + off, 0.25f + pitchY * float(l), float(j - side / 2) * 0.9f + off);
                auto b = box(QStringLiteral("b%1").arg(made), at, 1.0f, iris::Vec3(half, half, half));
                root->addChild(b);
                p.bodies.push_back(b);
            }
    return p;
}

double cpuMs()
{
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return (double(u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3) + double(u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3;
}

double median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

/// The frames the medians read: past the warm-up (all of them when the run is shorter).
std::vector<double> measured(const std::vector<double> &v)
{
    if (gWarmup <= 0 || size_t(gWarmup) >= v.size()) return v;
    return std::vector<double>(v.begin() + gWarmup, v.end());
}

struct Run {
    std::string arm;
    double stepMs = 0, worstMs = 0, cpuPerFrameMs = 0, syncMs = 0, stepSyncMs = 0;
    int activeMedian = 0, servedMin = -1, nonFinite = 0;
    std::vector<btTransform> poses;
    std::vector<std::pair<int, std::vector<btTransform>>> checkpoints;   ///< the poses after frames 1, 2, 4, 8, ...
    std::vector<std::pair<unsigned long long, unsigned long long>> stepSpans;
};

/// The engine side of `--mirror` / `--overlap`: one engine, an offscreen view.
struct Rig {
    std::unique_ptr<je::Engine> engine;
    je::View *view = nullptr;
};

Run runPile(const std::string &arm, int bodies, int frames, Rig *mirrorRig, const std::function<void()> &perFrame = {})
{
    Run r;
    r.arm = arm;
    iris::Environment::setSequentialSteps(arm == "seq");
    Pile pile = buildPile(bodies);
    auto env = pile.scene->getPhysicsEnvironment();
    std::unique_ptr<SceneMirror> mirror;
    je::Scene *target = nullptr;
    if (mirrorRig) {
        target = mirrorRig->engine->createScene("physics", 8u);
        mirrorRig->view->setScene(target);
        pile.scene->getRootNode()->applyStaticDefaults();
        mirror.reset(new SceneMirror(target));
        mirror->setVerifierBudget(0);
        mirror->setSource(pile.scene);
        for (int i = 0; i < SceneMirror::kStaticSettleFrames * 6; ++i) mirror->sync();
        pile.scene->setPlaying(true);
    }
    env->initializePhysicsWorldFromScene(pile.scene->getRootNode());
    env->simulatePhysics();
    std::vector<double> ms, sync;
    std::vector<int> active;
    const double cpu0 = cpuMs();
    for (int f = 0; f < frames; ++f) {
        const auto t0 = Clock::now();
        pile.scene->advance(kDt);
        const auto t1 = Clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        r.stepSpans.push_back({ (unsigned long long)std::chrono::duration_cast<std::chrono::nanoseconds>(t0.time_since_epoch()).count(),
                                (unsigned long long)std::chrono::duration_cast<std::chrono::nanoseconds>(t1.time_since_epoch()).count() });
        int a = 0, nonFinite = 0;
        for (auto it = env->hashBodies.cbegin(); it != env->hashBodies.cend(); ++it) {
            if (it.value()->getInvMass() > 0.0f && it.value()->isActive()) ++a;
            const btVector3 o = it.value()->getWorldTransform().getOrigin();
            if (!std::isfinite(o.x()) || !std::isfinite(o.y()) || !std::isfinite(o.z())) ++nonFinite;
        }
        active.push_back(a);
        if (nonFinite && !r.nonFinite) { std::printf("PHYSICS_NONFINITE frame %d: %d bodies off the reals\n", f, nonFinite); std::fflush(stdout); }
        r.nonFinite = std::max(r.nonFinite, nonFinite);
        if (mirror) {
            const auto s0 = Clock::now();
            mirror->sync();
            sync.push_back(std::chrono::duration<double, std::milli>(Clock::now() - s0).count());
            // From the third play frame on, every body the step moved is movable and served by the fast path.
            if (f >= 3) {
                const int served = mirror->fastPathServedCount();
                r.servedMin = r.servedMin < 0 ? served : std::min(r.servedMin, served);
            }
        }
        if (perFrame) perFrame();
        if (((f + 1) & f) == 0 || f + 1 == frames) {   // a power of two, and the last
            std::vector<btTransform> at;
            for (const auto &n : pile.bodies)
                if (btRigidBody *body = env->hashBodies.value(n->getGUID(), nullptr)) at.push_back(body->getWorldTransform());
            r.checkpoints.push_back({ f + 1, std::move(at) });
        }
    }
    r.cpuPerFrameMs = (cpuMs() - cpu0) / double(frames);
    CHECK(r.nonFinite == 0, "[%s] every body stays on the reals through %d frames (%d off at the worst frame)",
          arm.c_str(), frames, r.nonFinite);
    r.stepMs = median(measured(ms));
    r.worstMs = *std::max_element(ms.begin(), ms.end());
    r.syncMs = median(measured(sync));
    std::vector<double> both(ms);
    for (size_t i = 0; i < both.size() && i < sync.size(); ++i) both[i] += sync[i];
    r.stepSyncMs = median(measured(both));
    std::sort(active.begin(), active.end());
    r.activeMedian = active[active.size() / 2];
    for (const auto &n : pile.bodies)
        if (btRigidBody *body = env->hashBodies.value(n->getGUID(), nullptr)) r.poses.push_back(body->getWorldTransform());
    if (mirror) {
        CHECK(mirror->verifyAgainstFullWalk() == 0, "[%s] in play the full walk after the physics frames pushes nothing",
              arm.c_str());
        pile.scene->setPlaying(false);
        mirror.reset();
        mirrorRig->view->setScene(nullptr);
        mirrorRig->engine->destroyScene(target);
    }
    return r;
}

struct Delta { double maxPos = 0, meanPos = 0, maxAngleDeg = 0; int over1cm = 0; };

Delta compare(const std::vector<btTransform> &a, const std::vector<btTransform> &b)
{
    Delta d;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const double p = (a[i].getOrigin() - b[i].getOrigin()).length();
        const double dot = std::min(1.0, std::fabs(double(a[i].getRotation().dot(b[i].getRotation()))));
        d.maxPos = std::max(d.maxPos, p);
        d.meanPos += p;
        d.maxAngleDeg = std::max(d.maxAngleDeg, 2.0 * std::acos(dot) * 180.0 / M_PI);
        if (p > 0.01) ++d.over1cm;
    }
    if (n) d.meanPos /= double(n);
    return d;
}
Delta compare(const Run &a, const Run &b) { return compare(a.poses, b.poses); }

void printRun(const Run &r, int bodies, int frames)
{
    std::printf("PHYSICS_STEP arm=%s bodies=%d frames=%d threads=%d step_ms=%.3f (worst %.3f) share_of_60hz=%.3f "
                "cpu_per_frame_ms=%.3f active_median=%d", r.arm.c_str(), bodies, frames,
                r.arm == "seq" ? 1 : iris::Environment::taskThreads(), r.stepMs, r.worstMs, r.stepMs / 16.667,
                r.cpuPerFrameMs, r.activeMedian);
    if (r.servedMin >= 0) std::printf(" mirror_sync_ms=%.3f fast_path_served_min=%d", r.syncMs, r.servedMin);
    std::printf("\n");
}
}   // namespace

int main(int argc, char **argv)
{
    int bodies = 1000, frames = 120, threads = 1, runs = 1;
    double bar = -1.0;
    const char *besideImport = nullptr;
    bool overlap = false, mirror = false, noSleep = false, mtOnly = false, islandBatching = true;
    int boundSteps = -1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--bodies") && i + 1 < argc) bodies = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) {
            ++i;
            threads = !std::strcmp(argv[i], "budget") ? iris::threadBudget().bulletThreads() : std::atoi(argv[i]);
        }
        else if (!std::strcmp(argv[i], "--warmup") && i + 1 < argc) gWarmup = std::max(0, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--bar") && i + 1 < argc) bar = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--beside-import") && i + 1 < argc) besideImport = argv[++i];
        else if (!std::strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--bound") && i + 1 < argc) boundSteps = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--overlap")) overlap = true;
        else if (!std::strcmp(argv[i], "--mirror")) mirror = true;
        else if (!std::strcmp(argv[i], "--no-sleep")) noSleep = true;
        else if (!std::strcmp(argv[i], "--mt-only")) mtOnly = true;
        else if (!std::strcmp(argv[i], "--no-island-batching")) islandBatching = false;
        else if (!std::strcmp(argv[i], "--penetrating")) gPenetrating = true;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    iris::Environment::bindGameThread(threads);   // THIS thread is the game thread: Bullet's index 0
    iris::Environment::setSleepAfterStep(!noSleep);
    if (!islandBatching) iris::Environment::setIslandBatching(false);   // the comparison arm (the shipped world batches)
    std::printf("== physics: %d bodies, %d frames, the %s world, Bullet's scheduler bound at %d threads, workers %s "
                "after each step, in-island batching %s\n", bodies, frames,
                iris::Environment::multiThreadedWorld() ? "MULTI-threaded (btDiscreteDynamicsWorldMt)" : "single-threaded",
                iris::Environment::taskThreads(), noSleep ? "left spinning" : "sent to sleep", islandBatching ? "ON" : "OFF");
    CHECK(iris::Environment::multiThreadedWorld(), "the build's world is Bullet's multi-threaded one");
    CHECK(iris::Environment::taskThreads() == std::max(1, threads), "the bound scheduler runs %d threads (%d asked)",
          iris::Environment::taskThreads(), threads);

    Rig rig;
    if (overlap || mirror) {
        std::string err;
        je::EngineConfig cfg = enginetest::testEngineConfig("test-physics-step-ogre.log");
        cfg.renderThread = overlap;
        rig.engine = je::Engine::create(cfg, err);
        if (!rig.engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
        rig.engine->submitFrame(je::FrameInput::delta(kDt));
        rig.view = rig.engine->createOffscreenView("physics", 256, 256, je::Colour(0, 0, 0, 1));
        if (!rig.view) { std::printf("FAIL: view\n"); return 1; }
        if (mirror) gCube = testmesh::load(":assets/models/cube.obj");
    }

    if (overlap) {
        // THE CORE BUDGET'S QUESTION: does the step overlap the frame's pool work in time? An engine on its own render
        // thread (the owner's free-running mode) draws 10,000 cubes moving every frame, while this thread steps.
        je::Engine *e = rig.engine.get();
        je::Scene *scene = e->createScene("overlap", 8u);
        rig.view->setScene(scene);
        enginetest::addDirectionalLight(scene, je::Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
        enginetest::testCameraLookAt(rig.view, je::Vec3(0, 40, 90), je::Vec3(0, 0, 0));
        const je::MeshId mesh = scene->createMesh(enginetest::unitCubeMesh());
        const je::MaterialId mat = scene->createPbrMaterial(je::PbrParams());
        std::vector<je::NodeId> cubes;
        for (int i = 0; i < 10000; ++i) {
            const je::NodeId n = scene->createDocumentNode(je::reserveNodeId());
            scene->attachMesh(n, mesh, mat);
            cubes.push_back(n);
        }
        unsigned long long step = 0;
        const auto submit = [&]() {
            je::FrameInput in;
            in.frameDelta = kDt;
            in.stepSerial = ++step;
            in.stepPeriod = kDt;
            je::FrameInputScene part;
            part.scene = scene;
            for (int i = 0; i < 10000; ++i) {
                je::NodeRecord r;
                r.id = cubes[size_t(i)];
                r.fields = je::NodeRecord::Trs;
                r.position = je::Vec3(float(i % 100) * 1.2f - 60.0f, 0.05f * float(step % 2), float(i / 100) * 1.2f - 60.0f);
                r.rotation = je::Quat(0, 0, 0, 1);
                r.scale = je::Vec3(0.5f, 0.5f, 0.5f);
                part.nodes.push_back(r);
                part.changes.moved.push_back(r.id);
            }
            in.scenes.push_back(part);
            e->submitFrame(in);
        };
        e->setDisplayPeriod(1.0f / 60.0f);   // the owner's 60 Hz display (an offscreen view has no present to pace it)
        e->setFreeRunning(true);   // the render thread draws on its own, every display frame the newest record
        // ITS FIRST FRAMES ARE THE SLOW ONES (the 10,000 items' first compose, the pipelines): the measured steps start
        // once the render thread is drawing at its pace — 120 of its frames published.
        for (int f = 0; f < 30; ++f) submit();
        for (int guard = 0; guard < 6000 && e->published().frame < 120; ++guard)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const Run run = runPile("mt", bodies, frames, nullptr, submit);
        e->setFreeRunning(false);
        std::vector<std::pair<unsigned long long, unsigned long long>> spans;
        if (const auto *echo = je::inspect::engine(e)) echo->poolSpans(spans);
        double stepNs = 0.0, overlapNs = 0.0, frameNs = 0.0;
        int stepsOverlapped = 0;
        for (const auto &st : run.stepSpans) {
            stepNs += double(st.second - st.first);
            bool any = false;
            for (const auto &sp : spans) {
                const unsigned long long a = std::max(st.first, sp.first), b = std::min(st.second, sp.second);
                if (b > a) { overlapNs += double(b - a); any = true; }
            }
            stepsOverlapped += any ? 1 : 0;
        }
        for (const auto &sp : spans) frameNs += double(sp.second - sp.first);
        if (!spans.empty() && !run.stepSpans.empty())
            std::printf("PHYSICS_OVERLAP_WINDOWS steps %.3f..%.3f s, pool spans %.3f..%.3f s (steady clock)\n",
                        run.stepSpans.front().first / 1e9, run.stepSpans.back().second / 1e9, spans.front().first / 1e9,
                        spans.back().second / 1e9);
        printRun(run, bodies, frames);
        std::printf("PHYSICS_OVERLAP bodies=%d frames=%d threads=%d step_ms=%.3f: %.1f %% of the step's time ran inside the "
                    "render thread's pool spans; %d of %d steps overlapped one (%zu spans, %.3f ms each on average)\n",
                    bodies, frames, iris::Environment::taskThreads(), run.stepMs,
                    stepNs > 0.0 ? 100.0 * overlapNs / stepNs : 0.0, stepsOverlapped, frames, spans.size(),
                    spans.empty() ? 0.0 : frameNs / double(spans.size()) / 1e6);
        CHECK(!spans.empty(), "the engine recorded its pool spans");
        rig.view->setScene(nullptr);
        e->destroyScene(scene);
        e->destroyView(rig.view);
        return gFailures ? 1 : 0;
    }

    Rig *mr = mirror ? &rig : nullptr;
    if (besideImport) {
        // THE PREMISE (THREADING-1 stage 1): does an import beside a playing scene cost the step a frame? The pair in
        // ONE process: the mt arm idle, then the same arm while the import runs on its own two pools — Qt's global
        // pool (the parse and the bake's caller) capped as the app caps it, and the bake pool's nice-19 workers.
        const iris::ThreadBudget &budget = iris::threadBudget();
        QThreadPool::globalInstance()->setMaxThreadCount(budget.background);   // processbudget::apply's cap
        const Run idle = runPile("mt", bodies, frames, mr);
        printRun(idle, bodies, frames);
        std::atomic<bool> stop{ false }, started{ false };
        std::atomic<int> bakes{ 0 }, valid{ 0 };
        const QString file = QString::fromLocal8Bit(besideImport);
        const QString fp = iris::MeshBake::fingerprintFor(QStringLiteral("deadbeef").repeated(8));
        QFuture<void> import = QtConcurrent::run([&]() {
            QTemporaryDir scratch;
            while (!stop.load()) {
                started.store(true);
                const iris::MeshBake::Model m = iris::MeshBake::buildFromFile(file, fp, scratch.path());
                bakes.fetch_add(1);
                if (m.valid) valid.fetch_add(1);
            }
        });
        while (!started.load()) std::this_thread::yield();
        const int bakesBefore = bakes.load();
        const Run beside = runPile("mt", bodies, frames, mr);
        const int bakesDuring = bakes.load() - bakesBefore;
        stop.store(true);
        import.waitForFinished();
        printRun(beside, bodies, frames);
        std::printf("PHYSICS_BESIDE_IMPORT bodies=%d frames=%d warmup=%d threads=%d qt_pool=%d bake_pool=%d: step %.3f -> "
                    "%.3f ms, sync %.3f -> %.3f ms, step+sync %.3f -> %.3f ms (x%.2f) idle -> beside %d bake(s) of %s "
                    "finishing during the arm (%d of %d valid)\n",
                    bodies, frames, gWarmup, iris::Environment::taskThreads(), QThreadPool::globalInstance()->maxThreadCount(),
                    iris::MeshBake::bakePoolThreads(), idle.stepMs, beside.stepMs, idle.syncMs, beside.syncMs,
                    idle.stepSyncMs, beside.stepSyncMs, idle.stepSyncMs > 0 ? beside.stepSyncMs / idle.stepSyncMs : 0.0,
                    bakesDuring, qUtf8Printable(QFileInfo(file).fileName()), valid.load(), bakes.load());
        CHECK(valid.load() > 0 && valid.load() == bakes.load(), "the import beside the arm baked (%d of %d valid)",
              valid.load(), bakes.load());
        if (rig.view) rig.engine->destroyView(rig.view);
        return gFailures ? 1 : 0;
    }
    if (mtOnly) {   // a thread count's own process (the sweep): the mt arm alone
        const Run run = runPile("mt", bodies, frames, mr);
        printRun(run, bodies, frames);
        if (bar > 0.0) {
            // THE MANY MOVERS (THREADING-1 stage 2): the owner's real case — many movers per frame — on the threads the
            // ONE core budget gives the step. The step and the sync are printed apart; the bar is on their sum, per
            // frame, the median past the warm-up.
            std::printf("PHYSICS_MANY_MOVERS bodies=%d frames=%d warmup=%d threads=%d: step %.3f ms + mirror sync %.3f ms; "
                        "step+sync median %.3f ms against the bar %.3f ms (a 60 Hz frame)\n", bodies, frames, gWarmup,
                        iris::Environment::taskThreads(), run.stepMs, run.syncMs, run.stepSyncMs, bar);
            CHECK(mirror, "the bar reads the mirror's sync (--mirror)");
            CHECK(run.stepSyncMs <= bar, "%d bodies: the step + the mirror's sync, median %.3f ms <= %.3f ms on %d thread(s)",
                  bodies, run.stepSyncMs, bar, iris::Environment::taskThreads());
            CHECK(run.servedMin == bodies, "in play the fast path serves every moved body (%d of %d)", run.servedMin, bodies);
        }
        if (rig.view) rig.engine->destroyView(rig.view);
        std::printf(gFailures ? "physics.step_cost: FAILED (%d)\n" : "physics.step_cost: PASS\n", gFailures);
        return gFailures ? 1 : 0;
    }
    const Run seq = runPile("seq", bodies, frames, mr);
    printRun(seq, bodies, frames);
    std::vector<Run> mts;
    for (int k = 0; k < runs; ++k) {
        mts.push_back(runPile("mt", bodies, frames, mr));
        printRun(mts.back(), bodies, frames);
    }
    if (mirror) {
        CHECK(seq.servedMin == bodies && mts[0].servedMin == bodies, "in play the fast path serves every moved body "
              "(seq %d, mt %d of %d)", seq.servedMin, mts[0].servedMin, bodies);
    }
    std::printf("PHYSICS_GAIN bodies=%d threads=%d: step %.3f -> %.3f ms (x%.2f), share of a 60 Hz frame %.3f -> %.3f\n",
                bodies, iris::Environment::taskThreads(), seq.stepMs, mts[0].stepMs,
                mts[0].stepMs > 0 ? seq.stepMs / mts[0].stepMs : 0.0, seq.stepMs / 16.667, mts[0].stepMs / 16.667);
    // THE PAIR (BULLET-BATCHING-1): the shipped world's in-island batching against the same mt world without it, in
    // this process (Bullet reads the threshold at each solve; the arm sets it back after).
    if (islandBatching) {
        iris::Environment::setIslandBatching(false);
        const Run off = runPile("mt-nobatch", bodies, frames, mr);
        iris::Environment::setIslandBatching(true);
        printRun(off, bodies, frames);
        if (mirror)
            CHECK(off.servedMin == bodies, "in play the fast path serves every moved body (mt-nobatch %d of %d)",
                  off.servedMin, bodies);
        std::printf("PHYSICS_BATCHING bodies=%d threads=%d: in-island batching off -> on %.3f -> %.3f ms (x%.2f)\n",
                    bodies, iris::Environment::taskThreads(), off.stepMs, mts[0].stepMs,
                    mts[0].stepMs > 0 ? off.stepMs / mts[0].stepMs : 0.0);
    }
    // THE DETERMINISM, BOUNDED (decision 23.3).
    double worstSeq = 0.0, worstRun = 0.0;
    for (size_t k = 0; k < mts.size(); ++k) {
        const Delta d = compare(seq, mts[k]);
        worstSeq = std::max(worstSeq, d.maxPos);
        std::printf("PHYSICS_DETERMINISM seq vs mt run %zu after %d steps: position max %.6g m mean %.6g m, rotation max "
                    "%.4g deg, %d of %zu bodies over 1 cm\n", k, frames, d.maxPos, d.meanPos, d.maxAngleDeg, d.over1cm,
                    seq.poses.size());
        for (size_t j = k + 1; j < mts.size(); ++j) {
            const Delta s = compare(mts[k], mts[j]);
            worstRun = std::max(worstRun, s.maxPos);
            std::printf("PHYSICS_SPREAD mt run %zu vs run %zu: position max %.6g m mean %.6g m, rotation max %.4g deg, "
                        "%d bodies over 1 cm\n", k, j, s.maxPos, s.meanPos, s.maxAngleDeg, s.over1cm);
        }
    }
    // ...AND HOW IT GROWS: the seq-vs-mt difference at each checkpoint of the first mt run (the step's own contact
    // order is the source; a toppling pile amplifies it).
    for (size_t c = 0; c < seq.checkpoints.size() && c < mts[0].checkpoints.size(); ++c) {
        const Delta d = compare(seq.checkpoints[c].second, mts[0].checkpoints[c].second);
        std::printf("PHYSICS_GROWTH after %d steps: position max %.6g m mean %.6g m, rotation max %.4g deg, %d over 1 cm\n",
                    seq.checkpoints[c].first, d.maxPos, d.meanPos, d.maxAngleDeg, d.over1cm);
    }
    // THE BOUND (decision 23.3), DERIVED: the order a step solves its contacts in is the only thing the threads change
    // (the manifolds' merge order, the islands' unstable sort). (1) Before the first contact nothing is solved — every
    // body falls on gravity alone — so the arms are BIT-EQUAL (the pile's 5 cm gaps: the first contacts come near step
    // 5 at 15 m/s^2). (2) After it, a step's sequential-impulse solve (its 10 iterations end unconverged, and where
    // they end depends on the order) can route only the momentum that step's contacts carry: once, the landing — a
    // body that fell the 5 cm gap meets its contact at sqrt(2 g h) — and every step after, the resting load g dt. So
    // a body's velocity can differ by at most sqrt(2 g h) + S g dt by step S, its position by at most
    // sqrt(2 g h) dt + S g dt^2 — linear growth. (3) A pile that TOPPLES amplifies it past any such bound (a stack
    // that falls the other way is metres off): the law is held through the settling (checkpoint S of the guard) and
    // the growth after it only printed.
    if (boundSteps > 0) {
        const double g = 15.0, dt = 1.0 / 60.0, gdt2 = g * dt * dt;   // Environment's gravity (15 m/s^2), the grid
        const double landing = std::sqrt(2.0 * g * 0.05) * dt;          // the 5 cm gap's landing, once
        const double limit = landing + double(boundSteps) * gdt2;
        for (size_t c = 0; c < seq.checkpoints.size(); ++c) {
            const int at = seq.checkpoints[c].first;
            if (at > boundSteps) break;
            double worst = 0.0;
            for (const Run &m : mts)
                if (c < m.checkpoints.size()) worst = std::max(worst, compare(seq.checkpoints[c].second, m.checkpoints[c].second).maxPos);
            for (size_t a = 0; a < mts.size(); ++a)
                for (size_t b = a + 1; b < mts.size(); ++b)
                    if (c < mts[a].checkpoints.size() && c < mts[b].checkpoints.size())
                        worst = std::max(worst, compare(mts[a].checkpoints[c].second, mts[b].checkpoints[c].second).maxPos);
            if (at <= 2)
                CHECK(worst == 0.0, "after %d steps (free fall, no contact yet) every arm is bit-equal (%.6g m)", at, worst);
            else
                CHECK(worst <= landing + double(at) * gdt2, "after %d steps the arms differ by at most sqrt(2gh) dt + %d g "
                      "dt^2 = %.4g m (%.6g m: seq vs mt and run to run)", at, at, landing + double(at) * gdt2, worst);
        }
        std::printf("PHYSICS_BOUND through %d steps: %.4g m (the landing %.4g m once, g dt^2 = %.4g m a step)\n",
                    boundSteps, limit, landing, gdt2);
    }
    if (rig.view) rig.engine->destroyView(rig.view);
    std::printf(gFailures ? "physics.step_cost: FAILED (%d)\n" : "physics.step_cost: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
