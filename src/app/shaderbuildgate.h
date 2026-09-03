#ifndef SHADERBUILDGATE_H
#define SHADERBUILDGATE_H

// The startup shader build happens BEHIND THE LAUNCH SCREEN. Always.
//
// Owner decision, 2026-09-04: "shader building at startup must happen behind
// the startup screen, even incremental rebuilds", with a countdown on the
// splash. The rationale is not cosmetic — the open.responsive gate's warm
// budget has about 6 ms of headroom, and shader compilation racing that budget
// inside the live window is a flake generator. Moving the whole burst in front
// of the window removes the race instead of tuning it.
//
// WHAT THIS IS. After MainWindow is constructed (which starts the engine,
// creates the views and registers the Hlms) but before the splash is dismissed,
// this pumps the event loop — so the render driver's frames actually run, and
// every shader those frames need compiles — while the splash shows
// "Building shaders 34/68". It returns once the compile count has stopped
// moving, or when the hard deadline expires.
//
//   first-ever launch : the full build, behind the splash, tens of seconds if
//                       the machine is slow. Nothing renders half-built.
//   warm launch       : the counter races through cache hits and the gate
//                       returns almost immediately.
//   any launch        : a compile burst NEVER lands behind the live UI.
//
// The denominator comes from the last saved cache (ShaderCacheStats::
// expectedShaders): the run that wrote the cache recorded how many shaders it
// needed. A first-ever launch has no denominator and shows a plain count.
//
// Everything here is observable through the existing verbs: the numerator is
// app.shaderCache().compiledThisRun + .loadedThisRun, and the denominator is
// .expectedShaders.

#include <QtGlobal>

class QApplication;
class VersionSplashScreen;

/// Pumps the event loop until the startup shader build settles. Returns the
/// number of shaders built or served (0 when no engine is running — headless
/// runs return immediately). Safe to call when the cache is disabled: the
/// counters run regardless, and the gate simply waits for the same burst.
unsigned holdSplashForShaderBuild(QApplication &app, VersionSplashScreen &splash);

#endif // SHADERBUILDGATE_H
