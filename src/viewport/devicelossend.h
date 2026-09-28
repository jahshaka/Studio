/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DEVICELOSSEND_H
#define DEVICELOSSEND_H

// THE END OF A SESSION WHOSE GPU DIED (lane XID-2, 2026-09-17).
//
// A lost Vulkan device is not recoverable in this process: the renderer stops
// recreating it (fork d014b064f+1bccc3f93 (was 0072), because `vkDestroyDevice` on a device whose
// channel the driver has not reclaimed DOES NOT RETURN -- it spins at 100 % of
// a core for ever, which is the freeze the owner reported), and every later
// frame throws instead of drawing. Measured on the XID-2 reproducer before this
// existed: 4,348 `VK_ERROR_DEVICE_LOST` exceptions in one run, a window that
// never updated again, and a SEGV in the ordinary teardown at the end.
//
// So EVERY loop that calls `Engine::renderOneFrame()` asks this afterwards, and
// this says the sentence once and ends the process WITHOUT running destructors.
// Skipping the teardown is the point: an orderly quit walks back into the
// driver call that hangs.

#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <string>
#include <unistd.h>

#include "jahshaka/engine/Engine.h"
#include "services/jahlog.h"

namespace devicelossend {

/// Is there a person in front of this process? A scripted, headless or
/// self-test run must end with a status and no modal dialog -- one would block
/// for ever, since the exit is on the far side of it.
inline bool sessionHasAUser()
{
    if (!qApp || !qobject_cast<QApplication *>(qApp)) return false;
    if (!qEnvironmentVariableIsEmpty("JAHSHAKA_NO_DEVICE_LOSS_DIALOG")) return false;
    const QStringList args = QCoreApplication::arguments();
    for (const QString &a : args) {
        if (a == QLatin1String("--script") || a == QLatin1String("--headless") ||
            a.startsWith(QLatin1String("--engine-selftest")) ||
            a.startsWith(QLatin1String("--dump-api-docs")))
            return false;
    }
    return true;
}

/// Ends the session loudly. Called at most once; never returns when it fires.
///
/// TWO FATAL GPU FAULTS, said apart (lane FORK-OOM-1): a LOST DEVICE (exit 3,
/// as since XID-2) and an OUT-OF-MEMORY INSIDE A FRAME (exit 1) -- the device
/// is fine there, but the renderer's frame state is not whole after it, and a
/// next frame crashes; until the evict/retry lane makes that recoverable the
/// session ends the same clean way, with the engine's own honest text
/// ("GPU out of memory (VK_ERROR_OUT_OF_DEVICE_MEMORY; the device is NOT lost
/// - <the pool>) ...") in the log, so the run log and the rig class it as an
/// OOM and never as a loss.
inline void endNow(jahshaka::engine::GpuFault fault, const std::string &engineText)
{
    static bool ending = false;
    if (ending) return;
    ending = true;

    const bool oom = fault == jahshaka::engine::GpuFault::OutOfMemoryInFrame;
    if (oom) {
        JAH_LOG(JahLog::engine, Error,
                QStringLiteral("THE GPU RAN OUT OF MEMORY INSIDE A FRAME \u2014 ending the session "
                               "(the device is NOT lost; a mid-frame out-of-memory is not "
                               "recoverable yet): %1").arg(QString::fromStdString(engineText)));
        std::fprintf(stderr, "FATAL: %s; ending the session.\n", engineText.c_str());
    } else {
        JAH_LOG(JahLog::engine, Error,
                QStringLiteral("THE GRAPHICS DEVICE WAS LOST \u2014 ending the session (the renderer "
                               "does not recreate a lost device; look for an 'NVRM: Xid' line in the "
                               "system log: journalctl -k | grep -i xid)"));
        std::fprintf(stderr, "FATAL: the graphics device was lost; ending the session.\n");
    }
    std::fflush(stderr);

    if (sessionHasAUser()) {
        QMessageBox::critical(
            nullptr, QStringLiteral("Jahshaka"),
            oom ? QStringLiteral("The graphics card ran out of memory.\n\n"
                                 "Jahshaka cannot continue this session \u2014 the renderer cannot "
                                 "yet recover from running out of video memory in the middle of a "
                                 "frame. Your project on disk is unaffected; reopen it after "
                                 "restarting.\n\n"
                                 "Closing other programs that use the GPU usually helps.")
                : QStringLiteral("The graphics device was lost.\n\n"
                                 "Jahshaka cannot continue this session \u2014 the renderer does not "
                                 "recreate a lost device. Your project on disk is unaffected; reopen it "
                                 "after restarting.\n\n"
                                 "If this repeats, the system log usually names the cause "
                                 "(journalctl -k | grep -i xid)."));
    }

    std::fflush(nullptr);
    ::_exit(oom ? 1 : 3);   // NO DESTRUCTORS, deliberately -- see the note at the top.
}

/// The one line every render loop carries after `renderOneFrame()`.
inline void checkAfterFrame(const jahshaka::engine::Engine *engine)
{
    if (!engine) return;
    const jahshaka::engine::GpuFault fault = engine->gpuFault();
    if (fault != jahshaka::engine::GpuFault::None) endNow(fault, engine->lastError());
}

}   // namespace devicelossend

#endif   // DEVICELOSSEND_H
