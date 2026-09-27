// THE MILLISECOND BARS ARE NIGHTLY (lane D6B-GATE-SHAPE; docs/TESTING_GATE.md §4,
// SPECS/audits/GATE_SUITES_AUDIT_2026-09-26.md §5).
//
// A bar stated in milliseconds of wall clock or GPU time reads the BOX, not only the
// code: the PUSH tier runs four suites at once beside other lanes' gates, and the
// GPU-timing lock excludes only the other lock holders — never the CPU load of the
// suites around it. open.responsive's 300 ms frame bar went red that way (605 ms cold
// at -j4), and so did gi.field_follows' worst frame.
//
// So a suite with such a bar is registered TWICE over one binary:
//   * the push-tier row runs every COUNT, structure and picture assertion, and PRINTS
//     each millisecond bar's reading ("time: within/OVER ...") without deciding on it;
//   * `<suite>.timing` runs the same binary with JAHSHAKA_TIMING_BARS=1, which ARMS
//     the millisecond bars. It is labelled `nightly;quiet-box` (never a scoped gate,
//     never the MERGE/PUSH tier) and registered through the GPU-timing lock.
// Nothing is deleted: every bar is asserted where the box can answer it.
#pragma once
#include <cstdio>
#include <cstdlib>

namespace jahtest {

/// True in a `<suite>.timing` row (JAHSHAKA_TIMING_BARS set and not "0").
inline bool timingBarsArmed()
{
    const char *v = std::getenv("JAHSHAKA_TIMING_BARS");
    return v && *v && !(v[0] == '0' && v[1] == '\0');
}

} // namespace jahtest

/// A millisecond bar: asserted through the including file's own CHECK(cond, msg) in the
/// `<suite>.timing` row, PRINTED (never failed) in the push-tier row. `cond` is evaluated
/// exactly once either way, so a bar whose evaluation measures (a control re-roll) costs
/// the same in both rows.
#define JAH_TIMING_CHECK(suite, cond, msg) do { const bool jahTimingOk_ = (cond); \
    if (jahtest::timingBarsArmed()) { CHECK(jahTimingOk_, msg); } \
    else std::printf("time: %s: %s (a millisecond bar: %s.timing asserts it)\n", \
                     jahTimingOk_ ? "within" : "OVER", (msg), (suite)); } while (0)
