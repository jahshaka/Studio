// engine.build_burst (SHADER-SYSTEM-1 stage 7; the notice's engine fact; CPU, no engine) - THE BUILDS' BURST
// (irisgl engine/src/BuildBurst.h says why). The laws, fed as the engine's publish feeds it (pending, landings so far):
//   (1) idle reads 0 of 0;
//   (2) a burst BEGINS when pending rises from 0: 0 of pending, whatever landed before it;
//   (3) inside it, a landing moves `landed` and a new request raises `total` (n of m, m = landed + pending);
//   (4) the publish that ENDS it (pending back to 0) counts its last landings: m of m, and keeps reading so;
//   (5) THE NEXT burst counts from ITS OWN base: 0 of pending again, never the old burst's landings (RED with the base
//       not moved at a burst's start);
//   (6) work that began and ended between two publishes is no burst: the next one does not count it.
#include "BuildBurst.h"

#include <cstdio>

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } std::fflush(stdout); } while (0)

using jahshaka::engine::BuildBurst;

int main()
{
    BuildBurst b;
    b.feed(0u, 0ull);
    CHECK(b.landed() == 0u && b.total() == 0u, "(1) idle: %u of %u", b.landed(), b.total());
    b.feed(0u, 7ull);   // seven landed in work no publish saw pending
    b.feed(5u, 7ull);
    CHECK(b.landed() == 0u && b.total() == 5u, "(2)+(6) a burst begins: %u of %u (the 7 before it not counted)",
          b.landed(), b.total());
    b.feed(4u, 9ull);   // two landed, one more requested
    CHECK(b.landed() == 2u && b.total() == 6u, "(3) two landed, one requested: %u of %u", b.landed(), b.total());
    b.feed(0u, 13ull);  // the last four landed
    CHECK(b.landed() == 6u && b.total() == 6u, "(4) the burst ends: %u of %u", b.landed(), b.total());
    b.feed(0u, 13ull);
    CHECK(b.landed() == 6u && b.total() == 6u, "(4) ...and keeps reading its end: %u of %u", b.landed(), b.total());
    b.feed(0u, 15ull);  // two began and ended between publishes
    b.feed(3u, 15ull);
    CHECK(b.landed() == 0u && b.total() == 3u, "(5)+(6) the next burst counts from its own base: %u of %u", b.landed(),
          b.total());
    b.feed(1u, 17ull);
    CHECK(b.landed() == 2u && b.total() == 3u, "(3) ...two of its three landed: %u of %u", b.landed(), b.total());
    std::printf("\nengine.build_burst: %s\n", gFailures ? "FAILED" : "PASSED");
    return gFailures ? 1 : 0;
}
