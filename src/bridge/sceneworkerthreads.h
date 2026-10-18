#ifndef SCENEWORKERTHREADS_H
#define SCENEWORKERTHREADS_H

// How many worker threads each engine Scene gets (fps audit F3).
//
// Every SceneManager forks its culling, render-queue building and object
// updates across ITS OWN pool and joins them on a barrier — pools are not
// shared between scenes. Studio's createScene calls used to take the engine's
// hardcoded 2 for all of them, which on the owner's box (10 cores / 20 hardware
// threads) drew the editor on two
// threads while a thumbnail scene, a material-preview scene, an asset-preview
// scene and an avatar-preview scene each held two more they never needed.
//
// So the number is a property of WHAT THE SCENE IS FOR, and there are four
// answers:
//
//   Primary     the on-screen scenes a user watches at frame rate — the editor
//               viewport and the player. Gets the machine, capped at 8.
//   Preview     small on-screen previews and snapshot scenes (asset page,
//               material dock, avatar dock, the engine preview dialog): a few
//               hundred objects at most, usually in a small widget. 2, the
//               historical default.
//   Utility     a scene with real per-frame work but too little of it to be
//               worth splitting. 1.
//   MainThread  NO POOL AT ALL (THREADING_ADOPTION_SPEC.md P5): thumbnails, and
//               the document's staging managers on the engine side. At 128x128
//               the barrier is pure overhead — and 1 does not avoid it. Ogre
//               spawns a thread at 1 and pays two barrier syncs per parallel
//               pass to do serial work; only 0 takes the mForceMainThread path
//               and runs those passes inline. This tier is that 0, spelled so
//               it cannot be confused with "engine default".
//
// THE ONE EXCEPTION, and it is worth stating here because it reads like a
// mistake otherwise: the STARTUP SHADER WARM-UP scene renders 32x32 and is
// Tier::Primary (SPECS/THREADING_ADOPTION_SPEC.md P4(a), src/app/
// shaderbuildgate.cpp). Its job is not to cull or draw, it is to COMPILE, and
// Ogre forks shader compilation across this same pool: the parallel warm-up
// path needs getNumWorkerThreads() > 1 (OgreRenderQueue.cpp:588), and
// getNumWorkerThreads() returns max(n, 1) — so 1 and 0 both compile serially.
// The tier answers "how much per-frame work is there", and for that one scene
// the answer is "none, but there are 66 shaders to build".
//
// THE CAP IS MEASURED, NOT GUESSED. More threads is not free — the barrier
// costs at every parallel pass, so at small scene sizes extra threads make the
// frame SLOWER. bench_scenegraph --threads N is the instrument; the numbers
// behind the 8 are in the perf wave's report (Debug build, a 20-thread box, scenes
// of 1k / 10k / 50k nodes: 8 threads is where the 10k and 50k idle-tick gains
// stop growing, and 1k is flat rather than worse).
//
// Debug builds are what the owner runs daily, and that is the build these
// numbers were taken in.

#include <QThread>
#include <QtGlobal>
#include <algorithm>

// For kSceneMainThreadOnly — the boundary's spelling of "no worker pool"
// (Tier::MainThread below). A POD header; it pulls in no Ogre and no Qt.
#include "jahshaka/engine/Types.h"

namespace sceneworkers {

enum class Tier {
    Primary,    ///< the editor viewport and the player: on-screen, full scenes
    Preview,    ///< preview docks and snapshot scenes
    Utility,    ///< small one-frame-into-a-texture scenes with real per-frame work
    MainThread  ///< no worker pool at all — see below
};

/// Threads for a tier, in the boundary's own spelling.
///
/// Tier::MainThread answers with `kSceneMainThreadOnly` rather than a count
/// (SPECS/THREADING_ADOPTION_SPEC.md P5). It is a MODE, not a smaller pool: the
/// backend spawns no thread and runs every parallel pass inline with no
/// barrier. Passing 1 instead — which is what the Utility tier used to give
/// thumbnails — is strictly worse: a thread is created and two barrier syncs
/// are paid per pass, to do exactly the serial work the calling thread could
/// have done. Utility survives for a scene that has real per-frame work but too
/// little of it to split.
///
/// Never plain 0: at this boundary 0 means "engine default", and the two have
/// to be distinguishable.
///
/// JAHSHAKA_SCENE_THREADS overrides the PRIMARY tier, and exists so that the
/// number above can be re-measured on a machine without editing this file:
/// `JAHSHAKA_SCENE_THREADS=2 ./Jahshaka --script bench.js` reproduces the
/// pre-wave behaviour exactly. A measurement escape hatch, not a preference —
/// it is deliberately not in Preferences and not persisted.
inline unsigned count(Tier tier)
{
    const int cores = std::max(1, QThread::idealThreadCount());
    switch (tier) {
    case Tier::Primary: {
        bool ok = false;
        const int forced = qEnvironmentVariableIntValue("JAHSHAKA_SCENE_THREADS", &ok);
        if (ok && forced > 0) return unsigned(std::clamp(forced, 1, 32));
        return unsigned(std::clamp(cores, 2, 8));
    }
    case Tier::Preview:    return 2u;
    case Tier::Utility:    return 1u;
    case Tier::MainThread: return jahshaka::engine::kSceneMainThreadOnly;
    }
    return 2u;
}

/// THE PHYSICS STEP'S THREADS (ENGINE-UPGRADE-U4 stage 6, decision 23): Bullet's own task scheduler, sized from the
/// ONE core budget, the game thread included (it runs jobs during the step). The box's hardware threads, minus the
/// threads that run beside the step and are not Bullet's: the game thread's own place is Bullet's thread 0, so it is
/// counted in the share; the render thread; the dedicated ones (the shader-cache writer, the texture loader, Ogre's
/// streaming thread, the texture multiload); and the primary tier's pool (Ogre's SceneManager workers, whose frame
/// work runs on the render thread's timeline WHILE the game thread steps — the two overlap, so the split is fixed,
/// never shared). On the 20-thread box: 20 - 1 (render) - 4 (dedicated) - 8 (Ogre's pool) = 7 = the game thread + 6
/// workers. JAHSHAKA_PHYSICS_THREADS overrides it, the same measurement escape hatch as JAHSHAKA_SCENE_THREADS.
/// MEASURED (physics.step_overlap, Debug, 2026-10-18): a free-running engine's pool spans cover 22-35 % of the step's
/// time and every step meets one — the two do overlap, so the split is fixed. (physics.step_cost: a 10,000-body pile
/// steps in 279 ms on one thread, 218 on 7 — one toppled island solves on one thread, so past 4 threads the step
/// barely moves; 500 bodies in separate islands, 10.0 -> 6.2 ms.)
inline int physicsThreads()
{
    bool ok = false;
    const int forced = qEnvironmentVariableIntValue("JAHSHAKA_PHYSICS_THREADS", &ok);
    if (ok && forced > 0) return std::clamp(forced, 1, 32);
    constexpr int kRenderThread = 1, kDedicatedThreads = 4;
    const int hardware = std::max(1, QThread::idealThreadCount());
    return std::max(1, hardware - kRenderThread - kDedicatedThreads - int(count(Tier::Primary)));
}

}   // namespace sceneworkers

#endif   // SCENEWORKERTHREADS_H
