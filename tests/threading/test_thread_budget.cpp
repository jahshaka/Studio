// threading.budget — THE ONE CORE BUDGET (THREADING-1 stage 1; irisgl/core/threadbudget.h).
//
// One function sizes every thread pool of the process. This fixture proves the function's law on every box size, the
// numbers on the box it runs on, and that the process applies them — the same call main.cpp makes
// (processbudget::apply), then what each pool actually holds:
//
//   1. THE LAW, swept over 1..256 hardware threads: Bullet's workers at least 1 and at most their cap, the background
//      at least its floor; once the box holds the floors the per-frame ACTIVE set (game + render + Ogre's pool +
//      Bullet's workers) never exceeds it; a background above its floor means the active set + the background are the
//      box exactly; Bullet's workers below their cap take everything the frame's other threads leave. The dedicated set
//      (writer, streaming, multiload) is bursty and never subtracted. The overrides (JAHSHAKA_SCENE_THREADS,
//      JAH_TEXTURE_MULTILOAD, JAHSHAKA_PHYSICS_THREADS) move their own tier and the remainder only.
//   2. THE 20-THREAD BOX in numbers (the owner's 10-core i9): 1 game + 6 Bullet workers (7, the measured best), 1 render
//      + 8 Ogre workers — the active set 16 —, background 4; the dedicated 8 (writer, streaming, multiload 6) beside.
//   3. THE PROCESS: after processbudget::apply, Bullet's bound scheduler runs bulletThreads (= the foreground share +
//      the game thread), Qt's global pool's maximum is the background width, and the bake pool — created by the first
//      ask — started exactly the background width's threads (counted in /proc/self/task), and the scene workers' primary
//      tier is the render tier's pool.
//
// No engine, no display: seconds.
#include <QCoreApplication>
#include <QDir>
#include <QThreadPool>

#include <cstdio>
#include <cstdlib>

#include "bridge/processbudget.h"
#include "bridge/sceneworkerthreads.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/core/threadbudget.h"

static int gFailures = 0;
#define CHECK(cond, ...) do { char m_[512]; std::snprintf(m_, sizeof m_, __VA_ARGS__); \
    if (cond) std::printf("ok: %s\n", m_); else { std::printf("FAIL: %s\n", m_); ++gFailures; } } while (0)

namespace je = iris;

namespace {

int taskCount()
{
    return int(QDir(QStringLiteral("/proc/self/task")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
}

/// The law for one box; returns the number of violations (printed).
int lawViolations(const je::ThreadBudget &b, const char *what)
{
    int bad = 0;
    const auto fail = [&](const char *rule) {
        if (bad++ < 3)
            std::printf("  %s hardware=%d: %s (fg %d bg %d ogre %d multiload %d active %d total %d)\n", what, b.hardware,
                        rule, b.foreground, b.background, b.ogrePool, b.multiload, b.activeSet(), b.total());
    };
    // The floors of the active set: Bullet's share at its floor of 1.
    const int floors = b.game + 1 + b.render + b.ogrePool;
    if (b.foreground < 1 || b.foreground > je::kBudgetForegroundCap) fail("Bullet's workers are in [1, cap]");
    if (b.background < je::kBudgetBackgroundFloor) fail("the background holds its floor");
    if (b.hardware >= floors && b.activeSet() > b.hardware) fail("the per-frame active set fits a box that holds its floors");
    if (b.background > je::kBudgetBackgroundFloor && b.total() != b.hardware)
        fail("a background above its floor is exactly what the active set leaves");
    if (b.foreground < je::kBudgetForegroundCap && b.foreground > 1 && b.activeSet() != b.hardware)
        fail("Bullet's workers below their cap take everything the frame's other threads leave");
    if (b.multiload != std::min(6, std::max(2, b.hardware / 2)) && b.multiload != 0)
        fail("the multiload is min(6, max(2, hardware / 2)), sized and never subtracted");
    return bad;
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 1. THE LAW, every box size, with and without the overrides.
    int bad = 0, boxes = 0;
    for (int hw = 1; hw <= 256; ++hw, ++boxes) {
        bad += lawViolations(je::threadBudgetFor(hw), "default");
        bad += lawViolations(je::threadBudgetFor(hw, 1), "JAHSHAKA_SCENE_THREADS=1");
        bad += lawViolations(je::threadBudgetFor(hw, -1, 0), "JAH_TEXTURE_MULTILOAD=0");
    }
    CHECK(bad == 0, "the law holds on every box of 1..256 threads, default and overridden (%d boxes x 3, %d violations)",
          boxes, bad);
    const je::ThreadBudget forced = je::threadBudgetFor(20, -1, -1, 7);
    CHECK(forced.bulletThreads() == 7 && forced.ogrePool == 8 && forced.multiload == 6,
          "JAHSHAKA_PHYSICS_THREADS=7 sets Bullet's threads (the game thread included) and nothing else (%d, ogre %d, "
          "multiload %d)", forced.bulletThreads(), forced.ogrePool, forced.multiload);
    const je::ThreadBudget big = je::threadBudgetFor(64);
    CHECK(big.foreground == je::kBudgetForegroundCap && big.background == 64 - big.activeSet(),
          "a 64-thread box: Bullet at its cap of %d workers, the rest (%d) is the background's", big.foreground,
          big.background);

    // 2. THE 20-THREAD BOX.
    const je::ThreadBudget box20 = je::threadBudgetFor(20);
    CHECK(box20.game == 1 && box20.foreground == 6 && box20.bulletThreads() == 7 && box20.render == 1 &&
          box20.ogrePool == 8 && box20.writer == 1 && box20.streaming == 1 && box20.multiload == 6 &&
          box20.background == 4 && box20.activeSet() == 16 && box20.total() == 20,
          "the 20-thread box: game 1 + Bullet %d (%d threads), render 1 + Ogre %d — active %d —, background %d (active + "
          "background %d of 20); dedicated %d beside (multiload %d)", box20.foreground, box20.bulletThreads(),
          box20.ogrePool, box20.activeSet(), box20.background, box20.total(), box20.dedicated(), box20.multiload);

    // 3. THIS PROCESS.
    const je::ThreadBudget &b = je::threadBudget();
    std::printf("THREAD_BUDGET hardware=%d game=%d foreground=%d bullet=%d render=%d ogre_pool=%d writer=%d streaming=%d "
                "multiload=%d dedicated=%d background=%d active_set=%d total=%d\n", b.hardware, b.game, b.foreground,
                b.bulletThreads(), b.render, b.ogrePool, b.writer, b.streaming, b.multiload, b.dedicated(), b.background,
                b.activeSet(), b.total());
    CHECK(lawViolations(b, "this box") == 0, "the law holds on this box (%d threads)", b.hardware);
    processbudget::apply();
    CHECK(iris::Environment::taskThreads() == b.foreground + 1,
          "Bullet's bound scheduler runs the foreground share + the game thread (%d = %d + 1)",
          iris::Environment::taskThreads(), b.foreground);
    CHECK(QThreadPool::globalInstance()->maxThreadCount() == b.background,
          "Qt's global pool is capped at the background width (%d = %d)", QThreadPool::globalInstance()->maxThreadCount(),
          b.background);
    const int before = taskCount();
    const int poolThreads = iris::MeshBake::bakePoolThreads();
    const int started = taskCount() - before;
    CHECK(poolThreads == b.background && started == b.background,
          "the bake pool started the background width's threads, once (%d reported, %d new in /proc/self/task, %d wanted)",
          poolThreads, started, b.background);
    CHECK(iris::MeshBake::bakePoolThreads() == poolThreads && taskCount() - before == started,
          "asking again starts none (%d threads)", taskCount() - before);
    CHECK(int(sceneworkers::count(sceneworkers::Tier::Primary)) == b.ogrePool,
          "the scene workers' primary tier is the render tier's pool (%u = %d)",
          sceneworkers::count(sceneworkers::Tier::Primary), b.ogrePool);

    std::printf(gFailures ? "threading.budget: FAILED (%d)\n" : "threading.budget: PASS\n", gFailures);
    std::fflush(stdout);
    return gFailures ? 1 : 0;
}
