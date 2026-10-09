/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// app.device_loss_end — A DEVICE-LOSS END NOBODY ANSWERS STILL ENDS (TESTING-CLEANUP-2 fix round,
// TC2-DEVICE-LOST-HANG). devicelossend::endNow says FATAL and, for a session with a user, shows a
// modal box before it exits. A spawned app on a rig display IS such a session to that test, and
// nobody answers there: measured, the process stayed alive ~1,100 s after its FATAL line, until
// ctest killed it. The box is bounded now: unanswered for 60 s, the process takes the forced exit
// every other forced end takes (services/forcedexit.h: the `shutdown watchdog:` line, code 86).
//
// THE CLAIM, in a child process (this binary with --child, a QApplication offscreen and no
// --script: devicelossend's "a user is in front of it"): endNow(DeviceLost) prints its FATAL line,
// shows the box, nobody answers, and the process exits 86 naming the dialog — inside 120 s.
// RED ON THE BASE (a5ab3a057): the child never exits; the parent kills it at 120 s.
#include "viewport/devicelossend.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>

#include <cstdio>

int main(int argc, char **argv)
{
    if (argc > 1 && QByteArray(argv[1]) == "--child") {
        QApplication app(argc, argv);
        devicelossend::endNow(jahshaka::engine::GpuFault::DeviceLost, std::string());
        std::printf("CHILD: endNow returned\n");
        return 99;
    }
    QCoreApplication app(argc, argv);
    int failures = 0;
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.remove(QStringLiteral("JAHSHAKA_NO_DEVICE_LOSS_DIALOG"));
    child.setProcessEnvironment(env);
    QElapsedTimer t;
    t.start();
    child.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--child") });
    const bool ended = child.waitForFinished(120000);
    const QByteArray out = child.readAll();
    std::printf("---- the child ----\n%s\n-------------------\n", out.constData());
    if (!ended) { child.kill(); child.waitForFinished(5000); }
    const auto check = [&](bool ok, const char *what) {
        std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
        if (!ok) ++failures;
    };
    check(ended, "the unanswered device-loss session ENDS (inside 120 s)");
    check(out.contains("FATAL: the graphics device was lost; ending the session."), "...after its FATAL line");
    check(ended && child.exitStatus() == QProcess::NormalExit && child.exitCode() == forcedexit::kCode,
          "...with the forced exit's code 86");
    check(out.contains("shutdown watchdog: the device-loss dialog was not answered in 60 s"),
          "...and the watchdog's line naming the unanswered dialog");
    std::printf("info: ended after %lld ms, exit code %d\n", static_cast<long long>(t.elapsed()), child.exitCode());
    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
