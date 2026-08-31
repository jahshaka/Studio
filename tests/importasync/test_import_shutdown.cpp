// Quit-during-import must terminate the PROCESS (owner-reported zombie).
//
// Pre-fix, quitting the app while (or after) a threaded import ran left the
// process alive headless: the unparented progress dialog (a top-level
// window) kept quitOnLastWindowClosed from firing after the main window
// closed — and it only ever hide()s, which never fires lastWindowClosed, so
// exec() idled forever with an orphaned "loading" dialog on the desktop.
// Independently, the worker's Qt::BlockingQueuedConnection commit hop could
// only be woken by destroying the runner (a use-after-free race), and a
// surviving worker deadlocked QThreadPool's exit-time wait.
//
// This test spawns the REAL binary (like mcp.e2e), drives it over MCP:
//   1. quit DURING a many-file import batch  -> process exits bounded
//   2. quit right AFTER a batch completes    -> process exits bounded
// The contract asserted is the hard one: the process is GONE within
// kExitBudgetMs of app.quit(), exit code 0 (a logged forced exit also
// returns the real code — better than a zombie, and still bounded).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>
#include <QThread>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const int kExitBudgetMs = 30000;   // watchdog fires at 20s; give slack

struct McpClient
{
    QNetworkAccessManager net;
    QUrl url;
    QString token;
    int id = 0;

    QJsonObject post(const QJsonObject &body)
    {
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        QNetworkReply *reply = net.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        return QJsonDocument::fromJson(data).object();
    }

    void initialize()
    {
        post(QJsonObject{ { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "initialize" },
                          { "params", QJsonObject{
                                { "protocolVersion", "2025-06-18" },
                                { "capabilities", QJsonObject{} },
                                { "clientInfo", QJsonObject{ { "name", "shutdown-test" }, { "version", "0" } } } } } });
        post(QJsonObject{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } });
    }

    /// Runs a script through the run_script tool; returns the parsed
    /// {ok, result, ...} payload (empty object on transport failure).
    QJsonObject runScript(const QString &script)
    {
        const QJsonObject reply = post(QJsonObject{
            { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "tools/call" },
            { "params", QJsonObject{ { "name", "run_script" },
                                     { "arguments", QJsonObject{ { "script", script } } } } } });
        const QJsonArray content = reply.value("result").toObject().value("content").toArray();
        if (content.isEmpty()) return {};
        return QJsonDocument::fromJson(
            content.first().toObject().value("text").toString().toUtf8()).object();
    }
};

/// The app's settings live beside the binary in QT_DEBUG builds. Merge (never
/// overwrite) the two keys the close path reads, so no modal donate dialog
/// blocks the scripted quit and autosave keeps the close prompt-free.
static void seedSettings()
{
    const QString ini = QFileInfo(QStringLiteral(JAHSHAKA_BINARY)).dir().filePath("jahsettings.ini");
    QSettings settings(ini, QSettings::IniFormat);
    settings.setValue("ddialog_seen", true);
    settings.setValue("auto_save", true);
    settings.sync();
}

static bool spawn(QProcess &jahshaka, quint16 port, QString *tokenOut)
{
    jahshaka.setProcessChannelMode(QProcess::MergedChannels);
    jahshaka.start(QStringLiteral(JAHSHAKA_BINARY),
                   { QStringLiteral("--mcp-port=%1").arg(port) });
    if (!jahshaka.waitForStarted(15000)) return false;
    QByteArray bootLog;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 120000 && jahshaka.state() == QProcess::Running) {
        jahshaka.waitForReadyRead(500);
        bootLog += jahshaka.readAll();
        const int at = bootLog.indexOf("MCP: token ");
        if (at >= 0) {
            const int end = bootLog.indexOf('\n', at);
            if (end > at) {
                *tokenOut = QString::fromUtf8(bootLog.mid(at + 11, end - at - 11)).trimmed();
                return true;
            }
        }
    }
    std::printf("---- boot log ----\n%s\n", bootLog.constData());
    return false;
}

static quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

/// app.quit() then assert the PROCESS terminates within the budget.
static void quitAndAssertExit(QProcess &jahshaka, McpClient &mcp, const char *label)
{
    // The reply can lose the race with the queued close (the app is allowed
    // to go away mid-response), so the reply is informational only — the
    // assertion that matters is that the PROCESS goes away.
    const QJsonObject quit = mcp.runScript(QStringLiteral("app.quit()"));
    std::printf("info: %s: app.quit() reply ok=%s\n", label,
                quit.value("ok").toBool() ? "true" : "false/none");

    QElapsedTimer timer;
    timer.start();
    const bool exited = jahshaka.waitForFinished(kExitBudgetMs);
    std::printf("info: %s: exit after %lld ms\n", label, static_cast<long long>(timer.elapsed()));
    CHECK(exited, "process terminated within the exit budget");
    if (!exited) {
        jahshaka.kill();
        jahshaka.waitForFinished(5000);
        return;
    }
    const bool clean = jahshaka.exitStatus() == QProcess::NormalExit && jahshaka.exitCode() == 0;
    if (!clean) {
        std::printf("info: %s: exitStatus=%s exitCode=%d\n", label,
                    jahshaka.exitStatus() == QProcess::CrashExit ? "CrashExit" : "NormalExit",
                    jahshaka.exitCode());
        const QByteArray tail = jahshaka.readAll().right(2000);
        std::printf("---- app output tail ----\n%s\n-------------------------\n", tail.constData());
    }
    CHECK(clean, "process exited normally with code 0");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings();

    const QString cwd = QDir::currentPath();
    const QString fixture =
        QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/mislabeled_embedded.glb");
    CHECK(QFileInfo::exists(fixture), "GLB fixture present");

    // A many-file batch: enough runway that app.quit() lands mid-batch even
    // though one small fixture imports in tens of milliseconds.
    QStringList batch;
    for (int i = 0; i < 300; ++i) {
        const QString copy = cwd + QStringLiteral("/batch_%1.glb").arg(i);
        if (!QFileInfo::exists(copy)) QFile::copy(fixture, copy);
        batch.append(copy);
    }
    const QString batchJs = QStringLiteral("['%1']").arg(batch.join("','"));

    // ---- 1. quit DURING the import batch ----------------------------------
    {
        QProcess jahshaka;
        QString token;
        CHECK(spawn(jahshaka, freePort(), &token), "app booted and printed the MCP token (during-case)");
        if (token.isEmpty()) return 1;

        McpClient mcp;
        mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp")
                           .arg(jahshaka.arguments().first().split('=').last()));
        mcp.token = token;
        mcp.initialize();

        CHECK(mcp.runScript(QStringLiteral("project.create('shutdown_during')")).value("ok").toBool(),
              "project created (during-case)");
        // The scratch store persists across runs, so mid-flight is measured
        // as a DELTA against the pre-import object count.
        const int before = mcp.runScript(QStringLiteral(
            "assets.list({scope:'store'}).filter(function(a){return a.type=='object'}).length"))
                               .value("result").toInt();
        CHECK(mcp.runScript(QStringLiteral("editor.importAssets(%1)").arg(batchJs)).value("ok").toBool(),
              "threaded import batch started");
        QThread::msleep(700);   // let the batch get properly mid-flight

        // Prove the quit really lands MID-batch: not every file is in yet.
        const QJsonObject progress = mcp.runScript(QStringLiteral(
            "assets.list({scope:'store'}).filter(function(a){return a.type=='object'}).length"));
        const int imported = progress.value("result").toInt() - before;
        std::printf("info: objects imported when the quit was issued: %d of %d\n",
                    imported, int(batch.size()));
        CHECK(progress.value("ok").toBool() && imported < batch.size(),
              "the batch is still mid-flight at quit time");

        quitAndAssertExit(jahshaka, mcp, "quit-during-import");
    }

    // ---- 2. quit right AFTER an import batch ------------------------------
    {
        QProcess jahshaka;
        QString token;
        CHECK(spawn(jahshaka, freePort(), &token), "app booted and printed the MCP token (after-case)");
        if (token.isEmpty()) return 1;

        McpClient mcp;
        mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp")
                           .arg(jahshaka.arguments().first().split('=').last()));
        mcp.token = token;
        mcp.initialize();

        CHECK(mcp.runScript(QStringLiteral("project.create('shutdown_after')")).value("ok").toBool(),
              "project created (after-case)");
        // Small batch, then WAIT for it to finish before quitting. Counted as
        // a delta: the scratch store carries rows from earlier runs.
        const QString countJs = QStringLiteral(
            "assets.list({scope:'store'}).filter(function(a){return a.type=='object'}).length");
        const int before = mcp.runScript(countJs).value("result").toInt();
        const QString smallJs = QStringLiteral("['%1','%2']").arg(batch.at(0), batch.at(1));
        CHECK(mcp.runScript(QStringLiteral("editor.importAssets(%1)").arg(smallJs)).value("ok").toBool(),
              "small import batch started");
        bool done = false;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 90000) {
            const QJsonObject r = mcp.runScript(countJs);
            if (r.value("ok").toBool() && r.value("result").toInt() - before >= 2) { done = true; break; }
            QThread::msleep(1000);
        }
        CHECK(done, "import batch completed before the quit");
        QThread::msleep(2000);   // let completion tails start

        quitAndAssertExit(jahshaka, mcp, "quit-after-import");
    }

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
