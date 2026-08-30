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
#include <QThread>

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
    for (int frame = 0; frame < 40; ++frame) {
        if (frame == 10) window.resize(1100, 760);
        if (frame == 25) window.resize(700, 520);
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
    app.processEvents();

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
