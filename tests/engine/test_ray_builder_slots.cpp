// engine.ray_builder_slots (COLD-BOOT-1 stage 4; CPU, no engine) - THE RAY PIPELINE BUILDERS' SLOTS
// (irisgl engine/src/RayBuilderSlots.h says why). The laws:
//   (1) the width bounds the builders at once: a width of 2 gives two takes, a third waits until a give;
//   (2) THE PAIR NEVER DEADLOCKS: a builder holding a slot asks for its second builder through tryTake, which never waits -
//       on a tier one wide it is refused at once (the pair builds serially), on a wider tier it is granted;
//   (3) a width below one is one (a tier always builds).
// A wait that would hang is bounded by the row: the third take runs on its own thread and is read through a flag.
#include "RayBuilderSlots.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } std::fflush(stdout); } while (0)

using jahshaka::engine::RayBuilderSlots;

int main()
{
    {
        RayBuilderSlots s;
        s.reset(1);
        s.take();   // the reflection's builder
        CHECK(!s.tryTake(), "(2) a tier ONE wide: the pair's second builder is refused at once (serial, no deadlock)");
        s.give();
        CHECK(s.freeNow() == 1, "(2) ...and its slot comes back: %d free", s.freeNow());
    }
    {
        RayBuilderSlots s;
        s.reset(2);
        s.take();
        CHECK(s.tryTake(), "(2) a tier two wide: the pair's second builder is granted");
        CHECK(s.freeNow() == 0, "(1) two builders at once on a width of 2: %d free", s.freeNow());
        std::atomic<bool> third{ false };
        std::thread t([&] { s.take(); third.store(true); });
        // A SLEEP, NOT A POLL LOOP (CODING_RULES 6): the margin the third builder would need to slip through is held here.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(!third.load(), "(1) a third builder waits while the width is held");
        s.give();
        t.join();   // it takes the slot given back
        CHECK(third.load(), "(1) ...and takes the slot a give frees");
        s.give();
        s.give();
        CHECK(s.freeNow() == 2, "(1) every slot back: %d free", s.freeNow());
    }
    {
        RayBuilderSlots s;
        s.reset(0);
        CHECK(s.freeNow() == 1, "(3) a width below one is one: %d", s.freeNow());
    }
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
