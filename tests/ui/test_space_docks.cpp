/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.space_docks — THE EDITOR COMES BACK WITH ITS PANELS (owner report,
// 2026-09-14: "when I go from Player to Editor the editor always opens with NO
// panels, just the main 3D widget — if I switch to Materials and back I see the
// full editor UI", and, after a restart that morning, an editor with no docks at
// all and a saved layout that recorded none).
//
// TWO RULES, AND THEY ARE THE TWO HALVES OF THE SAME DEFECT.
//
//  1. THE SPACE OWNS DOCK VISIBILITY. Every space but the editor hides the
//     editor's docks; coming back to the editor shows them again, by the same
//     path from every space — the Player and the Materials page cannot differ.
//     They used to: the saved dock layout was re-applied one event-loop turn
//     after the editor page opened (applyColumnWidthsOnce's queued pass, which
//     exists to give the columns their real widths), and a layout saved while
//     the docks were hidden closed every one of them again.
//
//  2. WHAT IS SAVED AT EXIT IS THE EDITOR'S LAYOUT. The docks are hidden
//     whenever another page is showing, so quitting from the Player stored "no
//     panels" as the editor's own layout — and the next launch restored it.
//     This suite quits from the PLAYER space and then boots the app a SECOND
//     time in the same data root: the editor must still open with its panels.
//
// WHY THE REAL BINARY OVER MCP (the ui.column_law harness): dock visibility and
// width are produced by a LAYOUT in a shown window, with the event loop turning
// between the space switch and the measurement — a --script run holds the loop
// for its whole run and cannot see them. And rule 2 needs a real
// MainWindow::closeEvent, which only a real quit produces.
#include "../shutdown/mcpharness.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

QJsonArray readArray(McpClient &mcp, const QString &expression)
{
    const QJsonObject reply = mcp.runScript(QStringLiteral("JSON.stringify(%1)").arg(expression));
    if (!reply.value("ok").toBool()) {
        std::printf("info: script failed: %s\n", qUtf8Printable(reply.value("error").toString()));
        return {};
    }
    return QJsonDocument::fromJson(reply.value("result").toString().toUtf8()).array();
}

/// The layout settles between requests, not inside one.
void settle(McpClient &mcp)
{
    QThread::msleep(800);
    mcp.runScript(QStringLiteral("true"));
}

struct DockReading {
    int shown = 0;
    int tooNarrow = 0;
    QString detail;
};

DockReading readDocks(McpClient &mcp)
{
    DockReading r;
    const QJsonArray docks = readArray(mcp, QStringLiteral("app.docks()"));
    for (const QJsonValue &v : docks) {
        const QJsonObject d = v.toObject();
        const bool shown = d.value("shown").toBool();
        const int width = d.value("width").toInt();
        const int minWidth = d.value("minWidth").toInt();
        if (shown) {
            ++r.shown;
            // A PANEL TOO NARROW TO READ IS A MISSING PANEL (the owner's
            // 2026-09-14 screenshot: a 20 px left column showing nothing but
            // the hierarchy rows' lock icons).
            if (minWidth > 0 && width < minWidth) ++r.tooNarrow;
        }
        r.detail += QStringLiteral("%1=%2/%3(min %4) ")
                        .arg(d.value("name").toString()).arg(shown ? 1 : 0)
                        .arg(width).arg(minWidth);
    }
    return r;
}

/// The whole reading for one space, printed and asserted.
void expectEditorPanels(McpClient &mcp, const char *what)
{
    const DockReading r = readDocks(mcp);
    std::printf("info: %s -> %s\n", what, qUtf8Printable(r.detail));
    CHECK(r.shown == 5,
          qUtf8Printable(QStringLiteral("%1: all five editor panels are on screen (got %2)")
                             .arg(QLatin1String(what)).arg(r.shown)));
    CHECK(r.tooNarrow == 0,
          qUtf8Printable(QStringLiteral("%1: none of them is narrower than its own minimum")
                             .arg(QLatin1String(what))));
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

    // ================= RUN 1: the space switches =============================
    {
        QProcess jahshaka;
        QString token;
        QByteArray log;
        const quint16 port = freePort();
        CHECK(spawn(jahshaka, port, &token, &log), "run 1: app booted and printed the MCP token");
        if (token.isEmpty()) return 1;

        McpClient mcp;
        mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
        mcp.token = token;
        mcp.initialize();

        const QJsonObject created = mcp.runScript(QStringLiteral("project.create('SpaceDocks')"));
        CHECK(created.value("ok").toBool(), "run 1: a project is open (editor and player need one)");

        mcp.runScript(QStringLiteral("app.space('editor')"));
        settle(mcp);
        expectEditorPanels(mcp, "the editor, opened");

        // ---- the Player round trip: the owner's report ---------------------
        mcp.runScript(QStringLiteral("app.space('player')"));
        settle(mcp);
        const DockReading player = readDocks(mcp);
        std::printf("info: the player -> %s\n", qUtf8Printable(player.detail));
        CHECK(player.shown == 0, "the player space shows none of the editor's panels");

        mcp.runScript(QStringLiteral("app.space('editor')"));
        settle(mcp);
        expectEditorPanels(mcp, "player -> editor");

        // ---- the Materials round trip: the one that always worked ----------
        mcp.runScript(QStringLiteral("app.space('materials')"));
        settle(mcp);
        mcp.runScript(QStringLiteral("app.space('editor')"));
        settle(mcp);
        expectEditorPanels(mcp, "materials -> editor");

        // ---- quit FROM THE PLAYER: the exit that wrote "no panels" ---------
        mcp.runScript(QStringLiteral("app.space('player')"));
        settle(mcp);
        mcp.runScript(QStringLiteral("app.quit()"));
        const bool exited = jahshaka.waitForFinished(60000);
        if (!exited) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
        CHECK(exited, "run 1: the app quit from the player space (closeEvent saved the layout)");
    }

    // ================= RUN 2: what that exit stored ==========================
    // Same data root, so this run reads the layout the one above wrote.
    {
        QProcess jahshaka;
        QString token;
        QByteArray log;
        const quint16 port = freePort();
        CHECK(spawn(jahshaka, port, &token, &log), "run 2: the app booted again");
        if (token.isEmpty()) return 1;

        McpClient mcp;
        mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
        mcp.token = token;
        mcp.initialize();

        mcp.runScript(QStringLiteral("project.create('SpaceDocks2')"));
        mcp.runScript(QStringLiteral("app.space('editor')"));
        settle(mcp);
        expectEditorPanels(mcp, "the next launch, after quitting from the player");

        mcp.runScript(QStringLiteral("app.quit()"));
        if (!jahshaka.waitForFinished(60000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    }

    std::printf(failures == 0 ? "ui.space_docks: ALL PASS\n" : "ui.space_docks: %d FAILURES\n",
                failures);
    return failures == 0 ? 0 : 1;
}
