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

#include <QApplication>
#include <QFile>
#include <QThread>

#include "bridge/enginehost.h"
#include "scripting/scriptengine.h"
#include "shell/mainwindow.h"

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
    EngineHost::instance().shutdown();
    return rc;
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
