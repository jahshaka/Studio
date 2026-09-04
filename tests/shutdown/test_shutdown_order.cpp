// app.shutdown_order — the shutdown sequence, asserted instead of remembered
// (STABILITY_PROGRAM_SPEC.md §1.5 / Lane 3).
//
// Nobody had written the order down, and twice that cost a defect: undo
// commands writing to a database ~MainWindow had already closed (`740e0155`),
// and the Engine — held by shared_ptr copies in the viewport widgets — dying
// at Qt's child-tree teardown, i.e. AFTER closeDatabase(), with the engine
// teardown law running there. src/shell/shutdownorder.h is the written order;
// this is the test that keeps it true.
//
// The app prints "[shutdown] step N/7 <name>" from each participant (debug
// builds). This spawns the REAL binary (the import.shutdown pattern), quits it
// through app.quit() — the normal close path, so all seven steps run — and
// asserts the seven steps appear exactly once each, in order.
#include "mcpharness.h"

#include <QRegularExpression>
#include <QThread>

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const int kExitBudgetMs = 30000;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

    QProcess jahshaka;
    QString token;
    QByteArray log;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token, &log), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.initialize();

    // A project is opened first so the teardown has something real to take
    // apart: a scene, an engine scene mirrored into it, and an undo stack with
    // entries — which is exactly the state the two incidents happened in.
    const QJsonObject created = mcp.runScript(QStringLiteral("project.create('ShutdownOrder')"));
    CHECK(created.value("ok").toBool(), "a project was created for the teardown to unwind");
    mcp.runScript(QStringLiteral("node.add('cube')"));

    // Nothing has shut down yet.
    log += jahshaka.readAll();
    CHECK(!log.contains("[shutdown] step "), "no shutdown step fires before the quit");

    mcp.runScript(QStringLiteral("app.quit()"));

    QElapsedTimer exitTimer;
    exitTimer.start();
    const bool exited = jahshaka.waitForFinished(kExitBudgetMs);
    log += jahshaka.readAll();
    std::printf("info: exit after %lld ms\n", static_cast<long long>(exitTimer.elapsed()));
    CHECK(exited, "process terminated within the exit budget");
    if (!exited) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    else {
        const bool clean = jahshaka.exitStatus() == QProcess::NormalExit && jahshaka.exitCode() == 0;
        CHECK(clean, "process exited normally with code 0");
    }

    // ---- the sequence ------------------------------------------------------
    QVector<int> steps;
    QStringList names;
    QRegularExpression re(QStringLiteral(R"(\[shutdown\] step (\d)/7 ([^\n]*))"));
    auto it = re.globalMatch(QString::fromUtf8(log));
    while (it.hasNext()) {
        const auto m = it.next();
        steps.append(m.captured(1).toInt());
        names.append(m.captured(2).trimmed());
    }
    for (int i = 0; i < steps.size(); ++i)
        std::printf("info: step %d — %s\n", steps.at(i), qUtf8Printable(names.at(i)));

    CHECK(steps.size() == 7, "exactly seven shutdown steps were recorded");
    bool ordered = true, onceEach = true;
    for (int i = 0; i < steps.size(); ++i) {
        if (steps.at(i) != i + 1) ordered = false;
        if (steps.count(steps.at(i)) != 1) onceEach = false;
    }
    CHECK(ordered, "the steps ran 1..7 in order");
    CHECK(onceEach, "each step ran exactly once");

    // The recorder itself complains when the order is violated; a clean run
    // must contain none of those complaints.
    CHECK(!log.contains("fired again"), "no shutdown step fired twice");
    CHECK(!log.contains("fired AFTER step"), "no shutdown step ran out of order");

    // The load-bearing pair, named: the engine's death is step 5 and the
    // database closes at step 6. If someone reorders them the app is back to
    // tearing the engine down against a closed connection.
    const int engineAt = steps.indexOf(5), dbAt = steps.indexOf(6);
    CHECK(engineAt >= 0 && dbAt >= 0 && engineAt < dbAt,
          "the engine-holding widgets are destroyed BEFORE the database closes");
    CHECK(!log.contains("the Engine is STILL referenced"),
          "no shared_ptr<Engine> holder outlived the viewports");

    if (failures) {
        const QByteArray tail = log.right(4000);
        std::printf("---- app output tail ----\n%s\n-------------------------\n", tail.constData());
    }
    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
