// Quit-during-import must terminate the PROCESS (owner-reported zombie).
//
// Pre-fix, quitting the app while (or after) a threaded import ran left the
// process alive headless: the unparented progress dialog (a top-level
// window) kept quitOnLastWindowClosed from firing after the main window
// closed — and it only ever hide()s, which never fires lastWindowClosed, so
// exec() idled forever with an orphaned "loading" dialog on the desktop.
// Independently, the worker's Qt::BlockingQueuedConnection commit hop could
// only be woken by destroying the runner (a read-after-destroy race), and a
// surviving worker deadlocked QThreadPool's exit-time wait.
//
// This test spawns the REAL binary (like mcp.e2e), drives it over MCP:
//   1. quit DURING a many-file import batch  -> process exits bounded
//   2. quit right AFTER a batch completes    -> process exits bounded
// The contract asserted is the hard one: the process is GONE within
// kExitBudgetMs of app.quit(), exit code 0 (a logged forced exit also
// returns the real code — better than a zombie, and still bounded).
#include "../support/mcpharness.h"
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
#include <QStandardPaths>
#include <QTcpServer>
#include <cstdio>

static int failures = 0;
using namespace mcpharness;

#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const int kExitBudgetMs = 30000;   // watchdog fires at 20s; give slack

/// app.quit() then assert the PROCESS terminates within the budget.
static void quitAndAssertExit(QProcess &jahshaka, McpClient &mcp, const char *label)
{
    // The reply can lose the race with the queued close (the app is allowed
    // to go away mid-response), so the reply is informational only — the
    // assertion that matters is that the PROCESS goes away.
    const QJsonObject quit = mcp.quit();
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
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

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
        QByteArray log;
        CHECK(spawn(jahshaka, freePort(), &token, &log), "app booted and printed the MCP token (during-case)");
        if (token.isEmpty()) return 1;

        McpClient mcp;
        mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp")
                           .arg(jahshaka.arguments().first().split('=').last()));
        mcp.token = token;
        mcp.attach(jahshaka, log);   // a transport failure prints the child's state + log tail
        mcp.clientName = QStringLiteral("import-shutdown-test");
        // A 480 s suite whose verbs import a whole directory synchronously: half of
        // its own budget per request, well above the harness default.
        mcp.transferTimeoutMs = 240000;
        mcp.initialize();

        CHECK(mcp.runScript(QStringLiteral("project.create('shutdown_during')")).value("ok").toBool(),
              "project created (during-case)");
        // MID-FLIGHT BY COUNT, NOT BY CLOCK (TESTING-CLEANUP-2 P10b): ONE script starts the batch and
        // waits for its FIRST commit with editor.waitForImported (event-loop turns inside the app,
        // never a clock and never a poll of the library from here), then the quit goes out. It used
        // to sleep 700 ms and hope: on a starved box the batch had not reached its first object yet,
        // or had finished (4 FAIL in 206 runs).
        const QJsonObject wait = mcp.runScript(QStringLiteral(
            "(function () { var c0 = editor.importState().committed;"
            " if (!editor.importAssets(%1)) return { started: false };"
            " var w = editor.waitForImported(c0 + 1);"
            " return { started: true, committed: w.committed - c0, running: w.running, reached: w.reached,"
            " turns: w.turns }; })()").arg(batchJs));
        const QJsonObject w = wait.value("result").toObject();
        CHECK(wait.value("ok").toBool() && w.value("started").toBool(), "threaded import batch started");
        const int imported = w.value("committed").toInt();
        std::printf("info: files committed when the quit was issued: %d of %d (after %d event-loop turns)\n",
                    imported, int(batch.size()), w.value("turns").toInt());
        CHECK(w.value("reached").toBool() && imported >= 1,
              "the batch is running at quit time (its first file is committed)");
        CHECK(w.value("running").toBool() && imported < batch.size(), "the batch is still mid-flight at quit time");

        quitAndAssertExit(jahshaka, mcp, "quit-during-import");
    }

    // ---- 2. quit right AFTER an import batch ------------------------------
    {
        QProcess jahshaka;
        QString token;
        QByteArray log;
        CHECK(spawn(jahshaka, freePort(), &token, &log), "app booted and printed the MCP token (after-case)");
        if (token.isEmpty()) return 1;

        McpClient mcp;
        mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp")
                           .arg(jahshaka.arguments().first().split('=').last()));
        mcp.token = token;
        mcp.attach(jahshaka, log);   // a transport failure prints the child's state + log tail
        mcp.clientName = QStringLiteral("import-shutdown-test");
        mcp.transferTimeoutMs = 240000;
        mcp.initialize();

        CHECK(mcp.runScript(QStringLiteral("project.create('shutdown_after')")).value("ok").toBool(),
              "project created (after-case)");
        // Small batch, then WAIT for it to finish before quitting — by count, in the app's own
        // event-loop turns (editor.waitForImported), not a 1 s poll and a 2 s sleep.
        const QString smallJs = QStringLiteral("['%1','%2']").arg(batch.at(0), batch.at(1));
        const QJsonObject wait = mcp.runScript(QStringLiteral(
            "(function () { var c0 = editor.importState().committed;"
            " if (!editor.importAssets(%1)) return { started: false };"
            " var w = editor.waitForImported(c0 + 2);"
            " return { started: true, committed: w.committed - c0, reached: w.reached }; })()").arg(smallJs));
        const QJsonObject w = wait.value("result").toObject();
        CHECK(wait.value("ok").toBool() && w.value("started").toBool(), "small import batch started");
        CHECK(w.value("reached").toBool() && w.value("committed").toInt() >= 2,
              "import batch completed before the quit");

        quitAndAssertExit(jahshaka, mcp, "quit-after-import");
    }

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
