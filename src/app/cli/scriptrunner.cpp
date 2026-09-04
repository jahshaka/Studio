/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/cli/scriptrunner.h"

#include <cstdio>
#include <cstdlib>

#include <QApplication>
#include <QFile>
#include <QThread>
#include <QThreadPool>

#include "bridge/enginehost.h"
#include "scripting/scriptengine.h"
#include "scripting/mcp/mcpserver.h"
#include "shell/mainwindow.h"
#include "services/mainthreadwatchdog.h"
#include "shell/shutdownorder.h"

int finalizeAppExit(int rc)
{
    // STEP 3 of the shutdown order. The whole sequence is documented in one
    // place — at ~MainWindow (src/shell/mainwindow.cpp), enumerated in
    // src/shell/shutdownorder.h.
    //
    // NOTE WHAT THIS DOES NOT DO: EngineHost::shutdown() stops the render
    // driver, writes the shader cache and the warm-up set, and drops the
    // HOST's shared_ptr — it does NOT destroy the Engine, because the
    // viewport widgets hold their own copies. The Engine dies at step 5,
    // inside ~MainWindow, deliberately before the database closes.
    JAH_SHUTDOWN_STEP(ShutdownOrder::EngineHostRelease,
                      "finalizeAppExit: EngineHost::shutdown (driver + host ref)");

    // The CLI paths (--script, --dump-api-docs, --engine-selftest) never close
    // the window, so they never reach step 2 — and step 2 is where the
    // main-thread watchdog normally stops. Stopping it again here is a no-op
    // for the window-close path and the only stop the CLI paths get.
    MainThreadWatchdog::stop();

    // The engine borrows Qt's X display: release it before QApplication goes away.
    EngineHost::instance().shutdown();
    if (!QThreadPool::globalInstance()->waitForDone(5000)) {
        qWarning("shutdown: background workers still running 5s after exit — "
                 "forcing process exit (code %d)", rc);
        std::fflush(nullptr);
        std::_Exit(rc);
    }
    return rc;
}

int runScriptFile(MainWindow &window, QApplication &app, const QString &path, bool headless)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "script: cannot open %s\n", qPrintable(path));
        return 1;
    }
    const QString source = QString::fromUtf8(file.readAll());

    window.show();
    app.processEvents();

    if (!headless) {
        QString why;
        if (!window.beginEngineSelftest(why)) {
            std::fprintf(stderr, "script: %s\n", qPrintable(why));
            return 1;
        }
        // Let the engine settle (swapchain, first frames) like the selftest does.
        for (int frame = 0; frame < 10; ++frame) {
            app.processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(16);
        }
    }

    ScriptEngine *engine = window.scripting();
    QObject::connect(engine, &ScriptEngine::consoleOutput, [](const QString &t) {
        std::fprintf(stdout, "%s\n", qPrintable(t));
        std::fflush(stdout);
    });

    const ScriptResult result = engine->evaluate(source, path);

    int rc = 0;
    if (!result.ok) {
        std::fprintf(stderr, "%s\n", qPrintable(result.toString()));
        if (!result.stack.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(result.stack));
        rc = 1;
    } else {
        const int typeId = result.value.typeId();
        if (typeId == QMetaType::Int || typeId == QMetaType::Double || typeId == QMetaType::LongLong)
            rc = qBound(0, result.value.toInt(), 255);
    }

    if (!headless) window.endEngineSelftest();
    return finalizeAppExit(rc);
}

int runMcpServe(MainWindow &window, QApplication &app, unsigned short port, bool headless)
{
    window.show();
    app.processEvents();

    if (!headless) {
        // Same boot as a windowed script run: editor page shown, engine view
        // live, a default scene up — screenshot works immediately, and
        // run_script's project.create() switches to a real project.
        QString why;
        if (!window.beginEngineSelftest(why)) {
            std::fprintf(stderr, "mcp: %s\n", qPrintable(why));
            return 1;
        }
        for (int frame = 0; frame < 10; ++frame) {
            app.processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(16);
        }
    }

    QString error;
    if (!window.startMcpServer(port, &error)) {
        std::fprintf(stderr, "mcp: %s\n", qPrintable(error));
        return 1;
    }

    McpServer *mcp = window.mcp();
    std::printf("MCP: listening on http://127.0.0.1:%u/mcp\n", unsigned(mcp->port()));
    std::printf("MCP: token %s\n", qPrintable(mcp->token()));
    std::printf("MCP: connect with: %s\n", qPrintable(mcp->connectCommand()));
    std::fflush(stdout);

    const int rc = app.exec();
    return finalizeAppExit(rc);
}

int runDumpApiDocs(MainWindow &window, const QString &outPath)
{
    QFile docs(outPath);
    if (!docs.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        std::fprintf(stderr, "dump-api-docs: cannot write %s\n", qPrintable(outPath));
        return 1;
    }
    docs.write(window.scripting()->registry().markdown().toUtf8());
    std::fprintf(stderr, "dump-api-docs: wrote %s\n", qPrintable(outPath));
    return 0;
}
