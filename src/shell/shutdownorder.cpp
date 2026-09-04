/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/shutdownorder.h"

#include <QDebug>

#include <cstdio>

namespace ShutdownOrder {

void record(int step, const char *name)
{
#ifdef QT_DEBUG
    // Deliberately not a QVector: this runs while the app is being taken
    // apart, and the last steps happen after the services (and, at step 7,
    // most of Qt's world) are gone. A fixed array and a raw fprintf survive
    // that; a container that allocates would be one more thing to trust.
    static int  sSeen[8] = { 0 };
    static int  sHighest = 0;

    if (step < 1 || step > 7) return;
    if (sSeen[step]++)
        qWarning("[shutdown] step %d (%s) fired again (%dx) — the order in "
                 "shell/shutdownorder.h says once", step, name, sSeen[step]);
    else if (step < sHighest)
        qWarning("[shutdown] step %d (%s) fired AFTER step %d — out of order; "
                 "see shell/shutdownorder.h", step, name, sHighest);
    if (step > sHighest) sHighest = step;

    // stderr directly, unbuffered and Qt-free: step 7 can run after the
    // message handler's world has been dismantled, and the gate reads these
    // lines out of the spawned process's output.
    std::fprintf(stderr, "[shutdown] step %d/7 %s\n", step, name);
    std::fflush(stderr);
#else
    Q_UNUSED(step);
    Q_UNUSED(name);
#endif
}

WidgetTreeMarker::~WidgetTreeMarker()
{
    JAH_SHUTDOWN_STEP(WidgetTree, "~QWidget(MainWindow): the child widget tree");
}

}   // namespace ShutdownOrder
