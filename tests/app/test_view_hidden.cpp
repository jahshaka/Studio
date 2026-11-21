// view.hidden_draws_nothing — NOTHING RENDERS WHEN NOBODY LOOKS (ONE-FRAME-1 stage 3, V2 plan 2.4; ruling R4), on the CPU:
// the offscreen platform, no display, no card.
//
// THE LAW: an EngineViewWidget's View is enabled exactly while somebody can see it — EngineViewWidget::wantsToDraw:
// SHOWN (Qt's show/hide events) AND its top-level window EXPOSED (QWindow::isExposed at the window's expose events) —
// and with no view enabled the render loop draws nothing and ticks at its fallback interval (framepacing::
// tickIntervalMs, the driver's one rule).
//  1. MINIMISE HIDES THE VIEW (the spec's UNVERIFIED premise, decided here): setWindowState(Qt::WindowMinimized) on the
//     top-level sends its children a hide (nativeHides +1) and the widget stops wanting to draw; restoring draws again.
//     isVisible() is NOT the predicate: a minimised window's widgets still answer true — the second "is visible" flag
//     the widget's enable sites read before (createView, a native-window rebuild), folded into the one.
//  2. AN EXPOSE IS READ AT THE TOP-LEVEL: a synthetic QExposeEvent on the top-level window reaches the widget's filter
//     (its exposeEvents count moves, its exposure is the window's own isExposed); a not-exposed report (a window fully
//     covered, on another workspace) stops the drawing, an exposed one restores it.
//  3. THE TICK's RULE: nothing to draw -> the fallback interval (100 ms: the document clock keeps true time, 6 steps a
//     tick under its 8), whatever the pacing or a hold; drawing -> the pacing's (or the held interval).
// RED ON THE BASE 273b8e638: the widget had no exposure input at all (nothing in src/ read QWindow::isExposed, the
// ground read § 2.4), and the driver kept ticking at the paced interval with nothing to draw (enginerenderdriver.cpp's
// "THE TIMER KEEPS RUNNING"). The frame law with a real View (published().frame unchanged over N ticks while
// minimised, a thumbnail answered meanwhile) needs a View, which the NULL render system holds none of: the card's.
#include <QApplication>
#include <QExposeEvent>
#include <QWidget>
#include <QWindow>
#include <cstdio>

#include "services/framepacing.h"
#include "viewport/engineviewwidget.h"

static int gFailures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // ---- 1. minimise -------------------------------------------------------------------------------------------------
    QWidget window;
    window.resize(320, 240);
    auto *view = new EngineViewWidget(&window);
    view->resize(320, 240);
    view->setOffscreen(jahshaka::engine::OffscreenContract::StillPicture);   // no native window on the offscreen platform
    check(!view->wantsToDraw(), "a widget never shown does not draw");
    window.show();
    app.processEvents();
    check(view->wantsToDraw(), "shown (and its top-level exposed): it draws");
    const qulonglong hides = view->nativeHides();
    window.setWindowState(Qt::WindowMinimized);
    app.processEvents();
    std::printf("      minimised: nativeHides %llu -> %llu, isVisible %d, wantsToDraw %d\n", hides, view->nativeHides(),
                int(view->isVisible()), int(view->wantsToDraw()));
    check(view->nativeHides() == hides + 1, "MINIMISE SENDS THE CHILD A HIDE (the premise, measured): nativeHides +1");
    check(!view->wantsToDraw(), "...and the minimised window's view does not draw");
    check(view->isVisible(), "...though isVisible() still answers true (why it is not the predicate)");
    window.setWindowState(Qt::WindowNoState);
    app.processEvents();
    std::printf("      restored: wantsToDraw %d\n", int(view->wantsToDraw()));
    check(view->wantsToDraw(), "restored: it draws again");

    // ---- 2. the expose -------------------------------------------------------------------------------------------------
    QWindow *top = window.windowHandle();
    check(top != nullptr, "the top-level has a window");
    if (top) {
        const qulonglong seen = view->exposeEvents();
        QExposeEvent expose{ QRegion() };
        QCoreApplication::sendEvent(top, &expose);
        std::printf("      a synthetic expose: exposeEvents %llu -> %llu, the window's isExposed %d, the widget's %d\n",
                    seen, view->exposeEvents(), int(top->isExposed()), int(view->topExposed()));
        check(view->exposeEvents() == seen + 1, "the top-level's expose event reaches the widget's filter");
        check(view->topExposed() == top->isExposed(), "...and its exposure is the window's own isExposed()");
    }
    view->noteTopExposed(false);   // a window fully covered / on another workspace: the platform's report
    check(!view->wantsToDraw(), "NOT EXPOSED: the view does not draw");
    view->noteTopExposed(true);
    check(view->wantsToDraw(), "exposed again: it draws");

    // ---- 3. the tick's rule --------------------------------------------------------------------------------------------
    const int fallback = 100, heldMs = 4, paced = 16;
    check(framepacing::tickIntervalMs(true, false, false, paced, fallback, heldMs) == fallback &&
              framepacing::tickIntervalMs(true, false, true, 0, fallback, heldMs) == fallback,
          "nothing to draw: the tick at the fallback interval, whatever the pacing (even Unlimited's 0) or a hold");
    check(framepacing::tickIntervalMs(false, false, false, paced, fallback, heldMs) == paced,
          "drawing: the pacing's interval");
    check(framepacing::tickIntervalMs(false, false, true, paced, fallback, heldMs) == heldMs,
          "drawing, held: the held interval");
    check(framepacing::tickIntervalMs(false, true, false, paced, fallback, heldMs) == fallback,
          "drawing, the present pacing the ticks: the timer is its fallback");

    std::printf("view.hidden_draws_nothing: %s\n", gFailures ? "FAILED" : "all ok");
    return gFailures ? 1 : 0;
}
