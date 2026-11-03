// engine.moving_sample — THE MOVING SAMPLE INDEX's ONE RULE, as a pure function (TEST-STREAMLINE-1 3c, URGENT #157;
// irisgl/engine/src/MovingSample.h). No device, no Ogre: the three producers (the reflection, the gather, the area
// visibility) all read it, so its law is theirs.
// LAW: a view's moving sequence is a function of the frames since its scene's bind (or its size change) - a camera cut
// keeps it, a held frame does not advance it.
#include <cstdio>
#include "MovingSample.h"

using jahshaka::engine::detail::MovingSequence;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    const unsigned long long A = 11, B = 12;
    // 1. a scene bind restarts it
    MovingSequence s;
    bool okA = true;
    for (unsigned c = 100; c < 140; ++c) okA = okA && s.index(c, A, 640, 360) == c - 100;
    check(okA, "a view's first frames on scene A index from 0, one a frame (its counter started at 100)");
    check(s.index(140, B, 640, 360) == 0 && s.index(141, B, 640, 360) == 1, "a BIND to scene B restarts the index at 0");
    // 2. a size change restarts it
    check(s.index(150, B, 1280, 800) == 0 && s.index(151, B, 1280, 800) == 1, "a SIZE CHANGE restarts it");
    // 3. N frames on A, a bind to B, M frames = M frames on B from a fresh view (the #157 law: what came before is gone)
    MovingSequence moved, fresh;
    for (unsigned c = 0; c < 777; ++c) moved.index(c, A, 1280, 800);
    bool same = true;
    for (unsigned m = 0; m < 300; ++m) same = same && moved.index(777 + m, B, 1280, 800) == fresh.index(m, B, 1280, 800);
    check(same, "777 frames on A then a bind to B: every frame on B indexes as a fresh view's on B");
    // 4. a held frame (the producer's counter not advanced) does not advance it; a camera cut (no input) keeps it
    MovingSequence h;
    h.index(0, A, 64, 64); h.index(1, A, 64, 64);
    check(h.index(2, A, 64, 64) == 2 && h.index(2, A, 64, 64) == 2 && h.index(3, A, 64, 64) == 3,
          "a held frame (its counter unchanged) keeps the index; a camera cut has no input and keeps the age");
    // 5. a counter that went backwards (a producer that dropped its own state) restarts it
    check(h.index(0, A, 64, 64) == 0 && h.index(1, A, 64, 64) == 1, "a counter that went backwards restarts the index");
    // 6. its 16 bits, the shaders' (& 0xFFFF), and a unique id per scene (a reused address is not the same scene)
    MovingSequence w;
    w.index(5, A, 8, 8);
    check(w.index(5u + 65536u, A, 8, 8) == 0 && w.index(5u + 65537u, A, 8, 8) == 1, "the index is 16 bits (wraps at 65536)");
    MovingSequence z;
    check(z.index(0, 0, 8, 8) == 0 && z.index(9, 0, 8, 8) == 9, "no scene (id 0) is a scene like any other: its frames count");
    std::printf("engine.moving_sample: %s\n", gFail ? "FAILED" : "all ok");
    return gFail ? 1 : 0;
}
