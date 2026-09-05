/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// perf.pacing — the interval policy behind panel-aware frame pacing
// (fps audit F1, src/services/framepacing.h).
//
// DISPLAY-FREE ON PURPOSE. The policy is a pure function of a mode and a
// refresh rate, so it is testable without a screen, without an engine and
// without a window — which is the whole reason it lives in its own header
// instead of inside the render driver. What is NOT tested here (and cannot be,
// off-screen) is the value QScreen::refreshRate() reports on a given box; the
// driver takes that number from the host and this pins what it does with it.

#include <cstdio>
#include <limits>

#include <QCoreApplication>

#include "services/framepacing.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

using framepacing::Mode;
using framepacing::intervalMsFor;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- the names round-trip (the persisted value and the verb argument) ---
    {
        bool ok = false;
        CHECK(framepacing::modeFromName("display", &ok) == Mode::Display && ok, "'display' parses");
        CHECK(framepacing::modeFromName("unlimited", &ok) == Mode::Unlimited && ok, "'unlimited' parses");
        CHECK(framepacing::modeFromName("  Unlimited  ", &ok) == Mode::Unlimited && ok,
              "parsing is case- and whitespace-tolerant");
        framepacing::modeFromName("nonsense", &ok);
        CHECK(!ok, "an unknown mode reports failure instead of guessing");
        CHECK(framepacing::modeFromName("nonsense") == Mode::Display,
              "and falls back to Display, so a corrupt setting cannot break the loop");
        CHECK(framepacing::modeName(Mode::Display) == QLatin1String("display") &&
              framepacing::modeName(Mode::Unlimited) == QLatin1String("unlimited"),
              "names round-trip");
        CHECK(framepacing::modeNames().size() == 2, "both modes are listed for the UI and the verb");
    }

    // ---- the panel decides the interval ------------------------------------
    // THE POINT OF THE WHOLE LANE: a 100 Hz panel used to be paced by a
    // hardcoded 16 ms timer (62.5 fps ceiling). It must now get 10.
    CHECK(intervalMsFor(Mode::Display, 100.0) == 10, "100 Hz paces at 10 ms (was a hardcoded 16)");
    CHECK(intervalMsFor(Mode::Display, 60.0) == 16, "60 Hz paces at 16 ms");
    CHECK(intervalMsFor(Mode::Display, 59.94) == 16, "59.94 Hz still lands on 16 ms");
    CHECK(intervalMsFor(Mode::Display, 144.0) == 6, "144 Hz paces at 6 ms");
    CHECK(intervalMsFor(Mode::Display, 240.0) == 4, "240 Hz paces at 4 ms");

    // FLOOR, NOT ROUND. Rounding 60 Hz gives 17 ms = 58.8 fps: the timer would
    // become the slower pacer again and cap the panel it was derived from.
    CHECK(intervalMsFor(Mode::Display, 60.0) * 60 <= 1000,
          "the interval always undershoots the refresh period (the timer is never the cap)");
    CHECK(intervalMsFor(Mode::Display, 100.0) * 100 <= 1000, "the same at 100 Hz");

    // ---- the guards --------------------------------------------------------
    CHECK(intervalMsFor(Mode::Display, 0.0) == framepacing::kFallbackIntervalMs,
          "an unknown refresh rate falls back to 16 ms, not to a spin");
    CHECK(intervalMsFor(Mode::Display, -120.0) == framepacing::kFallbackIntervalMs,
          "a negative refresh rate falls back too");
    CHECK(intervalMsFor(Mode::Display, std::numeric_limits<double>::quiet_NaN()) ==
              framepacing::kFallbackIntervalMs,
          "a NaN refresh rate falls back too");
    CHECK(intervalMsFor(Mode::Display, 4000.0) == framepacing::kMinIntervalMs,
          "an absurd refresh rate clamps to the 4 ms floor rather than spinning the loop");
    CHECK(intervalMsFor(Mode::Display, 10.0) == framepacing::kMaxIntervalMs,
          "a very slow display clamps to the 33 ms ceiling (30 fps floor)");

    // ---- unlimited ---------------------------------------------------------
    CHECK(intervalMsFor(Mode::Unlimited, 100.0) == 0, "unlimited does not wait");
    CHECK(intervalMsFor(Mode::Unlimited, 0.0) == 0, "unlimited ignores the refresh rate entirely");
    CHECK(framepacing::vsyncFor(Mode::Display), "Display keeps vsync on");
    CHECK(!framepacing::vsyncFor(Mode::Unlimited),
          "Unlimited turns vsync OFF — a 0 ms timer against a blocking present is only a busier CPU");

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall pacing checks passed\n", failures);
    return failures == 0 ? 0 : 1;
}
