#ifndef ENGINERENDERDRIVER_H
#define ENGINERENDERDRIVER_H

// The ONE render loop for the engine.
//
// Owned wherever the Engine is owned. A single QTimer calls Engine::renderOneFrame(),
// which draws every enabled View. Widgets never own a timer: two widgets with two
// timers would render every window twice per tick (the audit's finding). Hidden
// viewports opt out with View::setEnabled(false), not by stopping this.
//
// PACING (fps audit F1). The interval is NOT a constant: it is derived from the
// refresh rate of the screen the viewport is on, and the mode
// (services/framepacing.h) decides whether the presentation waits for the
// display at all. Read the long note in that header before changing anything
// here — the "fps steps to refresh/n" behaviour it describes is why a hardcoded
// 16 ms timer capped a 100 Hz panel at 62.5 fps and made the frame rate
// quantize instead of slide.
//
// Runs on the thread that owns the Engine (its thread-affinity rule).
#include <QObject>
#include "jahshaka/engine/Engine.h"
#include "services/framepacing.h"

class QTimer;

class EngineRenderDriver : public QObject
{
    Q_OBJECT
public:
    /// What the loop has actually been doing — the numbers behind `app.frameStats()`
    /// and the frame-pacing gate. Cumulative for the life of the driver.
    struct Stats {
        qulonglong ticks    = 0;   ///< timer fires
        qulonglong rendered = 0;   ///< ticks that called Engine::renderOneFrame
        qulonglong skipped  = 0;   ///< ticks that had no enabled View to draw

        // ---- how long the ticks actually TOOK (STATS_OVERLAY_SPEC §4) -------
        // THE HONEST NUMBER, and the reason this pair exists. An FPS reading on
        // this architecture measures the QTimer below (and vsync), not the
        // renderer: a scene that got twice as expensive but still fits in the
        // budget reads the same fps. What diagnoses anything is how much of each tick
        // the frame ate — which was already measured here and thrown away
        // unless it crossed the 100 ms hitch threshold.
        /// Rolling average of the last kWorkWindow rendered ticks, ms. 0 until
        /// the first one. Deliberately a SHORT window: this is a live readout,
        /// and an all-time average would never move again after a bad open.
        double     workMs   = 0.0;
        /// The worst rendered tick since the driver started, ms.
        double     worstMs  = 0.0;
        /// Rendered ticks that crossed the hitch threshold (100 ms) — the same
        /// ones that log `[open-profile] slow frame` and land in the
        /// LoadTimeline as `frame:slow`. Cumulative, like ticks/rendered.
        qulonglong slowFrames = 0;
    };

    explicit EngineRenderDriver(jahshaka::engine::Engine *engine, QObject *parent = nullptr);

    /// Starts the loop at the interval the current pacing implies (there is no
    /// interval argument any more: the display and the pacing mode decide, and
    /// a caller that pinned a number would silently un-pace the editor — this
    /// driver is shared by every host in the process).
    void start();
    void stop();
    bool isRunning() const;
    int  intervalMs() const;

    // ---- Pacing (fps audit F1; services/framepacing.h) ---------------------
    /// The pacing mode. Setting it re-times a running loop immediately AND
    /// pushes vsync to the engine's on-screen windows (which rebuilds their
    /// swapchains — a user action, never a per-frame one).
    void setPacingMode(framepacing::Mode m);
    framepacing::Mode pacingMode() const { return mMode; }
    /// The refresh rate of the screen the viewport lives on, in Hz. The host
    /// pushes it at startup and again whenever the window changes screen or the
    /// screen changes mode; 0 means "unknown" and the loop falls back to 16 ms.
    void setRefreshHz(double hz);
    double refreshHz() const { return mRefreshHz; }
    /// What the two above currently imply, whether or not the loop is running.
    int pacedIntervalMs() const { return framepacing::intervalMsFor(mMode, mRefreshHz); }

    Stats stats() const { return mStats; }

signals:
    /// Emitted before each frame — animate here.
    void beforeFrame();
    /// Mode, refresh rate or the resulting interval changed. The Preferences
    /// page listens so two open surfaces cannot disagree.
    void pacingChanged();

private:
    /// How many rendered ticks the rolling work-ms average covers. About a
    /// second at a 60 Hz pacing — long enough to be readable, short enough
    /// that the number still reacts to what the app is doing right now.
    static constexpr int kWorkWindow = 60;

    /// Re-times the timer from mMode/mRefreshHz, if it is running, and emits
    /// pacingChanged(). The one place the interval is ever written.
    void applyPacing();

    jahshaka::engine::Engine *mEngine;
    QTimer *mTimer;
    Stats   mStats;
    framepacing::Mode mMode = framepacing::Mode::Display;
    /// 0 until a host tells us (see setRefreshHz) — the fallback interval then.
    double  mRefreshHz = 0.0;
    /// Ring of the last kWorkWindow rendered ticks' durations, ms.
    double  mWork[kWorkWindow] = {};
    int     mWorkNext = 0;
    int     mWorkFilled = 0;
};

#endif // ENGINERENDERDRIVER_H
