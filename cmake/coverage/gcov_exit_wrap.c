/* THE COVERAGE BUILD's EXIT (TEST-STREAMLINE-1 stage 2; JAH_COVERAGE only - the default build never compiles this file).
 * Every process we own ends in _Exit: the engine fixtures (tests/support/enginetesthelpers.h, on_exit -> std::_Exit), jahlab
 * (src/lab/main.cpp) and the app (src/services/processexit.cpp). _Exit skips the exit-time writer of gcov's counters, so an
 * instrumented row would leave NO profile (measured on a toy, spikes/test-streamline-1/v1-gcov-exit-toy.txt: no .gcda at all;
 * with this wrapper the executable's AND its shared libraries' .gcda files, a running thread notwithstanding). Linked with
 * -Wl,--wrap=_Exit: every reference to _Exit in a linked object resolves here, which dumps the counters, then exits. */
extern void __gcov_dump(void);
extern void __real__Exit(int) __attribute__((noreturn));
void __wrap__Exit(int rc) __attribute__((noreturn));
void __wrap__Exit(int rc)
{
    __gcov_dump();
    __real__Exit(rc);
}
