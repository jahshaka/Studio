// ui.harness_child_log — ENGINE-UPGRADE-U3 decision 26: a child that dies of a device loss keeps its WHOLE log.
//
// The MCP harness (tests/support/mcpharness.h) prints the last 40 lines of a child that failed a request, and a lost
// device fills every one of them with the same VK_ERROR_DEVICE_LOST line: the cause, which sits BEFORE the first loss,
// never reached a row's log (ui.column_law's watch item). This row proves the harness keeps the whole log of such a
// child in the row's working directory and prints the lines around the FIRST loss — through the same childReport()
// every app-spawning suite calls, on a real child process that writes such a log and exits. CPU only: the child is a
// shell, not the app; no display, no GPU.
#include "../support/mcpharness.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

static int gFailures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++gFailures;
}

int main(int argc, char **argv)
{
    QCoreApplication qapp(argc, argv);

    // ---- the pure half: a log with no loss writes nothing; a log with one keeps every byte ----
    QTemporaryDir dir;
    check(dir.isValid(), "a scratch directory for the kept logs");
    QString path;
    check(mcpharness::keepDeviceLostLog("boot\nframe 1\nquit\n", 4242, dir.path(), &path).isEmpty() && path.isEmpty(),
          "a log with no device loss keeps nothing");
    check(QDir(dir.path()).entryList(QDir::Files).isEmpty(), "...and writes no file");

    // ---- the whole path: a real child writes 200 lines (the cause at 120, the loss from 121 on) and exits ----
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.setWorkingDirectory(dir.path());
    child.start(QStringLiteral("/bin/sh"),
                { QStringLiteral("-c"),
                  QStringLiteral("i=1; while [ $i -lt 120 ]; do echo \"boot line $i\"; i=$((i+1)); done; "
                                 "echo 'avatar preview scene: THE CAUSE'; "
                                 "i=0; while [ $i -lt 80 ]; do echo 'vkQueueSubmit: VK_ERROR_DEVICE_LOST'; "
                                 "i=$((i+1)); done; exit 3") });
    check(child.waitForStarted(10000), "the child started");
    QByteArray log;
    mcpharness::McpClient mcp;
    mcp.attach(child, log);
    const qint64 pid = mcp.appPid;
    check(pid > 0, "attach() recorded the child's pid");
    check(child.waitForFinished(10000), "the child exited");
    const QString cwd = QDir::currentPath();
    QDir::setCurrent(dir.path());   // the row's working directory is where the kept log goes
    const QString report = mcp.childReport();
    QDir::setCurrent(cwd);

    const QString kept = QDir(dir.path()).filePath(QStringLiteral("child-%1-device-lost.log").arg(pid));
    check(report.contains(QStringLiteral("device-lost log: ") + kept), "childReport names the kept log");
    QFile f(kept);
    check(f.open(QIODevice::ReadOnly), "the kept log exists after the child died");
    const QByteArray whole = f.readAll();
    check(whole == log, "...and it is the WHOLE log, byte for byte");
    check(whole.count('\n') == 200, "...all 200 lines of it");
    check(whole.startsWith("boot line 1\n"), "...from the first line");
    check(!report.contains(QStringLiteral("THE CAUSE")), "the 40-line tail alone never reached the cause");
    const QString lost = mcpharness::keepDeviceLostLog(log, pid, dir.path());
    check(lost.contains(QStringLiteral("the first device loss is line 121 of")), "the context names the first loss");
    check(lost.contains(QStringLiteral("avatar preview scene: THE CAUSE")), "...and carries the line before it");
    check(lost.contains(QStringLiteral(">> vkQueueSubmit: VK_ERROR_DEVICE_LOST")), "...and marks the loss itself");

    std::printf("%s\n", gFailures ? "HARNESS CHILD LOG FAILED" : "ALL HARNESS CHILD LOG CHECKS PASSED");
    return gFailures ? 1 : 0;
}
