// app.update_check — the launch-time update check is decoration, and it must
// behave like decoration (UPDATE-1).
//
// main() runs UpdateChecker::checkForAppUpdate() on every windowed launch,
// once, right after the window is up. Two things about that used to be wrong
// and this suite holds them:
//
//   1. THE REQUEST CARRIED NO TRANSFER TIMEOUT — and a plain
//      QNetworkAccessManager applies NONE by itself (measured: 0; a GET
//      against a never-answering socket never finishes), so a launch behind a
//      black-holing route kept a socket, a reply and the manager's state alive
//      for the life of the process. It sets its own now
//      (UpdateChecker::kTransferTimeoutMs).
//   2. THE REPLY WAS NEVER DELETED — one orphan per launch, owned by the
//      manager until the process exited, on every path including the failures.
//
// The fixture is a local QTcpServer, so nothing here touches the network: one
// socket that accepts and never answers (the black hole), and one that answers
// with the real JSON body. No display, no engine, no app.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>

#include "app/updatechecker.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// Runs the event loop until `done` or `limitMs` elapses.
static void pump(const bool &done, int limitMs)
{
    QElapsedTimer t; t.start();
    while (!done && t.elapsed() < limitMs)
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- 1. THE BLACK HOLE: accepted, never answered ----------------------
    QTcpServer silent;
    CHECK(silent.listen(QHostAddress::LocalHost), "the silent server is listening");
    QList<QTcpSocket *> held;
    QObject::connect(&silent, &QTcpServer::newConnection, [&]() {
        // Hold the connection open and say nothing. This is what a black-holed
        // route looks like to the client: connected, then silence.
        held.append(silent.nextPendingConnection());
    });

    UpdateChecker checker;
    bool failed = false, updateOffered = false;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QString reason;
    QObject::connect(&checker, &UpdateChecker::checkFailed,
                     [&](QNetworkReply::NetworkError e, QString r) {
                         failed = true; error = e; reason = r;
                     });
    QObject::connect(&checker, &UpdateChecker::updateNeeded,
                     [&](QString, QString, QString) { updateOffered = true; });

    // A 1 s budget instead of the shipped 15 s: the mechanism under test is
    // "the check ends on ITS OWN clock", and a suite should not spend fifteen
    // seconds proving it. The shipped default is asserted separately below.
    const QUrl silentUrl(QStringLiteral("http://127.0.0.1:%1/update/").arg(silent.serverPort()));
    QElapsedTimer timer; timer.start();
    checker.checkForUpdate(silentUrl, 1000);
    const qint64 returnedAfterMs = timer.elapsed();
    std::printf("info: checkForUpdate() returned in %lld ms\n",
                static_cast<long long>(returnedAfterMs));
    CHECK(returnedAfterMs < 500,
          "the verb RETURNED immediately — the check never waits on the calling thread");
    CHECK(checker.checkInFlight(), "a check is in flight");

    // 10 s of headroom: if the timeout did not fire, the reply would sit here
    // for Qt's 30 s default and this would red on the elapsed assertion below.
    pump(failed, 10000);
    const qint64 endedAfterMs = timer.elapsed();
    std::printf("info: the check ended after %lld ms with error %d: %s\n",
                static_cast<long long>(endedAfterMs), int(error), reason.toUtf8().constData());
    CHECK(failed, "the finished handler ran on a check that was never answered");
    // TimeoutError (4), not OperationCanceledError (5): Qt's transfer timeout
    // is not a cancel, and code that triages a dead check on the error would
    // get it wrong the other way round (CLAUDE.md, HARNESS-1 2026-09-15).
    CHECK(error == QNetworkReply::TimeoutError,
          "... and reports QNetworkReply::TimeoutError");
    CHECK(endedAfterMs > 500 && endedAfterMs < 5000,
          "... on the timeout THIS CHECK set, not Qt's 30 s default");
    CHECK(!updateOffered, "no update was offered, so no dialog can be raised");

    // The reply is deleteLater()'d, so it dies one event-loop turn later.
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    CHECK(!checker.checkInFlight(),
          "the reply was destroyed — a failed check leaves no orphan behind");

    qDeleteAll(held);
    held.clear();

    // ---- 2. THE ANSWER: a real body still raises the update ---------------
    // The timeout must not have broken the path that matters.
    QTcpServer answering;
    CHECK(answering.listen(QHostAddress::LocalHost), "the answering server is listening");
    QObject::connect(&answering, &QTcpServer::newConnection, [&]() {
        QTcpSocket *s = answering.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, [s]() {
            s->readAll();
            const QByteArray body =
                "{\"should_update\":true,\"id\":\"9.9.9\",\"notes\":\"the notes\","
                "\"linux_url\":\"http://example.invalid/linux\","
                "\"windows_url\":\"http://example.invalid/windows\","
                "\"mac_url\":\"http://example.invalid/mac\"}";
            s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                     + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            s->flush();
            s->disconnectFromHost();
        });
    });

    failed = false; updateOffered = false;
    QString gotVersion, gotNotes, gotLink;
    QObject::connect(&checker, &UpdateChecker::updateNeeded,
                     [&](QString v, QString n, QString l) { gotVersion = v; gotNotes = n; gotLink = l; });
    checker.checkForUpdate(
        QUrl(QStringLiteral("http://127.0.0.1:%1/update/").arg(answering.serverPort())), 10000);
    pump(updateOffered, 10000);
    CHECK(updateOffered, "an answering endpoint still offers the update");
    CHECK(!failed, "... and reports no failure");
    CHECK(gotVersion == QLatin1String("9.9.9"), "... with the version from the body");
    CHECK(gotNotes == QLatin1String("the notes"), "... and the notes");
    CHECK(gotLink.contains(QLatin1String("example.invalid")),
          "... and this platform's download link");
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    CHECK(!checker.checkInFlight(),
          "the answered reply was destroyed too — no orphan on the happy path either");

    // ---- 3. THE SHIPPED DEFAULT -------------------------------------------
    // The number the launch path actually uses. Asserted as a range, because
    // the point is the ORDER: a small JSON fetch, seconds not minutes (Qt
    // itself imposes nothing; 30 s is only setTransferTimeout()'s no-argument
    // value, kept here as the ceiling a launch check must never reach).
    std::printf("info: the shipped transfer timeout is %d ms\n", UpdateChecker::kTransferTimeoutMs);
    CHECK(UpdateChecker::kTransferTimeoutMs > 1000 && UpdateChecker::kTransferTimeoutMs < 30000,
          "the shipped transfer timeout is this check's own, seconds not minutes");

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
