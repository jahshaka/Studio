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
//
// B ASKS ON AN EVENT, NEVER ON A CLOCK (DBUILD-FIX-1; trap 7). The arm used to
// send B from a 300 ms wall timer and hoped A was still importing then; A's four
// imports end in ~250 ms on this box (each 5-24 ms of import, the rest the verb
// hops; the control's first import is the process's cold one), so A ended first,
// B was never sent, and the arm proved nothing. Now B is sent the moment the app reports A's FIRST import done
// (its `import: '<file>' -> …` record on the app's output) — A's script has
// three imports still to run, so B's request reaches the app while A's run_script
// is dispatching (between hops, or inside the next import's pump). And the order
// is READ, not inferred: each client's reply is stamped when the network says it
// finished, so "B answered after A" is a comparison of two observed events.

#include "../support/mcpharness.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>

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

    // THE ARM: B asks while A's imports pump — on A's first import record.
    const QByteArray importRecord = QByteArrayLiteral("import: '") +
                                    QFileInfo(model).fileName().toUtf8() + QByteArrayLiteral("' -> ");
    const qsizetype armedFrom = (log += jahshaka.readAll()).size();   // the control's records stay behind
    qint64 bSentAt = -1, bAnsweredAt = -1, aRepliedAt = -1, bRepliedAt = -1;
    int importsAtSend = -1;
    bool bSent = false;
    QJsonObject bReply;
    const auto importsSoFar = [&]() { return int(log.mid(armedFrom).count(importRecord)); };
    const QMetaObject::Connection onOutput = QObject::connect(
        &jahshaka, &QProcess::readyReadStandardOutput, &app, [&]() {
            log += jahshaka.readAll();
            if (bSent || importsSoFar() == 0) return;
            bSent = true;   // before the call: B's own reply wait delivers this signal again
            importsAtSend = importsSoFar();
            bSentAt = clock.elapsed();
            bReply = b.runScript(QStringLiteral("project.archiveState()"));
            bAnsweredAt = clock.elapsed();
        });
    const QMetaObject::Connection aDone = QObject::connect(
        &a.net, &QNetworkAccessManager::finished, &app, [&](QNetworkReply *) {
            if (aRepliedAt < 0) aRepliedAt = clock.elapsed(); });
    const QMetaObject::Connection bDone = QObject::connect(
        &b.net, &QNetworkAccessManager::finished, &app, [&](QNetworkReply *) {
            if (bRepliedAt < 0) bRepliedAt = clock.elapsed(); });
    clock.restart();
    const QJsonObject armed = a.runScript(imports);
    const qint64 aMs = clock.elapsed();
    QObject::disconnect(onOutput);
    QObject::disconnect(aDone);
    QObject::disconnect(bDone);
    std::printf("info: armed: A %lld ms (control %lld), A's reply at %lld; B sent at %lld after %d of A's 4 "
                "import records, B's reply at %lld, B's call returned at %lld\n",
                static_cast<long long>(aMs), static_cast<long long>(controlMs),
                static_cast<long long>(aRepliedAt), static_cast<long long>(bSentAt), importsAtSend,
                static_cast<long long>(bRepliedAt), static_cast<long long>(bAnsweredAt));
    CHECK(bSent && importsAtSend >= 1 && importsAtSend < 4 && aRepliedAt >= 0 && bSentAt < aRepliedAt,
          "B's request was sent while A's imports were running (after A's first import, before A's reply)");
    CHECK(armed.value("ok").toBool() && armed.value("result").toInt() == 4,
          "A's imports all completed");
    CHECK(bReply.value("ok").toBool(), "B was answered");
    // THE DEFERRAL ITSELF: B's request waited for A's operations — its reply
    // comes after A's. Answered inside A's run (the pump delivering the MCP
    // queue), B's reply would finish first.
    CHECK(bRepliedAt >= 0 && aRepliedAt >= 0 && bRepliedAt > aRepliedAt,
          "B was answered only after A's run ended (the MCP queue waited)");
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
