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

    /// A CASE THAT PROVOKES A TRANSPORT FAILURE ON PURPOSE (app.shutdown_order proves the transfer
    /// timeout reports itself) sets this on ITS client (one used for nothing after it: a client that
    /// failed sends no further request, see post()): the failure is still counted and kept, but
    /// printed as `expected(transport): …` — never the `FAIL(transport): …` a run log records as the
    /// row's first failing line (TESTING-CLEANUP-2 P10g: the shutdown_order reds were recorded as
    /// "414 ms vs 0.4 s", which was this provoked line, not the check that failed).
    bool transportFailureExpected = false;

    /// THE APP THIS CLIENT TALKS TO (TEST-SELECTOR-1 H1, plan 9ax HARNESS-EXIT-1): the harness
    /// holds the QProcess and the log the suite keeps of it, so a transport failure says HOW the
    /// child is — running, or gone with which exit code / status / error — and prints the last
    /// lines it wrote. Before this a FAIL(transport) printed the request and nothing of the
    /// process: a crash, a watchdog abort and a slow verb read the same. attach() once after
    /// spawn(); a suite that drains the process itself passes the same log it drains into.
    QProcess *app = nullptr;
    QByteArray *appLog = nullptr;
    /// A log the client keeps itself, for a suite whose boot log is a local of a helper that
    /// returns before the client is done: `mcp.ownLog = log; mcp.attach(process, mcp.ownLog);`.
    QByteArray ownLog;

    void attach(QProcess &process, QByteArray &log)
    {
        app = &process;
        appLog = &log;
    }

    /// The child's state, exit code, exit status and error, and the last `tailLines` lines of
    /// its log (drained first), as one block; printed as `info:` lines and returned. Empty when
    /// no process is attached.
    QString childReport(int tailLines = 40)
    {
        if (!app) return QString();
        if (appLog) *appLog += app->readAll();
        static const char *states[] = { "NotRunning", "Starting", "Running" };
        QString out = QStringLiteral("child: state=%1 pid=%2")
                          .arg(QString::fromLatin1(states[qBound(0, int(app->state()), 2)]))
                          .arg(app->processId());
        if (app->state() == QProcess::NotRunning)
            out += QStringLiteral(" exitCode=%1 exitStatus=%2")
                       .arg(app->exitCode())
                       .arg(app->exitStatus() == QProcess::CrashExit ? QStringLiteral("CrashExit")
                                                                     : QStringLiteral("NormalExit"));
        if (app->error() != QProcess::UnknownError)
            out += QStringLiteral(" error=%1 (%2)").arg(int(app->error())).arg(app->errorString());
        QStringList lines;
        if (appLog) {
            const QList<QByteArray> all = appLog->split('\n');
            int end = all.size();
            while (end > 0 && all[end - 1].trimmed().isEmpty()) --end;
            for (int i = qMax(0, end - tailLines); i < end; ++i)
                lines << QString::fromUtf8(all[i]);
        }
        std::printf("info: ---- %s ----\n", qUtf8Printable(out));
        std::printf("info: ---- the child's last %lld log line(s) ----\n", static_cast<long long>(lines.size()));
        for (const QString &l : lines) std::printf("info: | %s\n", qUtf8Printable(l));
        std::printf("info: ---- end of the child's log ----\n");
        std::fflush(stdout);
        return out + QLatin1Char('\n') + lines.join(QLatin1Char('\n'));
    }

    /// ReplyOptional is for the ONE request whose answer may legitimately
    /// never arrive: `app.quit()` races the queued window close, so the app is
    /// allowed to go away mid-response. Everything else is ReplyRequired.
    enum ReplyPolicy { ReplyRequired, ReplyOptional };

    QJsonObject post(const QJsonObject &body, const QString &what = QString(),
                     ReplyPolicy policy = ReplyRequired)
    {
        // ONE TRANSPORT FAILURE ENDS THE CLIENT (TESTING-CLEANUP-2 fix round): after it the app is
        // dead or wedged, and every later request used to wait out its own 90 s budget — a suite
        // after a device loss spent ~1,100 s sending into nothing before ctest killed it. Later
        // requests are NOT sent: each answers the failure object at once, naming the first failure
        // (no further FAIL(transport) line: the first one is the row's failing line).
        if (transportFailures > 0) {
            const QString label = what.isEmpty() ? body.value("method").toString() : what;
            return QJsonObject{ { "ok", false },
                                { "error", QStringLiteral("not sent — the transport already failed: %1 (%2)")
                                               .arg(lastTransportError, label) },
                                { "transport", QStringLiteral("not sent after an earlier transport failure") } };
        }
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
        if (transportFailureExpected) {
            std::printf("expected(transport): %s\n", qUtf8Printable(lastTransportError));
            std::fflush(stdout);
        } else {
            std::printf("FAIL(transport): %s\n", qUtf8Printable(lastTransportError));
            std::fflush(stdout);
            childReport();
        }
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
            childReport();
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

/// THE LAYOUT READ: the main window, its columns and its docks, as one string — what a layout
/// change (a space switch, a window resize, a tray drag) moves.
static const char *const kLayoutProbe =
    "JSON.stringify([app.window(), app.columns(), app.docks()])";

/// SETTLE BY READING, NEVER BY SLEEPING (TESTING-CLEANUP-2 P10a; trap 7: settle by count, read
/// until the value stops moving). A layout change lands over a few turns of the app's event loop
/// — posted LayoutRequests, the X server's ConfigureNotify, a dock's deferred restore — and the
/// five copies of `settle()` this replaces each slept 800 ms and hoped (ui.column_law 5 FAIL in
/// 211 runs, ui.window_minimum a 120 s TIMEOUT at 7x starvation). This reads `probe` once per MCP
/// request — every request is at least one whole turn of the app's event loop, and with `frames`
/// above 0 the read first steps that many frames on the fixed clock (`editor.frame`) — until
/// `stableReads` consecutive reads answer the same, bounded by `maxReads` COUNTED reads. Returns
/// the settled reading (empty when the probe failed); a layout that never stops moving says so in
/// an info line and the caller's next assertion reads the moving value.
inline QString settle(McpClient &mcp, const QString &probe = QString::fromLatin1(kLayoutProbe),
                      int frames = 0, int stableReads = 3, int maxReads = 200)
{
    const QString script = frames > 0
        ? QStringLiteral("editor.frame(%1); %2").arg(frames).arg(probe)
        : probe;
    QString last;
    int same = 0;
    for (int reads = 1; reads <= maxReads; ++reads) {
        const QJsonObject r = mcp.runScript(script);
        if (!r.value("ok").toBool()) {
            std::printf("info: settle: the probe failed on read %d: %s\n", reads,
                        QJsonDocument(r).toJson(QJsonDocument::Compact).constData());
            std::fflush(stdout);
            return QString();
        }
        const QJsonValue v = r.value("result");
        const QString now = v.isString() ? v.toString()
                                         : QString::fromUtf8(QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact));
        same = (reads > 1 && now == last) ? same + 1 : 0;
        last = now;
        if (same + 1 >= stableReads) return last;
    }
    std::printf("info: settle: the layout was STILL MOVING after %d reads: %s\n", maxReads,
                qUtf8Printable(oneLine(last)));
    std::fflush(stdout);
    return last;
}

/// SETTLE ON THE REQUESTED SIZE (TESTING-CLEANUP-2 fix round): after app.resizeWindow(w, h) the
/// layout is done when the WINDOW is w x h and the editor viewport's render target is the size of
/// its widget — a state the read names, not three reads that happened to agree. Reads (each one MCP
/// request, `frames` stepped on the fixed clock first) until it holds, bounded by `maxReads`.
/// Returns whether it held; the last reading is printed when it did not.
inline bool settleToSize(McpClient &mcp, int width, int height, int frames = 3, int maxReads = 200)
{
    const QString script = QStringLiteral(
        "editor.frame(%1); (function () { var w = app.window(), v = editor.viewportState();"
        " return JSON.stringify({ w: w.width, h: w.height, tw: v.width, th: v.height,"
        " ww: v.windowW, wh: v.windowH }); })()").arg(qMax(1, frames));
    QJsonObject last;
    for (int reads = 1; reads <= maxReads; ++reads) {
        const QJsonObject r = mcp.runScript(script);
        if (!r.value("ok").toBool()) {
            std::printf("info: settleToSize: the read failed on read %d\n", reads);
            return false;
        }
        last = QJsonDocument::fromJson(r.value("result").toString().toUtf8()).object();
        const int tw = last.value("tw").toInt(), th = last.value("th").toInt();
        if (last.value("w").toInt() == width && last.value("h").toInt() == height && tw > 0 && th > 0 &&
            tw == last.value("ww").toInt() && th == last.value("wh").toInt())
            return true;
    }
    std::printf("info: settleToSize(%d x %d): not reached after %d reads: %s\n", width, height, maxReads,
                QJsonDocument(last).toJson(QJsonDocument::Compact).constData());
    return false;
}

/// WHAT THE APP SAID ABOUT ITS OWN UI THREAD, printed when a measurement of
/// that thread fails.
///
/// `spawn` below merges the child's stdout and stderr, and nothing after the
/// boot token ever reads them again — so every responsiveness red this tree
/// has had threw away the app's own account of the block. The app prints
/// `[heartbeat] UI thread blocked N ms (stage: ...)` for every gap past
/// 400 ms and, past two seconds, the WATCHDOG MAKES THE BLOCKED THREAD PRINT
/// ITS OWN BACKTRACE (services/mainthreadwatchdog.h). That backtrace WAS
/// produced in the avatar.responsive red of 2026-09-15 and discarded with the
/// pipe; a lane was then spent guessing at the block (ledger 459). It costs a
/// `readAll()` per poll to keep it.
///
/// `log` is what the suite drained out of the QProcess, and `from` is the
/// offset the MEASURED WINDOW began at — without it the print carries the
/// whole process's history and a reader attributes an earlier block to the
/// window (it did, the first time this was run: a 2 s assimp parse from a
/// project open two sections earlier). This prints the lines that name the
/// block and nothing else, so a red stays readable.
inline void printUiThreadEvidence(const QByteArray &whole, const char *why, int from = 0)
{
    const QByteArray log = whole.mid(qBound(0, from, int(whole.size())));
    std::printf("info: ---- the app's own account of its UI thread (%s) ----\n", why);
    bool inBacktrace = false;
    int printed = 0;
    const QList<QByteArray> lines = log.split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.contains("--- UI-thread backtrace")) inBacktrace = true;
        const bool wanted = inBacktrace || line.contains("[heartbeat]") ||
                            line.contains("[watchdog]");
        if (wanted && !line.isEmpty()) {
            std::printf("info: | %s\n", line.constData());
            ++printed;
        }
        if (line.contains("end of UI-thread backtrace")) inBacktrace = false;
    }
    if (printed == 0)
        std::printf("info: | (nothing: the app reported no gap past its own 400 ms "
                    "threshold, so the block was not on the UI thread it watches)\n");
    std::printf("info: ---- end of the app's account (%lld bytes since the window "
                "opened, %lld since boot) ----\n",
                static_cast<long long>(log.size()), static_cast<long long>(whole.size()));
    std::fflush(stdout);
}

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
/// the process's own output, so nothing may be thrown away (the overload that
/// kept no boot log is deleted, TEST-SELECTOR-1 H2). Then McpClient::attach().
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
    // the boot failed: HOW the child is, not only what it said (H1)
    std::printf("---- boot failed: state=%d exitCode=%d exitStatus=%s error=%s ----\n",
                int(jahshaka.state()), jahshaka.exitCode(),
                jahshaka.exitStatus() == QProcess::CrashExit ? "CrashExit" : "NormalExit",
                qUtf8Printable(jahshaka.errorString()));
    std::printf("---- boot log ----\n%s\n", log->constData());
    return false;
}

}   // namespace mcpharness

#endif   // TESTS_SUPPORT_MCPHARNESS_H
