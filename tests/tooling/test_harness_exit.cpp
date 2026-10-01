// devprocess.harness_exit — THE HARNESS SAYS HOW ITS CHILD DIED (TEST-SELECTOR-1 H1, plan 9ax
// HARNESS-EXIT-1). tests/support/mcpharness.h's McpClient holds the QProcess it talks to
// (attach()), and a transport failure prints the child's state, exit code, exit status and error
// and the LAST 40 lines of its log — before this a FAIL(transport) printed the request only, so a
// crash, a watchdog abort and a slow verb read the same. Proved on a stand-in child (a shell that
// prints 60 numbered lines and waits), no app and no display:
//   1. the child is KILLED, then a request fails (nobody listens on the port): the printed output
//      carries FAIL(transport), state=NotRunning, exitStatus=CrashExit and exactly lines 21..60;
//   2. a child that EXITS 5 by itself: exitCode=5 exitStatus=NormalExit;
//   3. a live child: state=Running, and no exit fields are invented.
#include "../support/mcpharness.h"

#include <QCoreApplication>
#include <QTemporaryFile>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } std::fflush(stdout); } while (0)

/// Everything `fn` prints to stdout, captured through a temporary file (the harness prints with
/// printf, which is what a ctest log keeps).
template <typename F>
static QByteArray captured(F fn)
{
    QTemporaryFile tmp;
    tmp.open();
    std::fflush(stdout);
    const int saved = dup(1);
    dup2(tmp.handle(), 1);
    fn();
    std::fflush(stdout);
    dup2(saved, 1);
    close(saved);
    tmp.seek(0);
    return tmp.readAll();
}

static bool startChild(QProcess &child, QByteArray &log, const QString &script, const QByteArray &until)
{
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QStringLiteral("/bin/sh"), QStringList{ QStringLiteral("-c"), script });
    if (!child.waitForStarted(10000)) return false;
    for (int i = 0; i < 100 && !log.contains(until); ++i) {
        child.waitForReadyRead(100);
        log += child.readAll();
    }
    return log.contains(until);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace mcpharness;
    const QString sixty = QStringLiteral("i=1; while [ $i -le 60 ]; do echo line-$i; i=$((i+1)); done; sleep 60");

    // ---- 1. a KILLED child, then a request that fails --------------------------------------------
    {
        QProcess child;
        QByteArray log;
        CHECK(startChild(child, log, sixty, "line-60"), "the stand-in child printed its 60 lines");
        McpClient mcp;
        mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(freePort()));   // nobody listens
        mcp.token = QStringLiteral("x");
        mcp.transferTimeoutMs = 5000;
        mcp.attach(child, log);
        child.kill();
        child.waitForFinished(10000);
        QJsonObject reply;
        const QByteArray out = captured([&] { reply = mcp.runScript(QStringLiteral("app.version()")); });
        std::printf("---- what the failing request printed ----\n%s---- end ----\n", out.constData());
        CHECK(reply.contains(QStringLiteral("transport")) && out.contains("FAIL(transport)"),
              "the request failed as a transport failure");
        CHECK(out.contains("state=NotRunning") && out.contains("exitStatus=CrashExit"),
              "...and the harness printed the killed child's state and CrashExit");
        QList<QByteArray> tail;
        for (const QByteArray &l : out.split('\n'))
            if (l.startsWith("info: | line-")) tail << l.mid(int(qstrlen("info: | ")));
        CHECK(tail.size() == 40 && tail.first() == "line-21" && tail.last() == "line-60",
              "...and exactly the last 40 lines of its log (line-21 .. line-60)");
    }
    // ---- 2. a child that exits 5 by itself ---------------------------------------------------------
    {
        QProcess child;
        QByteArray log;
        CHECK(startChild(child, log, QStringLiteral("echo bye; exit 5"), "bye"), "the second child ran");
        child.waitForFinished(10000);
        McpClient mcp;
        mcp.attach(child, log);
        const QString rep = mcp.childReport();
        CHECK(rep.contains(QStringLiteral("exitCode=5")) && rep.contains(QStringLiteral("exitStatus=NormalExit"))
                  && rep.contains(QStringLiteral("bye")),
              "a child that exited 5: exitCode=5 exitStatus=NormalExit, its line kept");
    }
    // ---- 3. a live child ----------------------------------------------------------------------------
    {
        QProcess child;
        QByteArray log;
        CHECK(startChild(child, log, QStringLiteral("echo up; sleep 30"), "up"), "the third child is up");
        McpClient mcp;
        mcp.attach(child, log);
        const QString rep = mcp.childReport();
        CHECK(rep.contains(QStringLiteral("state=Running")) && !rep.contains(QStringLiteral("exitCode")),
              "a live child: state=Running and no exit fields");
        child.kill();
        child.waitForFinished(10000);
    }
    std::printf("devprocess.harness_exit: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
