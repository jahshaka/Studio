// ui.column_law — EVERY PAGE'S COLUMNS ARE THE EDITOR'S COLUMNS, and the
// editor's bottom tray carries the console as a TAB (smoke S1, owner
// 2026-09-11: "all right columns (Materials, Avatars, Assets) and left columns
// unify on the Editor's widths — the Editor right column is the correct width";
// "the editor's bottom asset-tray widget gets TABS at its top, Unreal style —
// turning the console on adds a Console tab beside Assets and the two share
// that widget").
//
// Before S1 every page carried its own numbers: the Materials docks were 330,
// the Assets metadata pane lived in a 280-380 band, the Avatar page opened at
// 220/800/280, and the editor's right column only LOOKED 396 wide because the
// Presets panel's MINIMUM was 396 — a minimum 96 px above the 300 the same
// column advertised, so it could never be dragged to the width its own
// constant promised (platform audit F-X1). Walking from page to page moved
// both edges of the work area.
//
// WHY THE REAL BINARY OVER MCP, and not an offscreen widget test: these widths
// are produced by LAYOUT — dock areas, splitters, size hints — and a layout
// only happens in a window that is actually shown, with an event loop running
// between the show and the measurement. A --script run cannot see them (the
// script holds the loop for its whole run), so the suite drives the app the way
// a user does: one request at a time, with the loop turning in between. The
// shutdown suites' harness does exactly this already.
//
// The assertions are the LAW, read from the app itself: app.columns() reports
// what PanelMetrics says (`metrics`) beside what the page did (`left`/`right`),
// so a page that forgot the constant fails here, per page, at the real laid-out
// width.
#include "../shutdown/mcpharness.h"

#include <QJsonDocument>
#include <QThread>

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// run_script + JSON.stringify, parsed back. Empty object on any failure.
QJsonObject readObject(McpClient &mcp, const QString &expression)
{
    const QJsonObject reply = mcp.runScript(QStringLiteral("JSON.stringify(%1)").arg(expression));
    if (!reply.value("ok").toBool()) {
        std::printf("info: script failed: %s\n",
                    qUtf8Printable(reply.value("error").toString()));
        return {};
    }
    return QJsonDocument::fromJson(reply.value("result").toString().toUtf8()).object();
}

/// The layout settles between requests, not inside one: give the window a
/// couple of event-loop turns after a space switch before measuring it.
void settle(McpClient &mcp)
{
    QThread::msleep(800);
    mcp.runScript(QStringLiteral("true"));
}

}   // namespace

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

    const QJsonObject created = mcp.runScript(QStringLiteral("project.create('ColumnLaw')"));
    CHECK(created.value("ok").toBool(), "a project is open (editor and materials need one)");

    // ---- 1. the column law, page by page -----------------------------------
    const QStringList spaces{ QStringLiteral("editor"), QStringLiteral("materials"),
                              QStringLiteral("assets"), QStringLiteral("avatar") };
    QVector<int> leftWidths, rightWidths;
    for (const QString &space : spaces) {
        const QJsonObject switched = mcp.runScript(QStringLiteral("app.space('%1')").arg(space));
        CHECK(switched.value("ok").toBool(), qUtf8Printable(space + " page opens"));
        settle(mcp);

        const QJsonObject cols = readObject(mcp, QStringLiteral("app.columns()"));
        const QJsonObject metrics = cols.value("metrics").toObject();
        const QJsonObject left = cols.value("left").toObject();
        const QJsonObject right = cols.value("right").toObject();
        std::printf("info: %s -> left %d (min %d), right %d (min %d)\n", qUtf8Printable(space),
                    left.value("width").toInt(), left.value("min").toInt(),
                    right.value("width").toInt(), right.value("min").toInt());

        CHECK(!left.isEmpty() && !right.isEmpty(),
              qUtf8Printable(space + ": the page reports both of its columns"));
        CHECK(left.value("width").toInt() == metrics.value("leftWidth").toInt(),
              qUtf8Printable(space + ": the left column opens at PanelMetrics::leftColumnWidth"));
        CHECK(left.value("min").toInt() == metrics.value("leftMin").toInt(),
              qUtf8Printable(space + ": the left column's floor is leftColumnMinWidth"));
        CHECK(right.value("width").toInt() == metrics.value("rightWidth").toInt(),
              qUtf8Printable(space + ": the right column opens at rightColumnWidth"));
        // F-X1: the floor is the column's OWN minimum, not the presets panel's
        // old 396 — the column has to be draggable to what it advertises.
        CHECK(right.value("min").toInt() == metrics.value("rightMin").toInt(),
              qUtf8Printable(space + ": the right column's floor is rightColumnMinWidth (F-X1)"));
        leftWidths.append(left.value("width").toInt());
        rightWidths.append(right.value("width").toInt());
    }
    bool sameLeft = true, sameRight = true;
    for (int i = 1; i < leftWidths.size(); ++i) {
        sameLeft = sameLeft && leftWidths.at(i) == leftWidths.at(0);
        sameRight = sameRight && rightWidths.at(i) == rightWidths.at(0);
    }
    CHECK(sameLeft, "every page's LEFT column is the same width");
    CHECK(sameRight, "every page's RIGHT column is the same width");

    // ---- 2. the bottom tray's tabs -----------------------------------------
    mcp.runScript(QStringLiteral("app.space('editor')"));
    settle(mcp);

    QJsonObject tray = readObject(mcp, QStringLiteral("editor.trayState()"));
    CHECK(tray.value("tab").toString() == QLatin1String("assets"),
          "the tray opens on the Assets tab");
    CHECK(tray.value("consoleVisible").toBool() == false,
          "…and there is no Console tab until the console is asked for");
    CHECK(tray.value("tabs").toArray().size() == 1, "one tab in the bar to begin with");

    tray = readObject(mcp, QStringLiteral("editor.tray({tab: 'console'})"));
    CHECK(tray.value("consoleVisible").toBool(), "asking for the console ADDS the Console tab");
    CHECK(tray.value("tab").toString() == QLatin1String("console"),
          "…and brings it to the front of the same tray widget");
    CHECK(tray.value("tabs").toArray().size() == 2,
          "…beside Assets — the two share one widget, they do not split the area");
    CHECK(tray.value("consoleFocused").toBool(),
          "…and the keyboard is in the console input (what Ctrl+` promises)");
    CHECK(tray.value("visible").toBool(), "the tray itself is on screen");

    tray = readObject(mcp, QStringLiteral("editor.tray({tab: 'assets'})"));
    CHECK(tray.value("tab").toString() == QLatin1String("assets"),
          "picking the Assets tab brings the asset browser forward");
    CHECK(tray.value("consoleVisible").toBool(),
          "…without throwing the console away (that is what a tab bar is for)");

    tray = readObject(mcp, QStringLiteral("editor.tray({console: false})"));
    CHECK(!tray.value("consoleVisible").toBool(), "turning the console off removes its tab");
    CHECK(tray.value("tab").toString() == QLatin1String("assets"),
          "…and the tray returns to Assets");

    const QJsonObject bogus = mcp.runScript(QStringLiteral("editor.tray({tab: 'nope'})"));
    CHECK(!bogus.value("ok").toBool(), "an unknown tab name is refused, not guessed at");

    mcp.runScript(QStringLiteral("app.quit()"));
    if (!jahshaka.waitForFinished(30000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }

    std::printf(failures == 0 ? "ui.column_law: ALL PASS\n" : "ui.column_law: %d FAILURES\n",
                failures);
    return failures == 0 ? 0 : 1;
}
