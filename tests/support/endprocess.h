#pragma once
// THE EXIT-ONLY TEARDOWN for a fixture's end (URGENT #102, jahshaka/engine/ProcessExit.h): the engine torn down with
// its Root kept — no vkDestroyDevice, which waits for the NVIDIA driver's background compile (10-20 s cold,
// measured) — then the fixture's shader-compile check (enginetesthelpers.h, URGENT #83's law; an _Exit runs no
// atexit handler, so it runs here), then std::_Exit with the fixture's code.
// `return enginetest::endProcess(engine, rc);` as main's last line; never in a fixture that destroys and re-creates
// its engine before its end (test_engine_recreate keeps the real destroy).
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/ProcessExit.h"

#include <cstdio>
#include <cstdlib>

namespace enginetest {

template <class EnginePtr> [[noreturn]] int endProcess(EnginePtr &engine, int rc)
{
    jahshaka::engine::markProcessExit();
    engine.reset();
    const unsigned long long failed = jahshaka::engine::Engine::processShaderCompileErrors();
    if (failed != 0) {
        std::fprintf(stderr, "FAIL: %llu shader program(s) failed to compile in this fixture (its readings are of "
                             "draws that never happened) - see its Ogre log\n", failed);
        rc = 1;
    }
    std::fflush(nullptr);
    std::_Exit(rc);
}

}   // namespace enginetest
