/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// scripting.e2e.pump_defer — A UI-THREAD OPERATION'S PUMP DELIVERS NO OTHER WORK
// (STUDIO-D1 fix round, items 3-4; services/uithreadwait.h).
//
// A synchronous import verb waits for its parse + bake on a worker while the UI
// thread pumps. Before the rule, an MCP request arriving in that pump ran INSIDE
// it: a second run_script reached McpTools::waitForScriptIdle, which waits for
// the first script to end — and the first script's verb could not return until
// that nested wait gave up at its ceiling (the outer verb stalled for the whole
// script timeout). Now the pump defers the MCP queue (and the script bridge's
// verb hops) until the operation ends.
//
// The arm: client A runs a script of several model imports; while it runs,
// client B sends its own run_script. B is answered only after A's operations
// end, and NEITHER waits for the nested ceiling: A's wall stays a small multiple
// of its imports' own cost, measured against a control run of the same script
// alone, never the 30 s the nested wait would add.

#include "../support/mcpharness.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

using namespace mcpharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));
    const QString model = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/skeletal/fixtures/mixamo_tpose.fbx");
    CHECK(QFile::exists(model), "the model fixture is present");

    QProcess jahshaka;
    QString token;
    QByteArray log;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token, &log), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient a, b;
    a.clientName = QStringLiteral("pump-defer-a");
    b.clientName = QStringLiteral("pump-defer-b");
    for (McpClient *c : { &a, &b }) {
        c->url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
        c->token = token;
        c->attach(jahshaka, log);
        c->initialize();
    }
    CHECK(a.runScript(QStringLiteral("project.create('PumpDefer').length > 10")).value("ok").toBool(),
          "a project to import into");
    const QString imports = QStringLiteral(
        "(function () { var n = 0;"
        "  for (var i = 0; i < 4; i++) if (assets.importFile('%1')) ++n;"
        "  return n; })()").arg(model);

    // THE CONTROL: the same imports with nobody else asking.
    QElapsedTimer clock;
    clock.start();
    const QJsonObject control = a.runScript(imports);
    const qint64 controlMs = clock.elapsed();
    std::printf("info: control: %lld ms, result %s\n", static_cast<long long>(controlMs),
                qUtf8Printable(control.value("result").toVariant().toString()));
    CHECK(control.value("ok").toBool() && control.value("result").toInt() == 4,
          "the control run imported all four");

    // THE ARM: B asks while A's imports pump.
    qint64 bSentAt = -1, bAnsweredAt = -1;
    QJsonObject bReply;
    QTimer::singleShot(300, [&]() {
        bSentAt = clock.elapsed();
        bReply = b.runScript(QStringLiteral("project.archiveState()"));
        bAnsweredAt = clock.elapsed();
    });
    clock.restart();
    const QJsonObject armed = a.runScript(imports);
    const qint64 aMs = clock.elapsed();
    // B's request runs inside A's reply wait (the harness's nested loop); if it
    // has not fired yet the arm would prove nothing.
    std::printf("info: armed: A %lld ms (control %lld), B sent at %lld, answered at %lld\n",
                static_cast<long long>(aMs), static_cast<long long>(controlMs),
                static_cast<long long>(bSentAt), static_cast<long long>(bAnsweredAt));
    CHECK(bSentAt >= 0 && bSentAt < aMs, "B's request was sent while A's imports were running");
    CHECK(armed.value("ok").toBool() && armed.value("result").toInt() == 4,
          "A's imports all completed");
    CHECK(bReply.value("ok").toBool(), "B was answered");
    // The nested wait's ceiling is McpTools' script timeout (30 s): A must not
    // pay it. 3x the control + 10 s covers a loaded box without admitting it.
    CHECK(aMs < 3 * controlMs + 10000,
          "A was not held by a nested wait inside its pump (no 30 s ceiling)");

    a.quit();
    if (!jahshaka.waitForFinished(30000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    std::printf(failures == 0 ? "scripting.e2e.pump_defer: ALL PASS\n"
                              : "scripting.e2e.pump_defer: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
