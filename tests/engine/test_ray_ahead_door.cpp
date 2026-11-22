// engine.ray_ahead_door (SHADER-SYSTEM-1 stage 7, call 3; the lead's ruling P1; CPU, no engine) - THE RAY CONSUMER's DOOR
// (irisgl engine/src/RayAheadDoor.h says why). The laws:
//   (1) A BUILD STILL RUNNING IN A SCENE ON THE SERVICE IS NEVER JOINED, AT ANY PACE: NotThisFrame on a Streaming frame AND
//       on a Complete one (RED before stage 7: a Complete frame - the editor's, the startup gate's tails - joined a cold
//       driver compile on the render thread);
//   (2) a SYNCHRONOUS scene (a script, MCP, the selftest, a fixture) joins it, on purpose, at either pace (the control:
//       the door does join where the law says it must);
//   (3) a FINISHED build is joined at once in any scene (no wait), unless the test door holds it (running);
//   (4) no build pending (joined already, or never started) runs the consumer: Run;
//   (5) P3 (A ROUTE JOINS A FRAME ONLY WHEN ITS PIPELINE AND ITS LANDED SET ARE BOTH BUILT): in a scene on the service a
//       route whose landed set is not built is refused - its build finished, joined or never started (RED before P3: a
//       finished build joined and its consumer ran, the landed pass's permutations not built: the gather's black cut);
//       a synchronous scene never reads the set (its pass compiles in its frame: the control).
#include "RayAheadDoor.h"

#include <cstdio>
#include <initializer_list>

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } std::fflush(stdout); } while (0)

using namespace jahshaka::engine::rayahead;

int main()
{
    for (const bool complete : { false, true }) {
        const char *pace = complete ? "Complete" : "Streaming";
        CHECK(door(true, true, true, complete, true) == Door::NotThisFrame,
              "(1) a running build, the scene on the service, a %s frame: NOT THIS FRAME (never joined)", pace);
        CHECK(door(true, true, false, complete, true) == Door::Join,
              "(2) a running build, a synchronous scene, a %s frame: JOINED, on purpose (the control)", pace);
        CHECK(door(true, false, true, complete, true) == Door::Join && door(true, false, false, complete, true) == Door::Join,
              "(3) a finished build, a %s frame: joined at once in either scene", pace);
        CHECK(door(false, true, true, complete, true) == Door::Run && door(false, false, false, complete, true) == Door::Run,
              "(4) no build pending, a %s frame: the consumer runs", pace);
        CHECK(door(true, false, true, complete, false) == Door::NotThisFrame &&
                  door(false, false, true, complete, false) == Door::NotThisFrame &&
                  door(true, true, true, complete, false) == Door::NotThisFrame,
              "(5) the landed set not built, the scene on the service, a %s frame: NOT THIS FRAME (finished, joined or "
              "running)", pace);
        CHECK(door(true, false, false, complete, false) == Door::Join && door(false, false, false, complete, false) == Door::Run,
              "(5) the landed set not built, a synchronous scene, a %s frame: the set is not read (the control)", pace);
    }
    if (gFailures) std::printf("\nengine.ray_ahead_door: FAILED (%d)\n", gFailures);
    else std::printf("\nengine.ray_ahead_door: PASSED\n");
    return gFailures ? 1 : 0;
}
