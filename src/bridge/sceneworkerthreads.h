#ifndef SCENEWORKERTHREADS_H
#define SCENEWORKERTHREADS_H

// How many worker threads each engine Scene gets (fps audit F3).
//
// Every SceneManager forks its culling, render-queue building and object
// updates across ITS OWN pool and joins them on a barrier — pools are not
// shared between scenes. Studio's createScene calls used to take the engine's
// hardcoded 2 for all of them, which on a 16-core box drew the editor on two
// threads while a thumbnail scene, a material-preview scene, an asset-preview
// scene and an avatar-preview scene each held two more they never needed.
//
// So the number is a property of WHAT THE SCENE IS FOR, and there are three
// answers:
//
//   Primary   the on-screen scenes a user watches at frame rate — the editor
//             viewport and the player. Gets the machine, capped at 8.
//   Preview   small on-screen previews and snapshot scenes (asset page,
//             material dock, avatar dock, the engine preview dialog): a few
//             hundred objects at most, usually in a small widget. 2, the
//             historical default.
//   Utility   scenes that exist to render one frame into a texture and be
//             emptied again (thumbnails, the startup shader warm-up). 1: the
//             barrier is pure overhead at 128x128.
//
// THE CAP IS MEASURED, NOT GUESSED. More threads is not free — the barrier
// costs at every parallel pass, so at small scene sizes extra threads make the
// frame SLOWER. bench_scenegraph --threads N is the instrument; the numbers
// behind the 8 are in the perf wave's report (Debug build, 16-core box, scenes
// of 1k / 10k / 50k nodes: 8 threads is where the 10k and 50k idle-tick gains
// stop growing, and 1k is flat rather than worse).
//
// Debug builds are what the owner runs daily, and that is the build these
// numbers were taken in.

#include <QThread>
#include <QtGlobal>
#include <algorithm>

namespace sceneworkers {

enum class Tier {
    Primary,   ///< the editor viewport and the player: on-screen, full scenes
    Preview,   ///< preview docks and snapshot scenes
    Utility    ///< one-frame-into-a-texture scenes (thumbnails, warm-up)
};

/// Threads for a tier. Never 0 (that would mean "engine default" at the
/// boundary) and never more than the machine has.
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
    case Tier::Preview: return 2u;
    case Tier::Utility: return 1u;
    }
    return 2u;
}

}   // namespace sceneworkers

#endif   // SCENEWORKERTHREADS_H
