/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.window_minimum — THE WINDOW FITS A 1366x768 LAPTOP (plan item 15, lane L11).
//
// The owner's laptop report: the main window refused to go below 1412x638, so
// on a 1366x768 screen part of it was always off the edge. The floor is not
// set anywhere as a number — Qt derives it: a window cannot be smaller than
// its layout's minimum, a dock area cannot be smaller than the sum of its
// docks' minimums, and every widget inside a dock contributes. So the only
// honest test is the REAL window, laid out, asked for its floor in every
// space, and then actually put at laptop size.
//
// What moved since the report: smoke S1 (L8) took the width floor to 1248 by
// giving the right column its advertised 300 px minimum (F-X1). What this lane
// fixed is the HEIGHT: the editor's bottom tray — the asset browser, and since
// S1 the script console as its second tab — insisted on 208 px of content
// (four stacked console buttons, and two list views at Qt's default scroll-area
// minimum), which made the editor window's floor 694 px: more than a 768-line
// screen has left after a taskbar and a title bar.
//
// The budget (ui/style/panelmetrics.h laptopWindowMin*): 1366 wide, and
// 768 - 48 (taskbar) - 40 (title bar) = 680 tall for the window we put it at,
// with the FLOOR at or under 640 so every desktop's chrome fits.
//
// WHY THE REAL BINARY OVER MCP (the ui.column_law harness): minimums and
// column widths come out of LAYOUT, which only happens in a shown window with
// the event loop turning between requests; a --script run holds the loop.
#include "../shutdown/mcpharness.h"

#include <QJsonDocument>
#include <QThread>

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

constexpr int kLaptopWidth = 1366;       // PanelMetrics::laptopWindowMinWidth
constexpr int kFloorHeight = 640;        // PanelMetrics::laptopWindowMinHeight
constexpr int kLaptopHeight = 680;       // 768 - taskbar 48 - title bar 40
constexpr int kViewportMinWidth = 400;   // the brief: the viewport keeps >= 400 px

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

    const QJsonObject created = mcp.runScript(QStringLiteral("project.create('WindowMinimum')"));
    CHECK(created.value("ok").toBool(), "a project is open (the editor needs one)");

    // Every space is visited FIRST at the rig's full size: a space's docks
    // only join the window's floor once they have been shown, and the editor's
    // stay in it afterwards — which is why the floor is asserted after the tour
    // too, from a page that has no docks of its own.
    const QStringList spaces{ QStringLiteral("editor"), QStringLiteral("materials"),
                              QStringLiteral("assets"), QStringLiteral("avatar"),
                              QStringLiteral("player"), QStringLiteral("publish"),
                              QStringLiteral("desktop") };

    // ---- 1. the FLOOR, in every space ---------------------------------------
    for (const QString &space : spaces) {
        mcp.runScript(QStringLiteral("app.space('%1')").arg(space));
        settle(mcp);
        const QJsonObject w = readObject(mcp, QStringLiteral("app.window()"));
        std::printf("info: %s -> floor %d x %d\n", qUtf8Printable(space),
                    w.value("minWidth").toInt(), w.value("minHeight").toInt());
        CHECK(w.value("minWidth").toInt() > 0 && w.value("minWidth").toInt() <= kLaptopWidth,
              qUtf8Printable(space + ": the window's minimum WIDTH fits a 1366 px screen"));
        CHECK(w.value("minHeight").toInt() > 0 && w.value("minHeight").toInt() <= kFloorHeight,
              qUtf8Printable(space + ": the window's minimum HEIGHT is at most 640 px "
                             "(768 less a taskbar and a title bar)"));
    }

    // ---- 2. put it AT laptop size and use it --------------------------------
    mcp.runScript(QStringLiteral("app.space('editor')"));
    settle(mcp);
    const QJsonObject sized = readObject(
        mcp, QStringLiteral("app.resizeWindow(%1, %2)").arg(kLaptopWidth).arg(kLaptopHeight));
    std::printf("info: resized -> %d x %d\n", sized.value("width").toInt(),
                sized.value("height").toInt());
    CHECK(sized.value("width").toInt() == kLaptopWidth
              && sized.value("height").toInt() == kLaptopHeight,
          "the window TAKES 1366x680 (it used to stop at its own floor)");
    settle(mcp);

    const QStringList columned{ QStringLiteral("editor"), QStringLiteral("materials"),
                                QStringLiteral("assets"), QStringLiteral("avatar") };
    for (const QString &space : columned) {
        mcp.runScript(QStringLiteral("app.space('%1')").arg(space));
        settle(mcp);
        const QJsonObject w = readObject(mcp, QStringLiteral("app.window()"));
        CHECK(w.value("width").toInt() == kLaptopWidth && w.value("height").toInt() == kLaptopHeight,
              qUtf8Printable(space + ": still 1366x680 on this page (no page pushes it back out)"));
        const QJsonObject cols = readObject(mcp, QStringLiteral("app.columns()"));
        const QJsonObject left = cols.value("left").toObject();
        const QJsonObject right = cols.value("right").toObject();
        std::printf("info: %s at laptop size -> left %d (min %d), right %d (min %d)\n",
                    qUtf8Printable(space), left.value("width").toInt(), left.value("min").toInt(),
                    right.value("width").toInt(), right.value("min").toInt());
        CHECK(!left.isEmpty() && left.value("width").toInt() >= left.value("min").toInt(),
              qUtf8Printable(space + ": the left column is at or above its minimum"));
        CHECK(!right.isEmpty() && right.value("width").toInt() >= right.value("min").toInt(),
              qUtf8Printable(space + ": the right column is at or above its minimum"));
        const int between = kLaptopWidth - left.value("width").toInt() - right.value("width").toInt();
        CHECK(between >= kViewportMinWidth,
              qUtf8Printable(space + QStringLiteral(": %1 px are left between the columns (>= 400)")
                                         .arg(between)));
    }

    // ...and the editor's own viewport, measured rather than inferred.
    mcp.runScript(QStringLiteral("app.space('editor')"));
    settle(mcp);
    const QJsonObject vp = readObject(mcp, QStringLiteral("editor.viewportState()"));
    std::printf("info: editor viewport at laptop size -> %d x %d\n", vp.value("width").toInt(),
                vp.value("height").toInt());
    CHECK(vp.value("width").toInt() >= kViewportMinWidth,
          "the editor viewport keeps at least 400 px of width at 1366x680");
    CHECK(vp.value("height").toInt() > 0, "…and a visible height");

    mcp.runScript(QStringLiteral("app.quit()"));
    if (!jahshaka.waitForFinished(30000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }

    std::printf(failures == 0 ? "ui.window_minimum: ALL PASS\n"
                              : "ui.window_minimum: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
