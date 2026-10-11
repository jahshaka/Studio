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
// TWO ARMS (DBUILD-FIX-1 fix round 2: the one-reader read's F1).
//
// 1. THE DEFERRAL ITSELF, in this process. Two MCP clients cannot prove it: A's
//    run_script holds McpServer::mBusy for its whole dispatch, so B's request
//    waits on mBusy whatever UiThreadWait does — delete the inOperation() block
//    in McpServer::drainQueue and that arm stays green. The rule protects the
//    OTHER doors (an import a menu, a drop, the console or a seed starts on the
//    UI thread, with no MCP dispatch running), and none of them can be driven
//    from outside the app. So this arm IS that door: the REAL McpServer
//    (mcpserver.cpp, linked; its tools stubbed below — the request is a `ping`,
//    which never reaches them) listens here, this thread runs a REAL
//    UiThreadWait::run operation whose worker waits for this thread's word, and
//    an MCP request is posted before the operation starts — so its bytes are
//    written and read INSIDE the operation's pump (the client and the server
//    share this thread's event loop, which only the pump turns). Between pumps
//    the operation's tick reads what happened — the deferred work queued
//    (UiThreadWait::deferredWork(), the app-side record), or the reply already
//    finished — and only then releases the worker. Counted, never timed: the
//    release is an event. The claim: the request was DEFERRED, not answered
//    inside the pump, and answered after the operation ended.
// 2. THE APP, end to end: client A runs a script of several model imports;
//    while it runs, client B sends its own run_script. B is answered after A
//    (the server's FIFO queue — one request at a time), and A is never held by a
//    nested wait: A's wall stays a small multiple of its imports' own cost,
//    measured against a control run of the same script alone, never the 30 s a
//    nested waitForScriptIdle would add.
//
// B ASKS ON AN EVENT, NEVER ON A CLOCK (DBUILD-FIX-1; trap 7). Arm 2 used to
// send B from a 300 ms wall timer and hoped A was still importing then; A's four
// imports end in ~250 ms on this box (each 5-24 ms of import, the rest the verb
// hops; the control's first import is the process's cold one), so A ended first,
// B was never sent, and the arm proved nothing. Now B is sent the moment the app
// reports A's FIRST import done (its `import: '<file>' -> …` record on the app's
// output; the control's four records come first, so the trigger is the FIFTH
// record of the whole log — never a cursor a record still in the pipe could
// slip past). A's script has three imports still to run. And the order is
// READ, not inferred: each client's reply is stamped when the network says it
// finished, so "B answered after A" is a comparison of two observed events.

#include "../support/mcpharness.h"

#include "scripting/mcp/mcpserver.h"
#include "scripting/mcp/mcptools.h"
#include "services/uithreadwait.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <atomic>

using namespace mcpharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// THE SERVER'S TOOLS, STUBBED (arm 1 sends `ping`, which McpServer::dispatch
// answers itself; a tools/* request here would be a test defect, and says so).
McpTools::McpTools(ScriptEngine *engine) : mEngine(engine) {}
QJsonArray McpTools::listTools() const { return {}; }
QJsonObject McpTools::call(const QString &name, const QJsonObject &)
{
    return QJsonObject{ { "isError", true }, { "error", QStringLiteral("stub tool %1").arg(name) } };
}

// ARM 1 (see the header): an MCP request that lands inside a UI-thread
// operation's pump, with no MCP dispatch running, waits for the operation.
static void deferralArm()
{
    McpServer server(nullptr);
    QString why;
    const bool listening = server.start(0, &why);
    CHECK(listening, qPrintable(listening ? QStringLiteral("arm 1: the real McpServer listens on 127.0.0.1:%1").arg(server.port())
                                          : QStringLiteral("arm 1: the real McpServer listens (%1)").arg(why)));
    if (!server.isRunning()) return;

    QNetworkAccessManager net;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(server.port())));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + server.token().toUtf8());
    // POSTED BEFORE THE OPERATION: nothing turns this thread's event loop until
    // the pump does, so the request is written, read and handed to the server
    // INSIDE the operation.
    QNetworkReply *reply = net.post(request, QJsonDocument(QJsonObject{
        { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "ping" } }).toJson(QJsonDocument::Compact));

    std::atomic<bool> release { false };
    bool deferredSeen = false, answeredInside = false, inOperationSeen = false;
    long ticks = 0;
    const long kTickCeiling = 2000000;   // a COUNT: a server that neither defers nor answers ends the arm red
    UiThreadWait::run([&release]() {
        while (!release.load()) QThread::yieldCurrentThread();
    }, UiThreadWait::Pump::Events, [&]() {
        if (release.load()) return;
        ++ticks;
        inOperationSeen = inOperationSeen || UiThreadWait::inOperation();
        if (!UiThreadWait::deferredWork().empty()) deferredSeen = true;
        if (reply->isFinished()) answeredInside = true;
        if (deferredSeen || answeredInside || ticks >= kTickCeiling) release.store(true);
    });
    const bool finishedAtEnd = reply->isFinished();
    // The operation ended: the deferred drain is POSTED now (uithreadwait.h).
    if (!reply->isFinished()) {
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
    std::printf("info: arm 1: %ld tick(s) in the operation; deferred work queued: %s; answered inside the "
                "pump: %s; answer: %s\n", ticks, deferredSeen ? "yes" : "no", answeredInside ? "YES" : "no",
                QJsonDocument(answer).toJson(QJsonDocument::Compact).constData());
    CHECK(inOperationSeen, "arm 1: the tick ran inside the UI-thread operation");
    CHECK(deferredSeen && !answeredInside && !finishedAtEnd,
          "arm 1: the MCP request that arrived in the operation's pump was DEFERRED, not answered inside it");
    CHECK(answer.value("id").toInt() == 1 && answer.contains("result"),
          "arm 1: the request was answered (the ping's result)");
    reply->deleteLater();
    server.stop();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));
    deferralArm();
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
    const int controlRecords = 4;   // the control's, counted in the same log (never a cursor)
    qint64 bSentAt = -1, bAnsweredAt = -1, aRepliedAt = -1, bRepliedAt = -1;
    int importsAtSend = -1;
    bool bSent = false;
    QJsonObject bReply;
    const auto importsSoFar = [&]() { return int(log.count(importRecord)) - controlRecords; };
    const QMetaObject::Connection onOutput = QObject::connect(
        &jahshaka, &QProcess::readyReadStandardOutput, &app, [&]() {
            log += jahshaka.readAll();
            if (bSent || importsSoFar() <= 0) return;
            // Before the call: B's reply wait turns the event loop, and the
            // first-record test must not send a second B from a later signal.
            bSent = true;
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
    // THE QUEUE: one request at a time (McpServer::mBusy) — B's reply comes
    // after A's. The deferral itself is arm 1's: here mBusy alone orders them.
    CHECK(bRepliedAt >= 0 && aRepliedAt >= 0 && bRepliedAt > aRepliedAt,
          "B was answered only after A's run ended (the server's queue: one request at a time)");
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
