#ifndef FRAMEPACING_H
#define FRAMEPACING_H

// FRAME PACING — how fast the ONE render loop is allowed to tick, and whether
// the presentation waits for the display (fps audit F1).
//
// THE PROBLEM THIS FILE ANSWERS. EngineRenderDriver is a QTimer, and its
// interval was the literal 16 (mainwindow.cpp, and the driver's own default
// argument). 16 ms is 62.5 ticks a second, so on a 100 Hz panel the editor
// could never draw more than 62 frames a second no matter how cheap the frame
// was — and, because the swapchain acquire BLOCKS until the display releases an
// image, the two pacers beat against each other and the result quantizes: the
// loop settles on refresh/n (100/2 = 50, 100/3 = 33, 100/4 = 25) rather than
// sliding smoothly. The owner's "60 -> 24 fps" episode was exactly one step of
// that divisor, not a renderer regression.
//
// So the interval is DERIVED from the display, and the whole thing is one
// user-visible choice with two settings:
//
//   Display   (default) the timer beats a little FASTER than the panel
//             (floor(1000/refreshRate), so 100 Hz -> 10 ms) and vsync does the
//             actual pacing. Tear-free, and the ceiling is the panel's rate
//             instead of 62.5.
//   Unlimited no timer wait (0 ms) and vsync OFF: frames go out as fast as the
//             loop can make them, tearing included. This is the honest "what
//             can this machine actually do" mode and what a capacity
//             measurement should use.
//
// EVERYTHING HERE IS PURE — no engine, no widgets, no display access — so the
// interval policy is unit-testable off-screen (tests/perf, suite `perf.pacing`).
// The driver holds the state; MainWindow feeds it the screen's refresh rate and
// the persisted mode; `app.pacing()` and Preferences > Viewport are two callers
// of the same driver verb.

#include <QString>
#include <QStringList>

namespace framepacing {

enum class Mode {
    Display,     ///< refresh-derived interval, vsync on (the default)
    Unlimited    ///< no interval, vsync off
};

/// The persisted key (SettingsManager). Values are modeName() spellings, so an
/// unknown/absent value falls back to Display without a migration.
inline const char *settingsKey() { return "viewport/pacing"; }

QString     modeName(Mode m);                 ///< "display" | "unlimited"
Mode        modeFromName(const QString &name, bool *ok = nullptr);
QStringList modeNames();
/// The human label for a mode — the Preferences combo and nothing else.
QString     modeLabel(Mode m);

/// What the timer interval must be, in milliseconds, for `m` on a display
/// running at `refreshHz`. 0 means "no wait" (Unlimited).
///
/// FLOOR, NOT ROUND, on purpose: the tick must never be the slower of the two
/// pacers, or it becomes the cap again (60 Hz rounds to 17 ms = 58.8 fps, while
/// flooring to 16 ms lets vsync pace it at 60). An unusable refresh rate (0, a
/// NaN, anything a headless/offscreen QScreen reports) falls back to the
/// historical 16 ms rather than guessing.
int intervalMsFor(Mode m, double refreshHz);

/// Whether the engine's on-screen windows should wait for the display in `m`.
inline bool vsyncFor(Mode m) { return m == Mode::Display; }

/// The interval used when a refresh rate cannot be had. This is what the loop
/// ran at unconditionally before this file existed.
inline constexpr int kFallbackIntervalMs = 16;
/// Sane floor: a 250 Hz ceiling on the loop. Protects against a bogus
/// four-digit refresh report turning the render loop into a spin.
inline constexpr int kMinIntervalMs = 4;
/// Sane ceiling: 30 fps. A panel claiming 24 Hz (or a virtual display claiming
/// something absurd) must not pace the editor slower than that.
inline constexpr int kMaxIntervalMs = 33;

}   // namespace framepacing

#endif   // FRAMEPACING_H
