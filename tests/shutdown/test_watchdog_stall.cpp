// app.watchdog_stall — "the window froze; here is WHERE", asserted
// (STABILITY_PROGRAM_SPEC.md Lane 5, decision D2 option A).
//
// MainThreadHeartbeat measures a freeze; the watchdog acts on one. A thread of
// our own polls the heartbeat's last-tick ATOMIC and, past the threshold,
// makes the UI thread photograph itself (pthread_kill + SIGUSR2 — a watchdog
// thread calling backtrace() would photograph itself, which is useless).
//
// Two contracts, exactly as specced:
//   * a deliberate 3 s UI-thread block produces EXACTLY ONE [warn] and a
//     NON-EMPTY stack — and the stack is really the UI thread's (it contains
//     the frame that is blocking),
//   * a 100 ms block produces nothing.
// Plus the safeguards that keep the mechanism from being the crash: the
// feature is off outside a dev build, and app.blockUiThread refuses there.
#include "mcpharness.h"

#include <QThread>

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static int countOf(const QByteArray &haystack, const char *needle)
{
    int n = 0, at = 0;
    while ((at = haystack.indexOf(needle, at)) >= 0) { ++n; at += int(qstrlen(needle)); }
    return n;
}

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

    // DELIBERATELY NO PROJECT. This suite quits a session that never opened
    // one, which is how it found the close-path zombie fixed in this lane: the
    // editor's default scene leaves the undo stack dirty while
    // ProjectService::isSceneOpen() is false, and closeEvent used to answer
    // that with a modal "Unsaved Changes" box nothing could dismiss — the
    // process then outlived its own quit. Every other MCP e2e suite happens to
    // create a project first, which is why nobody had met it.

    // ---- it is on, by itself, in a dev build ------------------------------
    const QJsonObject before = mcp.runScript(QStringLiteral("app.watchdogStats()"))
                                   .value("result").toObject();
    std::printf("info: watchdog at rest: %s\n",
                QJsonDocument(before).toJson(QJsonDocument::Compact).constData());
    CHECK(before.value("supported").toBool(), "the watchdog is supported in this build");
    CHECK(before.value("running").toBool(),
          "the watchdog started itself with the window (no script had to ask)");

    // The heartbeat is its input and must be running without anyone starting
    // it — the watchdog does that itself.
    const QJsonObject hb = mcp.runScript(QStringLiteral("app.heartbeatStats()"))
                               .value("result").toObject();
    CHECK(hb.value("running").toBool(), "the heartbeat probe is running as the watchdog's input");

    // ---- THE COOLDOWN IS PART OF THE CONTRACT (ENGINE-5 item 4) -----------
    //
    // The watchdog rate-limits itself: after a report it drops everything for
    // kCooldownMs (5 s), because "a UI thread ticking once every three seconds
    // is one pathology, not fifty incidents". That is correct behaviour and
    // the reason this suite went red on every COLD-CACHE gate: booting with an
    // empty shader cache under load stalls the UI thread past the 2 s
    // threshold (measured: `lastStallMs 2075, reports 1` at rest), and the
    // deliberate 3 s block that follows a second later lands INSIDE that
    // cooldown and is dropped — `stall warnings: 1 -> 1`. Nothing about the
    // watchdog was wrong; the suite was measuring the rate limit.
    //
    // So the window is opened properly: wait until the cooldown has expired,
    // then take the baseline. `sinceLastReportMs`/`cooldownMs` exist on
    // app.watchdogStats() for exactly this — before them a caller could not
    // tell a DROPPED report from a MISSED one.
    const auto waitOutCooldown = [&](const char *why) {
        QElapsedTimer t; t.start();
        while (t.elapsed() < 30000) {
            const QJsonObject st = mcp.runScript(QStringLiteral("app.watchdogStats()"))
                                       .value("result").toObject();
            const double since = st.value("sinceLastReportMs").toDouble();
            const double cool = st.value("cooldownMs").toDouble();
            if (since < 0.0 || since >= cool) return;
            const int waitMs = int(cool - since) + 400;
            std::printf("info: %s — a report landed %.0f ms ago, waiting %d ms out of the "
                        "watchdog's %.0f ms cooldown\n", why, since, waitMs, cool);
            QThread::msleep(unsigned(waitMs));
        }
        std::printf("FAIL: the watchdog never left its cooldown\n");
        ++failures;
    };
    CHECK(before.contains("cooldownMs") && before.contains("sinceLastReportMs"),
          "app.watchdogStats() reports its cooldown");

    // A STALL THE SUITE DID NOT ASK FOR, before the measured window — the cold
    // gate's boot stall, made deliberate so that the case is covered on every
    // run and not only on the runs that happen to be slow. Everything after it
    // must behave exactly as if it had not happened.
    waitOutCooldown("before the unattributed stall");
    // THE BASELINE IS TAKEN HERE, not with `before` at the top (ENGINE-5
    // review, ledger §208). A boot stall can be reported DURING the wait above
    // — that is exactly the cold-cache case this block exists for — and a
    // baseline read before the wait would then already be one behind, so the
    // `>` below could be satisfied by the boot stall rather than by the
    // deliberate one. Read after the cooldown, nothing can land between it and
    // the block that follows.
    const int baseline = mcp.runScript(QStringLiteral("app.watchdogStats()"))
                             .value("result").toObject().value("reports").toInt();
    mcp.runScript(QStringLiteral("app.blockUiThread(3000)"));
    QThread::msleep(300);
    log += jahshaka.readAll();
    const QJsonObject unattributed = mcp.runScript(QStringLiteral("app.watchdogStats()"))
                                         .value("result").toObject();
    std::printf("info: after the unattributed stall: reports %d\n",
                unattributed.value("reports").toInt());
    CHECK(unattributed.value("reports").toInt() > baseline,
          "the unattributed stall WAS reported (the measured window starts dirty, on purpose)");
    waitOutCooldown("before the measured window");

    // THE MEASURED WINDOW. Both counts are taken here, immediately before the
    // block: never the absolute number, always the delta across this window.
    log += jahshaka.readAll();
    const int warnsBefore = countOf(log, "[watchdog] UI thread stalled");
    const int reportsBefore = mcp.runScript(QStringLiteral("app.watchdogStats()"))
                                  .value("result").toObject().value("reports").toInt();

    // ---- 1. a 3 s block: one warn, one real stack -------------------------
    // The verb does not return until the freeze is over, which is the point:
    // the app cannot answer this request while it is blocked, so the reply
    // itself proves the UI thread was gone.
    QElapsedTimer blockTimer;
    blockTimer.start();
    const QJsonObject blocked = mcp.runScript(QStringLiteral("app.blockUiThread(3000)"));
    const qint64 blockedMs = blockTimer.elapsed();
    std::printf("info: blockUiThread(3000) took %lld ms\n", static_cast<long long>(blockedMs));
    CHECK(blocked.value("ok").toBool() && blocked.value("result").toBool(),
          "app.blockUiThread(3000) ran (dev build)");
    CHECK(blockedMs >= 2900, "the UI thread really was blocked for the full duration");

    // Give the handler's output time to reach us.
    QThread::msleep(300);
    log += jahshaka.readAll();

    const int warnsAfter = countOf(log, "[watchdog] UI thread stalled");
    std::printf("info: stall warnings: %d -> %d\n", warnsBefore, warnsAfter);
    CHECK(warnsAfter - warnsBefore == 1, "exactly one [warn] for the 3 s stall");

    const QJsonObject after = mcp.runScript(QStringLiteral("app.watchdogStats()"))
                                  .value("result").toObject();
    std::printf("info: watchdog after the stall: %s\n",
                QJsonDocument(after).toJson(QJsonDocument::Compact).constData());
    CHECK(after.value("reports").toInt() - reportsBefore == 1,
          "the verb agrees: exactly one report IN THIS WINDOW");
    CHECK(after.value("lastStallMs").toDouble() >= 2000.0,
          "the reported stall is at least the threshold");

    // The stack: present, delimited, and non-empty.
    const int stackStart = log.lastIndexOf("[watchdog] --- UI-thread backtrace");
    const int stackEnd = log.lastIndexOf("[watchdog] --- end of UI-thread backtrace");
    CHECK(stackStart >= 0 && stackEnd > stackStart, "a delimited backtrace block was written");
    int frames = 0;
    QByteArray stack;
    if (stackStart >= 0 && stackEnd > stackStart) {
        stack = log.mid(stackStart, stackEnd - stackStart);
        for (const QByteArray &line : stack.split('\n'))
            if (line.contains("[0x") || line.contains("(+0x")) ++frames;
    }
    std::printf("info: backtrace frames: %d\n", frames);
    CHECK(frames >= 5, "the backtrace is non-empty (>= 5 frames)");
    // It is the UI THREAD's stack, not the watchdog's: the blocking call and
    // the process entry point are both in it. A watchdog-thread self-portrait
    // would contain neither.
    CHECK(stack.contains("nanosleep") || stack.contains("clock_nanosleep"),
          "the stack contains the blocking call itself");
    CHECK(stack.contains("main"), "the stack is the MAIN thread's (it reaches main)");

    // ---- 2. a 100 ms block: nothing ---------------------------------------
    const int warnsBeforeShort = countOf(log, "[watchdog] UI thread stalled");
    CHECK(mcp.runScript(QStringLiteral("app.blockUiThread(100)")).value("ok").toBool(),
          "app.blockUiThread(100) ran");
    QThread::msleep(600);
    log += jahshaka.readAll();
    CHECK(countOf(log, "[watchdog] UI thread stalled") == warnsBeforeShort,
          "a 100 ms block produces no warning at all");
    const QJsonObject shortStats = mcp.runScript(QStringLiteral("app.watchdogStats()"))
                                       .value("result").toObject();
    CHECK(shortStats.value("reports").toInt() == after.value("reports").toInt(),
          "the report count did not move for the short block");

    // ---- 3. the app is still healthy after being signalled ----------------
    // SA_RESTART is the reason this can be asserted: the signal lands on a UI
    // thread that may be inside a restartable syscall, and the loop has to
    // come back.
    CHECK(mcp.runScript(QStringLiteral("scene.nodes().length")).value("ok").toBool(),
          "the app still answers scripts after being signalled");

    mcp.runScript(QStringLiteral("app.quit()"));
    const bool exited = jahshaka.waitForFinished(30000);
    log += jahshaka.readAll();
    CHECK(exited, "process terminated after the watchdog fired");
    if (!exited) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    else CHECK(jahshaka.exitStatus() == QProcess::NormalExit && jahshaka.exitCode() == 0,
               "process exited normally with code 0");
    // The watchdog must not photograph its own teardown (Lane 5's "stop
    // yourself at step 2").
    CHECK(countOf(log, "[watchdog] UI thread stalled") == warnsBeforeShort,
          "no stall was reported during shutdown");

    if (failures) {
        const QByteArray tail = log.right(4000);
        std::printf("---- app output tail ----\n%s\n-------------------------\n", tail.constData());
    }
    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
