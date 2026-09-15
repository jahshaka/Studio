// The spawn-the-real-binary-over-MCP harness EVERY app-spawning suite shares
// (the import.shutdown / open.responsive pattern). It lived in tests/shutdown/
// while two suites needed it; twelve do now, and four of them carried a
// verbatim COPY of the client — which is how a transport defect could hide in
// one and not the others. One copy, here, beside the other shared test
// headers.
#ifndef TESTS_SUPPORT_MCPHARNESS_H
#define TESTS_SUPPORT_MCPHARNESS_H

#include "seedsettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <cstdio>

namespace mcpharness {

/// THE PER-REQUEST BUDGET — spelled out because the default is wrong for us.
///
/// Qt >= 6.7 gives every QNetworkAccessManager request a DEFAULT transfer
/// timeout of 30 s. Nothing in these suites asked for it, nothing printed it,
/// and when a request outlived it the reply came back EMPTY with the manager's
/// error set — which this harness used to throw away, returning `{}`. Every
/// caller then read `.value("ok")` off an empty object and saw `false`: a
/// script failure with NO message, indistinguishable from the app refusing the
/// verb. It cost `ui.window_minimum` a red on 2026-09-15 (ledger 404), where
/// `app.space('player')` — bringing up a second View under four Vulkan
/// instances — simply took longer than 30 s.
///
/// 90 s instead, for two measured reasons. It is far above any legitimate
/// single request in this tree: the longest one-shot verbs are a synchronous
/// sample open (12.5 s on Matcaps before the threaded path landed), an avatar
/// import, and a space switch that boots a second View — tens of seconds at
/// worst, and this box has never been slow enough to quadruple that. And it is
/// below the SMALLEST ctest TIMEOUT among the suites sharing this header
/// (app.shutdown_order at 120 s), so a request that really is never answered
/// dies with a message the log carries instead of the whole suite being killed
/// by ctest with nothing to read.
///
/// A suite whose own budget is larger and whose verbs legitimately run longer
/// raises `McpClient::transferTimeoutMs` (avatar.responsive, import.shutdown
/// and perf.capture_bundle do). It is never lowered blindly — the one place
/// that lowers it is the case in app.shutdown_order that PROVES the timeout
/// reports itself.
static const int kTransferTimeoutMs = 90000;

/// The margin between the APP's ceiling on one run_script call (MCP's
/// `timeoutMs` argument, 30 s by default — the same 30 s Qt used to cancel at)
/// and this client's. They are two different clocks and the app's must expire
/// FIRST, so a call that really does run too long comes back as the app saying
/// "timed out" with a line number instead of the client abandoning it with
/// nothing. Raising the transfer budget without raising the script budget
/// would have left ledger 404's `app.space('player')` dying at 30 s anyway.
static const int kScriptTimeoutMarginMs = 10000;

/// One log line out of a script that may be long or multi-line: the message
/// has to name the verb that died, not a page of JavaScript.
inline QString oneLine(const QString &script)
{
    QString s = script.simplified();
    if (s.size() > 160) s = s.left(157) + QStringLiteral("...");
    return s;
}

struct McpClient
{
    QNetworkAccessManager net;
    QUrl url;
    QString token;
    QString clientName = QStringLiteral("jahshaka-test");
    int id = 0;

    /// Per-request budget (see kTransferTimeoutMs). Set it before the first
    /// post; it is applied to every request.
    int transferTimeoutMs = kTransferTimeoutMs;

    /// The app-side budget for one run_script call, sent as the tool's
    /// `timeoutMs`. 0 means "transferTimeoutMs minus the margin", which is
    /// what every suite wants; a case that deliberately shortens the transport
    /// budget sets this by hand so the APP keeps working while the client
    /// gives up (tests/shutdown/test_shutdown_order.cpp does exactly that).
    int scriptTimeoutMs = 0;

    int effectiveScriptTimeoutMs() const
    {
        if (scriptTimeoutMs > 0) return scriptTimeoutMs;
        return qBound(1000, transferTimeoutMs - kScriptTimeoutMarginMs, 600000);
    }

    /// What the transport did to the last request that failed, and how many
    /// have failed on this client. A suite may assert on either; both are
    /// printed as they happen, so a log alone is enough to tell a dead
    /// request from a refused verb.
    QString lastTransportError;
    int transportFailures = 0;

    /// ReplyOptional is for the ONE request whose answer may legitimately
    /// never arrive: `app.quit()` races the queued window close, so the app is
    /// allowed to go away mid-response. Everything else is ReplyRequired.
    enum ReplyPolicy { ReplyRequired, ReplyOptional };

    QJsonObject post(const QJsonObject &body, const QString &what = QString(),
                     ReplyPolicy policy = ReplyRequired)
    {
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        request.setTransferTimeout(transferTimeoutMs);
        QElapsedTimer timer;
        timer.start();
        QNetworkReply *reply = net.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const qint64 elapsedMs = timer.elapsed();
        // The transfer timeout ends the reply with TimeoutError ("Operation
        // timed out") — measured on Qt 6.10, not the OperationCanceledError the
        // documentation suggests; both are accepted here, and since this
        // harness never calls abort() itself the code IS the verdict.
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorText = reply->errorString();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();

        // A JSON-RPC NOTIFICATION is accepted with no body (mcpserver.cpp:162
        // answers 202): an empty reply is the correct answer there and the only
        // place it is.
        if (error == QNetworkReply::NoError && data.isEmpty() &&
            (status == 202 || status == 204))
            return {};

        QString problem;
        if (error == QNetworkReply::TimeoutError ||
            error == QNetworkReply::OperationCanceledError)
            problem = QStringLiteral("no reply within %1 s (transfer timeout; %2 ms elapsed)")
                          .arg(transferTimeoutMs / 1000.0, 0, 'g', 3)
                          .arg(elapsedMs);
        else if (error != QNetworkReply::NoError)
            problem = QStringLiteral("transport error after %1 ms (HTTP %2): %3")
                          .arg(elapsedMs).arg(status).arg(errorText);
        else if (data.isEmpty())
            problem = QStringLiteral("empty reply (HTTP %1) after %2 ms").arg(status).arg(elapsedMs);
        if (problem.isEmpty()) return QJsonDocument::fromJson(data).object();

        const QString label = what.isEmpty() ? body.value("method").toString() : what;
        lastTransportError = QStringLiteral("%1 — %2").arg(problem, label);
        // An ALLOWED no-reply is not a failure and is not counted as one; it is
        // still printed, because "the app went away before it answered" is a
        // fact a log reader wants.
        if (policy == ReplyOptional) {
            std::printf("info: no reply (allowed): %s\n", qUtf8Printable(lastTransportError));
            std::fflush(stdout);
            return {};
        }
        ++transportFailures;
        std::printf("FAIL(transport): %s\n", qUtf8Printable(lastTransportError));
        std::fflush(stdout);
        // NOT an empty object: a caller reading .value("ok") still fails, and
        // now the object it read says why.
        return QJsonObject{ { "ok", false },
                            { "error", lastTransportError },
                            { "transport", problem } };
    }

    void initialize()
    {
        post(QJsonObject{ { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "initialize" },
                          { "params", QJsonObject{
                                { "protocolVersion", "2025-06-18" },
                                { "capabilities", QJsonObject{} },
                                { "clientInfo", QJsonObject{ { "name", clientName }, { "version", "0" } } } } } },
             QStringLiteral("initialize"));
        post(QJsonObject{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } },
             QStringLiteral("notifications/initialized"));
    }

    /// Runs a script through the run_script tool; returns the parsed
    /// {ok, result, ...} payload. On a transport failure or a reply with no
    /// content the payload is {ok:false, error:"...", transport:"..."} —
    /// never a blank object.
    QJsonObject runScript(const QString &script, ReplyPolicy policy = ReplyRequired)
    {
        const QString what = QStringLiteral("run_script: %1").arg(oneLine(script));
        const QJsonObject reply = post(QJsonObject{
            { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "tools/call" },
            { "params", QJsonObject{ { "name", "run_script" },
                                     { "arguments", QJsonObject{
                                           { "script", script },
                                           { "timeoutMs", effectiveScriptTimeoutMs() } } } } } },
            what, policy);
        if (reply.contains(QStringLiteral("transport"))) return reply;   // already named
        if (reply.isEmpty()) return {};                                  // ReplyOptional, no answer
        const QJsonArray content = reply.value("result").toObject().value("content").toArray();
        if (content.isEmpty()) {
            const QString problem = QStringLiteral("the MCP reply carried no content: %1")
                .arg(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
            lastTransportError = QStringLiteral("%1 — %2").arg(problem, what);
            ++transportFailures;
            std::printf("FAIL(transport): %s\n", qUtf8Printable(lastTransportError));
            std::fflush(stdout);
            return QJsonObject{ { "ok", false },
                                { "error", lastTransportError },
                                { "transport", problem } };
        }
        return QJsonDocument::fromJson(
            content.first().toObject().value("text").toString().toUtf8()).object();
    }

    /// app.quit(): the one verb allowed to go unanswered (the queued window
    /// close can win the race with the response), so a missing reply is an
    /// `info:` line and not a transport failure. The reply IS returned when it
    /// arrives — import.shutdown prints it.
    QJsonObject quit() { return runScript(QStringLiteral("app.quit()"), ReplyOptional); }

    /// The script's `result` as a value / string / int, with the ok flag
    /// printed when a call failed (a refusal is a test finding, not silence).
    QJsonValue value(const QString &script)
    {
        const QJsonObject reply = runScript(script);
        if (!reply.value("ok").toBool(true))
            std::printf("info: script refused: %s -> %s\n", script.toUtf8().constData(),
                        QJsonDocument(reply).toJson(QJsonDocument::Compact).constData());
        return reply.value("result");
    }
    QString string(const QString &script) { return value(script).toString(); }
    int integer(const QString &script) { return value(script).toInt(); }
};

/// Same reason as import.shutdown: keep the close path prompt-free. In a
/// QT_DEBUG build jahsettings.ini lives beside the BINARY (applicationDirPath),
/// which a scratch HOME does not isolate — this is the shared file every e2e
/// suite in this tree has to seed.
inline void seedSettings(const QString &binary)
{
    testsupport::seedSettingsForSpawnedApp(binary);
}

inline quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

/// Spawns the app on `port` and returns once it printed its session token.
/// Everything read on the way is appended to `log` — these suites assert on
/// the process's own output, so nothing may be thrown away.
inline bool spawn(QProcess &jahshaka, quint16 port, QString *tokenOut, QByteArray *log,
                  const QStringList &extraArgs = QStringList(), int bootBudgetMs = 120000)
{
    jahshaka.setProcessChannelMode(QProcess::MergedChannels);
    jahshaka.start(QStringLiteral(JAHSHAKA_BINARY),
                   QStringList{ QStringLiteral("--mcp-port=%1").arg(port) } + extraArgs);
    if (!jahshaka.waitForStarted(15000)) return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < bootBudgetMs && jahshaka.state() == QProcess::Running) {
        jahshaka.waitForReadyRead(500);
        *log += jahshaka.readAll();
        const int at = log->indexOf("MCP: token ");
        if (at >= 0) {
            const int end = log->indexOf('\n', at);
            if (end > at) {
                *tokenOut = QString::fromUtf8(log->mid(at + 11, end - at - 11)).trimmed();
                return true;
            }
        }
    }
    std::printf("---- boot log ----\n%s\n", log->constData());
    return false;
}

/// The four suites that grew their own copy spawned without keeping the boot
/// log; this overload is that call, unchanged.
inline bool spawn(QProcess &jahshaka, quint16 port, QString *tokenOut,
                  const QStringList &extraArgs = QStringList(), int bootBudgetMs = 120000)
{
    QByteArray log;
    return spawn(jahshaka, port, tokenOut, &log, extraArgs, bootBudgetMs);
}

}   // namespace mcpharness

/// The name the suites in tests/shutdown/ have used since the harness was
/// theirs. Kept as an alias so the twelve users do not all have to change the
/// word in the same commit that changes the transport.
namespace shutdownharness = mcpharness;

#endif   // TESTS_SUPPORT_MCPHARNESS_H
