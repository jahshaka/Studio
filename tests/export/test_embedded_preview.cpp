/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// export.embed — EmbeddedWebPreview contract tests (WEB_EXPORT embed follow-up).
// What is honestly automatable headless: the precondition guards, every
// FAILURE path (dead browser, window never appears) resolving to failed() with
// no crash and no dialog, stop() killing the child, and the viewer<->host
// readiness-marker contract. The successful adoption itself needs a real
// Chrome + GPU and is verified on the rig (see the embed spike report).
// Convention from export.web holds: never spawn a real browser in tests —
// only fake stand-ins (/bin/true, a sleep script).

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

#include "export/embeddedpreview.h"

static int failures = 0;
#define CHECK(cond, what) \
    do { \
        if (cond) std::printf("ok  %s\n", what); \
        else { std::printf("FAIL %s\n", what); ++failures; } \
    } while (0)

// Run the loop until `sig` fires on `obj` or `ms` elapses; true when it fired.
template <typename Sender, typename Signal>
static bool waitForSignal(Sender *obj, Signal sig, int ms)
{
    QEventLoop loop;
    bool fired = false;
    QObject::connect(obj, sig, &loop, [&] { fired = true; loop.quit(); });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return fired;
}

int main(int argc, char **argv)
{
    // xcb on purpose: platformSupported() and the failure paths must be
    // exercised on the platform the feature actually targets.
    qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    const QString index = tmp.filePath("index.html");
    {
        QFile f(index);
        if (!f.open(QIODevice::WriteOnly)) { std::printf("FAIL temp index\n"); return 1; }
        f.write("<!doctype html><title>t</title>");
    }
    QWidget slot_;
    auto *lay = new QVBoxLayout(&slot_);
    Q_UNUSED(lay);

    // ---- platform ----
    CHECK(EmbeddedWebPreview::platformSupported(), "platformSupported on xcb");

    // ---- precondition guards: start() refuses without side effects ----
    {
        EmbeddedWebPreview p;
        CHECK(!p.start(QStringLiteral("/bin/true"), tmp.filePath("missing.html"), &slot_),
              "start refused: index.html missing");
        CHECK(!p.start(QString(), index, &slot_), "start refused: empty browser path");
        QWidget bare; // no layout
        CHECK(!p.start(QStringLiteral("/bin/true"), index, &bare),
              "start refused: slot without layout");
        CHECK(!p.start(QStringLiteral("/bin/true"), index, nullptr),
              "start refused: null slot");
    }

    // ---- failure path: browser exits immediately -> failed(), no crash ----
    {
        EmbeddedWebPreview p;
        QString reason;
        QObject::connect(&p, &EmbeddedWebPreview::failed,
                         [&reason](const QString &r) { reason = r; });
        CHECK(p.start(QStringLiteral("/bin/true"), index, &slot_),
              "start accepted a fake browser");
        CHECK(waitForSignal(&p, &EmbeddedWebPreview::failed, 5000),
              "dead browser -> failed() promptly");
        CHECK(!reason.isEmpty(), "failed() carries a reason");
        CHECK(!p.isEmbedded(), "never reported embedded");
        p.stop();
        p.stop(); // idempotent
        CHECK(true, "stop() is idempotent after failure");
    }

    // ---- failure path: browser alive but no window -> find timeout ----
    {
        const QString fake = tmp.filePath("fakebrowser.sh");
        {
            QFile f(fake);
            if (!f.open(QIODevice::WriteOnly)) { std::printf("FAIL temp script\n"); return 1; }
            f.write("#!/bin/bash\nsleep 30\n");
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        }
        EmbeddedWebPreview p;
        p.findTimeoutMs = 1200; // keep the suite fast
        QString reason;
        QObject::connect(&p, &EmbeddedWebPreview::failed,
                         [&reason](const QString &r) { reason = r; });
        CHECK(p.start(fake, index, &slot_), "start accepted the sleeping browser");
        CHECK(waitForSignal(&p, &EmbeddedWebPreview::failed, 6000),
              "no window within findTimeoutMs -> failed()");
        CHECK(reason.contains(QStringLiteral("not found")), "reason names the find timeout");
        CHECK(p.process() && p.process()->state() == QProcess::Running,
              "failed() leaves the child for the caller to stop");
        p.stop();
        CHECK(p.process()->state() == QProcess::NotRunning, "stop() kills the child");
    }

    // ---- viewer <-> host readiness-marker contract (drift guard) ----
    {
        QFile viewer(QStringLiteral(":/export/viewer.js"));
        CHECK(viewer.open(QIODevice::ReadOnly), "viewer.js resource opens");
        const QByteArray js = viewer.readAll();
        CHECK(js.contains("[jah-gpu-ready]"), "viewer emits the gpu-ready marker");
        CHECK(js.contains("[jah-no-gpu]"), "viewer emits the no-gpu marker");
        CHECK(js.contains("jahembed=1"), "markers gated on the jahembed query");
    }

    std::printf(failures ? "FAILED: %d checks\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}
