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
// THE CLAIMS, in child processes (this binary with --child, a QApplication offscreen and no
// --script: devicelossend's "a user is in front of it"):
//   RIG — with JAHSHAKA_NO_DEVICE_LOSS_DIALOG=1 (what the harness's spawn() and run_pool.py set):
//         no box, exit 3 at once, no watchdog line;
//   USER — endNow(DeviceLost) prints its FATAL line, shows the box, nobody answers, and the process
//         exits 86 naming the dialog — inside 120 s.
// RED WITHOUT THE BOUND (measured 2026-10-10: the arm compiled out): the USER child never exits;
// the parent kills it at 120 s.
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
    const auto check = [&](bool ok, const char *what) {
        std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
        if (!ok) ++failures;
    };
    // One child per path: the RIG (JAHSHAKA_NO_DEVICE_LOSS_DIALOG=1, what tests/support's spawn()
    // and run_pool.py set) and the USER (no variable: the box, bounded at 60 s).
    const auto runChild = [&](bool rig, QByteArray &out, QProcess::ExitStatus &status, int &code, qint64 &ms) {
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        if (rig) env.insert(QStringLiteral("JAHSHAKA_NO_DEVICE_LOSS_DIALOG"), QStringLiteral("1"));
        else env.remove(QStringLiteral("JAHSHAKA_NO_DEVICE_LOSS_DIALOG"));
        child.setProcessEnvironment(env);
        QElapsedTimer t;
        t.start();
        child.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--child") });
        const bool ended = child.waitForFinished(120000);
        out = child.readAll();
        ms = t.elapsed();
        if (!ended) { child.kill(); child.waitForFinished(5000); }
        status = child.exitStatus();
        code = ended ? child.exitCode() : -1;
        std::printf("---- the %s child (%lld ms, exit %d) ----\n%s\n-------------------\n", rig ? "RIG" : "USER",
                    static_cast<long long>(ms), code, out.constData());
        return ended;
    };
    QByteArray out;
    QProcess::ExitStatus status;
    int code = 0;
    qint64 ms = 0;
    // 1. THE RIG: no box, the device-loss code (3) at once.
    bool ended = runChild(true, out, status, code, ms);
    check(ended && ms < 30000, "RIG: a harness's app with a lost device ends AT ONCE (no dialog wait)");
    check(out.contains("FATAL: the graphics device was lost; ending the session."), "RIG: ...after its FATAL line");
    check(ended && status == QProcess::NormalExit && code == 3, "RIG: ...with the device-loss code 3");
    check(!out.contains("shutdown watchdog:"), "RIG: ...and no watchdog fired");
    // 2. THE USER: the box shown, nobody answers, the 60 s bound ends it with the forced exit (86).
    ended = runChild(false, out, status, code, ms);
    check(ended, "USER: the unanswered device-loss session ENDS (inside 120 s)");
    check(out.contains("FATAL: the graphics device was lost; ending the session."), "USER: ...after its FATAL line");
    check(ended && status == QProcess::NormalExit && code == forcedexit::kCode, "USER: ...with the forced exit's code 86");
    check(out.contains("shutdown watchdog: the device-loss dialog was not answered in 60 s"),
          "USER: ...and the watchdog's line naming the unanswered dialog");
    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
