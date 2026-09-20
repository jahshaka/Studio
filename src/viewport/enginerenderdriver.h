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
#include <QElapsedTimer>
#include <QObject>
#include "jahshaka/engine/Engine.h"
#include "services/framepacing.h"
#include "viewport/framewindows.h"

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

        // THE `skipped` COUNTER IS GONE (owner review 2026-09-18, answer Q3).
        // It counted ticks with no enabled View — the app sitting on the
        // Desktop or Assets page at ~62 ticks a second — and the F3 readout
        // showed it as a lifetime total ("104,329 skipped"), which reads as
        // dropped frames and is in fact the loop doing exactly the right
        // thing. Nothing is lost: every tick is either rendered or not, so the
        // number is `ticks - rendered` for anybody who wants it, and the LIVE
        // half of the question — is the loop drawing right now? — is `drawing`
        // below.
        /// Did the last counted tick draw? The idle/drawing state of the F3
        /// readout; false on a page with no visible viewport.
        bool       drawing  = false;
        /// Frames ACTUALLY DRAWN in the last second — not ticks, and not
        /// Ogre's own rolling average. It counts frames drawn OUTSIDE this
        /// loop too (a script's editor.frame, the VR pump), because what the
        /// readout answers is "is the picture moving", and those frames move
        /// it (see noteExternalFrame).
        double     fpsDrawn = 0.0;
        /// Frames over the 100 ms hitch threshold IN THE LAST MINUTE. A
        /// rolling window, never a lifetime total: a live readout whose number
        /// can only go up says nothing about now. `slowFrames` below keeps the
        /// cumulative figure for the session log and the perf sampler, which
        /// want exactly that.
        int        slowFramesLastMinute = 0;

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
    int pacedIntervalMs() const {
        // A VR SESSION IS THE CLOCK (SPECS/VR_SPEC.md §4.3). While one runs,
        // `Engine::renderOneFrame` blocks at the top in `xrWaitFrame` until the
        // runtime wants the next picture, so a timer of our own can only make
        // the loop LATE — "two pacers, the slower wins", and the slower one
        // must be the headset. Zero interval, and vsync off below so the
        // mirror window's own swapchain cannot gate the loop either.
        if (mVrSession && mVrPumping) return 0;
        return framepacing::intervalMsFor(mMode, mRefreshHz);
    }

    // ---- VR (SPECS/VR_SPEC.md §4.3) ---------------------------------------
    /// Called once when a VR session begins and once when it ends. It does NOT
    /// touch the pacing MODE — that is the user's setting and must survive the
    /// session — it overrides the interval and vsync for the duration and puts
    /// both back afterwards.
    void setVrSessionActive(bool on);
    bool vrSessionActive() const { return mVrSession; }

private:
    /// Reconciles this loop with the engine's session, once per tick (F5).
    /// TWO things can make the driver's pacing a lie, and neither of them goes
    /// through the host: the engine ENDS a session by itself when the runtime
    /// or the device goes away, and a live session STOPS BLOCKING whenever it
    /// is not between Ready and Focused (the headset is off, the runtime is
    /// still coming up, the session is stopping). Either way a zero interval
    /// with the pump not blocking is a spin, so the tick asks and re-times.
    void syncVrPacing();

public:

    /// The counters, with the two ROLLING ones evaluated at the moment of the
    /// read (they are windows over a clock, not accumulators — see
    /// viewport/framewindows.h), so a reader that stops reading cannot leave a
    /// stale rate standing.
    Stats stats() const;

    // ---- the script run policy (SCRIPTING_LIVE_SPEC §3.1) -----------------
    /// What a script run in flight is doing to this loop. ONE setter, called
    /// once when a run starts and once when it ends, because the two policies
    /// are two answers to the same question and a second flag could disagree
    /// with the first.
    enum class ScriptRun {
        None,   ///< no script is running: the loop is nobody's business but its own
        /// A run whose policy is Off owns the loop: the tick does NOTHING for
        /// the length of it. Not stop() — the timer keeps running, so resuming
        /// costs nothing and needs no signal from anybody (the same reason the
        /// empty-viewport skip below is per tick). The whole tick is skipped,
        /// beforeFrame included, and that is the point: it reproduces EXACTLY
        /// what a script used to get for free by holding the UI thread — no
        /// frame, no mirror sync, no simulated time between two verbs — which
        /// is what 54 frame-stepping e2e scripts and 18 frame-counter readers
        /// were written against. (The usual warning about skipping beforeFrame
        /// — its subscribers pull a wall-clock delta, so an idle period banks
        /// into the first resumed frame — applies and is accepted: it is the
        /// behaviour a blocked UI thread already had.)
        Off,
        /// A run whose policy is Live wants the picture to move, and it is
        /// PACED BY TIME while it does. Without pacing the loop and the script
        /// strictly alternate — one frame per verb, whatever the verb costs —
        /// because the worker can only post its next hop after the previous one
        /// returns, and in that gap an overdue timer always wins. Measured: a
        /// trivial verb cost 8.1 ms instead of 30 us, a 270x tax on a
        /// query-dense script. At most one frame per DISPLAY PERIOD while a
        /// live run is in flight puts that back to the honest price of the
        /// frames the user asked to see.
        Live
    };
    /// Called once at the start and once at the end of a run. Gated strictly on
    /// Live, so a session with no script running is byte-identical to one with
    /// no pacing code at all — frame stats, the render monitor, everything.
    void setScriptRun(ScriptRun run) { mScriptRun = run; }
    ScriptRun scriptRun() const { return mScriptRun; }

    /// A FRAME THIS LOOP DID NOT DRAW, but the display now shows (DOUBLE-FRAME-1,
    /// 2026-09-18). The Live pacing above is "at most one frame per display
    /// period", and it used to mean at most one of MY frames: the clock was
    /// restarted only at the end of a driver tick, so a script that renders its
    /// own frames — `editor.frame()`, which is how every drag, every step and
    /// every MCP gesture moves the picture deterministically — got a driver
    /// frame in the gap between two verbs ON TOP of the one it had just drawn.
    /// MEASURED with `--script-live`, 60 scripted frames per arm: 81 engine
    /// frames AT REST (1.35 per scripted frame, with no document edit at all)
    /// and 89-95 while dragging a cube (1.48-1.58) — which is the "two frames
    /// per document edit" the render audit recorded from an MCP-driven drag,
    /// seen from its actual cause: the frames are not caused by the EDIT, they
    /// are caused by the script drawing.
    ///
    /// So every host loop that renders outside this tick says so, and the
    /// period is measured from the last frame ANYBODY drew.
    ///
    /// It also STAMPS THE FRAME into the drawn-rate window (Stats::fpsDrawn):
    /// a frame a script drew is a frame the user saw, and a readout that said
    /// "0 fps drawn" through a scripted animation would be exactly the kind of
    /// lie this round of the readout exists to remove.
    void noteExternalFrame();

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
    /// One display period, in ms — the ceiling on how often a LIVE script run
    /// is allowed to cost a frame.
    double scriptPacePeriodMs() const;

    jahshaka::engine::Engine *mEngine;
    QTimer *mTimer;
    Stats   mStats;
    framepacing::Mode mMode = framepacing::Mode::Display;
    /// 0 until a host tells us (see setRefreshHz) — the fallback interval then.
    double  mRefreshHz = 0.0;
    /// What the script run in flight (if any) is doing to this loop.
    ScriptRun mScriptRun = ScriptRun::None;
    /// True between Engine::beginVrSession and endVrSession (setVrSessionActive).
    bool mVrSession = false;
    /// True while that session is actually PUMPING — i.e. while renderOneFrame
    /// blocks in xrWaitFrame and is therefore the clock. False for the states
    /// either side of it, where this loop must pace itself again.
    bool mVrPumping = false;
    /// Since the END of the last rendered tick, for the Live pacing above.
    /// From the END, not the start: a 33 ms Debug frame measured from its start
    /// is already past a 16.7 ms period the instant it finishes, and the
    /// alternation this exists to break would survive untouched.
    QElapsedTimer mSinceFrameEnd;
    /// Ring of the last kWorkWindow rendered ticks' durations, ms.
    double  mWork[kWorkWindow] = {};
    int     mWorkNext = 0;
    int     mWorkFilled = 0;

    // ---- the two ROLLING windows (owner review answer Q3) -----------------
    /// The driver's own monotonic clock, started at construction. It is the
    /// only clock the windows below ever see, and it is read exactly twice per
    /// tick — the stamps have to come from one source or a rate computed from
    /// two of them would be nonsense.
    QElapsedTimer mSinceStart;
    /// One stamp per frame ANYBODY drew (this loop's, and every host loop that
    /// calls noteExternalFrame).
    framewindows::EventWindow mDrawn{ framewindows::kDrawnRateWindowMs };
    /// One stamp per frame over the hitch threshold.
    framewindows::EventWindow mSlow{ framewindows::kSlowFrameWindowMs };
    /// `mSinceStart` in ms — one call, so every stamp in one tick agrees.
    double nowMs() const { return double(mSinceStart.nsecsElapsed()) / 1.0e6; }
};

#endif // ENGINERENDERDRIVER_H
