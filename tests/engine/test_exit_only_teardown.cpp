// engine.exit_only_teardown (COLD-BOOT-1, the lane's added item "THE FIXTURES' EXIT"; CPU, a headless engine, no card).
//
// THE DEFECT (spikes/exit-race/VERIFY.md): a fixture's exit-only teardown leaves Root, the device and the driver's threads
// to the OS (jahshaka/engine/ProcessExit.h: allowed only when the process _Exits RIGHT AFTER the engine dies). The harness's
// _Exit used to be an on_exit handler registered at the run's FIRST line, so exit() - which runs its handlers last in,
// first out - ran every handler and static destructor registered AFTER it first, under live worker and driver threads:
// gi.scroll_slab.scroll crashed 2 times in 3 inside libnvidia-gpucomp.so after its laws passed.
// THE LAW, on a headless (NULL) engine taking the harness's exit-only teardown, in a child process:
//   (1) a handler registered with atexit AFTER the engine booted NEVER RUNS - the process ends at the engine's death;
//   (2) the exit status is what the run returned (3);
//   (3) THE FIXTURE'S EXIT CHECKS STILL RUN, BY NAME, in the final handler: a route declaration no frame asserted
//       (PHOTON-7-9A's PendingRoutes - a static whose destructor this exit path never reaches) is a FAIL by name and the
//       exit is 1.
// NEGATIVE CONTROL: the same child WITHOUT an engine - the late handler runs (the marker is readable at all).
#include <jahshaka/engine/Engine.h>
#include "enginetesthelpers.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace jahshaka::engine;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {

const char *kMarker = "LATE-ATEXIT-RAN";

/// The child's run, as a fixture's main: boot (or not), register a late handler, return 3 through exit().
[[noreturn]] void child(bool withEngine, bool pendingRoute = false)
{
    {
        std::unique_ptr<Engine> e;
        if (withEngine) {
            std::string err;
            e = Engine::create(enginetest::testEngineConfig("test-exit-only-teardown-ogre.log",
                                                            enginetest::EngineOverrides().headless()), err);
            if (!e) { std::printf("child: Engine::create: %s\n", err.c_str()); std::fflush(stdout); std::_Exit(90); }
        }
        std::atexit([] { std::printf("%s\n", kMarker); std::fflush(stdout); });
        // a declaration no frame will assert (the view and scene are never read: only the declaration's text)
        if (pendingRoute) enginetest::declareRoutes(nullptr, nullptr, enginetest::Routes().gather(), "the exit row's view");
    }   // the engine dies here, as at a fixture's main's end
    std::exit(3);
}

struct Run { int status = -1; std::string out; };

Run runChild(bool withEngine, bool pendingRoute = false)
{
    Run r;
    int fd[2];
    if (pipe(fd) != 0) return r;
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[0]);
        close(fd[1]);
        child(withEngine, pendingRoute);
    }
    close(fd[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(fd[0], buf, sizeof buf)) > 0) r.out.append(buf, size_t(n));
    close(fd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    r.status = WIFEXITED(st) ? WEXITSTATUS(st) : 1000 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    return r;
}

}  // namespace

int main()
{
    const Run none = runChild(false);
    CHECK(none.status == 3 && none.out.find(kMarker) != std::string::npos,
          "NEGATIVE CONTROL: with no engine the late atexit handler runs (status %d, marker %s)", none.status,
          none.out.find(kMarker) != std::string::npos ? "printed" : "absent");
    const Run eng = runChild(true);
    CHECK(eng.out.find(kMarker) == std::string::npos,
          "(1) an atexit registered after the engine booted never runs: the process ends at the engine's death (%s)",
          eng.out.find(kMarker) == std::string::npos ? "absent" : "PRINTED");
    CHECK(eng.status == 3, "(2) the exit status is what the run returned (3): %d", eng.status);
    const Run pend = runChild(true, true);
    CHECK(pend.status == 1 && pend.out.find("the exit row's view declared") != std::string::npos &&
              pend.out.find(kMarker) == std::string::npos,
          "(3) a route declaration no frame asserted fails the exit by name (status %d, the FAIL line %s)", pend.status,
          pend.out.find("the exit row's view declared") != std::string::npos ? "printed" : "ABSENT");
    if (gFailures) std::printf("---- the children's output ----\n%s----\n%s----\n", eng.out.c_str(), pend.out.c_str());
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
