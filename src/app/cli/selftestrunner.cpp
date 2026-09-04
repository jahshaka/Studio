/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/cli/selftestrunner.h"

#include <cstdio>

#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QSize>
#include <QThread>
#include <QWidget>

#include "bridge/enginehost.h"
#include "shell/mainwindow.h"
#include "viewport/ieditorviewport.h"

int runEngineSelftest(MainWindow &window, QApplication &app, const QString &outPng)
{
    window.show();
    app.processEvents();

    QString why;
    if (!window.beginEngineSelftest(why)) {
        std::fprintf(stderr, "engine-selftest: %s\n", qPrintable(why));
        return 1;
    }

    // Pump the render loop for ~30 frames (the driver ticks every 16 ms).
    QElapsedTimer clock;
    clock.start();
    // Resize twice on the way (the layout does this to the viewport in real use):
    // the engine must survive a swapchain rebuild without a stale depth buffer.
    QSize afterFirstResize, afterSecondResize;
    QSize widgetAfterFirst, widgetAfterSecond;
    for (int frame = 0; frame < 40; ++frame) {
        if (frame == 10) window.resize(1100, 760);
        if (frame == 25) window.resize(700, 520);
        if (frame == 20) { afterFirstResize  = window.viewport()->renderTargetSize();
                           widgetAfterFirst  = window.viewport()->asWidget()->size(); }
        if (frame == 35) { afterSecondResize = window.viewport()->renderTargetSize();
                           widgetAfterSecond = window.viewport()->asWidget()->size(); }
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
    app.processEvents();

    // On-screen coverage that does NOT assert pixels (MACOS_VIEWPORT_SPEC §5.1):
    // the pixel assertion below deliberately goes through a separate OFFSCREEN
    // view, so without this the selftest would pass just as happily with a blank
    // widget. Platforms where the on-screen path is a swapchain window must prove
    // the view is not offscreen and that it survived both resizes with a real size.
//
// LINUX TOO since the swapchain lane (deep audit area 7 F3). renderTargetSize()
// used to report the values the widget had pushed down, so this block could only
// ever have compared them with themselves; it now reads the live render target,
// which is what makes the size comparison below an assertion rather than a
// tautology. Windows joins when it grows a window backend.
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    if (window.viewport()->isOffscreen()) {
        std::fprintf(stderr, "engine-selftest: the editor viewport is OFFSCREEN — the on-screen "
                             "window backend did not take\n");
        return 1;
    }
    if (afterFirstResize.isEmpty() || afterSecondResize.isEmpty()) {
        std::fprintf(stderr, "engine-selftest: viewport lost its render target across a resize "
                             "(%dx%d then %dx%d)\n",
                     afterFirstResize.width(), afterFirstResize.height(),
                     afterSecondResize.width(), afterSecondResize.height());
        return 1;
    }
    // THE assertion: the render target tracks the widget. Not "the two resizes
    // produced different numbers" — a window manager is entitled to refuse a
    // resize, and then the widget did not change either and there is nothing to
    // report. What must never happen is the swapchain being a different size
    // from the window it presents into: that is the stale-swapchain state the
    // viewport gets stuck in when nothing but Ogre's OUT_OF_DATE self-heal is
    // driving the rebuild.
    if (afterFirstResize != widgetAfterFirst || afterSecondResize != widgetAfterSecond) {
        std::fprintf(stderr,
                     "engine-selftest: the render target did not follow the viewport across a "
                     "resize — target %dx%d vs widget %dx%d, then target %dx%d vs widget %dx%d\n",
                     afterFirstResize.width(), afterFirstResize.height(),
                     widgetAfterFirst.width(), widgetAfterFirst.height(),
                     afterSecondResize.width(), afterSecondResize.height(),
                     widgetAfterSecond.width(), widgetAfterSecond.height());
        return 1;
    }
    std::fprintf(stderr, "engine-selftest: on-screen view survived resizes: %dx%d then %dx%d "
                         "(widget %dx%d then %dx%d)\n",
                 afterFirstResize.width(), afterFirstResize.height(),
                 afterSecondResize.width(), afterSecondResize.height(),
                 widgetAfterFirst.width(), widgetAfterFirst.height(),
                 widgetAfterSecond.width(), widgetAfterSecond.height());
#endif

    QImage img = window.viewport()->takeScreenshot(256, 256);
    if (img.isNull()) {
        std::fprintf(stderr, "engine-selftest: takeScreenshot returned a null image after %lld ms\n",
                     static_cast<long long>(clock.elapsed()));
        return 1;
    }
    if (!img.save(outPng, "PNG")) {
        std::fprintf(stderr, "engine-selftest: could not save %s\n", qPrintable(outPng));
        return 1;
    }
    const QColor centre = img.pixelColor(img.width() / 2, img.height() / 2);
    const QColor clear = QColor::fromRgbF(0.10f, 0.11f, 0.14f);
    const int tolerance = 2;
    const bool differs = qAbs(centre.red() - clear.red()) > tolerance ||
                         qAbs(centre.green() - clear.green()) > tolerance ||
                         qAbs(centre.blue() - clear.blue()) > tolerance;
    std::fprintf(stderr, "engine-selftest: %dx%d image, centre pixel (%d,%d,%d), clear (%d,%d,%d) -> %s\n",
                 img.width(), img.height(), centre.red(), centre.green(), centre.blue(),
                 clear.red(), clear.green(), clear.blue(), differs ? "PASS" : "FAIL");
    window.endEngineSelftest();
    EngineHost::instance().shutdown();
    return differs ? 0 : 1;
}
