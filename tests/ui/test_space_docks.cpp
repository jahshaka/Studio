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
#include "../support/mcpharness.h"

#include <QHash>
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

QJsonObject readObject(McpClient &mcp, const QString &expression)
{
    const QJsonObject reply = mcp.runScript(QStringLiteral("JSON.stringify(%1)").arg(expression));
    if (!reply.value("ok").toBool()) {
        std::printf("info: script failed: %s\n", qUtf8Printable(reply.value("error").toString()));
        return {};
    }
    return QJsonDocument::fromJson(reply.value("result").toString().toUtf8()).object();
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
    QHash<QString, QJsonObject> byName;

    bool isShown(const char *name) const { return byName.value(name).value("shown").toBool(); }
    bool isCurrent(const char *name) const { return byName.value(name).value("current").toBool(); }
    bool isTabbed(const char *name) const { return byName.value(name).value("tabbed").toBool(); }
};

/// THE CONSOLE IS THE SIXTH DOCK (lane SPACE-2) and it is CLOSED by default, so
/// "all the editor's panels are on screen" counts the five the editor is made
/// of. Ctrl+` decides the console, and it is asserted where that is the subject.
const char *kConsoleDock = "scriptConsoleDock";

DockReading readDocks(McpClient &mcp)
{
    DockReading r;
    const QJsonArray docks = readArray(mcp, QStringLiteral("app.docks()"));
    for (const QJsonValue &v : docks) {
        const QJsonObject d = v.toObject();
        const bool shown = d.value("shown").toBool();
        const int width = d.value("width").toInt();
        const int minWidth = d.value("minWidth").toInt();
        r.byName.insert(d.value("name").toString(), d);
        if (shown && d.value("name").toString() != QLatin1String(kConsoleDock)) {
            ++r.shown;
            // A PANEL TOO NARROW TO READ IS A MISSING PANEL (the owner's
            // 2026-09-14 screenshot: a 20 px left column showing nothing but
            // the hierarchy rows' lock icons).
            if (minWidth > 0 && width < minWidth) ++r.tooNarrow;
        }
        r.detail += QStringLiteral("%1=%2/%3(min %4)%5 ")
                        .arg(d.value("name").toString()).arg(shown ? 1 : 0)
                        .arg(width).arg(minWidth)
                        .arg(d.value("tabbed").toBool()
                                 ? (d.value("current").toBool() ? "[front tab]" : "[behind a tab]")
                                 : "");
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
    // RUN 1 STARTS FROM THE DEFAULT LAYOUT (lane SPACE-2). The suite's data
    // root is PERSISTENT — that is the whole point of run 2, which reads what
    // run 1's exit stored — so without this the first run inherits the layout
    // the last run of the suite left behind, and a build that once wrote a bad
    // one keeps failing against it forever (measured while this lane was being
    // written: a stored "console alone" layout survived the fix). Run 2 is the
    // one that must NOT wipe: it is the restart.
    for (const QString &ini : testsupport::settingsFilesForSpawnedApp(
             QStringLiteral(JAHSHAKA_BINARY))) {
        QSettings settings(ini, QSettings::IniFormat);
        settings.remove(QStringLiteral("viewportDockState"));
        settings.sync();
    }

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
        mcp.clientName = QStringLiteral("space-docks-test");
        // app.space() brings up a second View — the request that outlived the old
        // 30 s default under four Vulkan instances (ledger 404). A 600 s suite can
        // afford to wait far longer than the harness default before it gives up.
        mcp.transferTimeoutMs = 240000;
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

        // ---- THE BOTTOM AREA IS ONE TAB BAR (lane SPACE-2) ------------------
        // The owner's 2026-09-14 report: "the timeline widget was in the bottom
        // where Assets is and is gone; it should have its own tab". It was
        // never closed — it was the tab behind the asset browser, reachable
        // only through Qt's default SOUTH dock tab bar, a ~20 px strip along
        // the very bottom edge of the window, under a tray that carried a tab
        // bar of its own at the top. "Open" and "reachable" are two different
        // numbers, which is why app.docks() reports `tabbed` and `current`.
        {
            const DockReading bottom = readDocks(mcp);
            std::printf("info: the bottom area -> %s\n", qUtf8Printable(bottom.detail));
            CHECK(bottom.isShown("assetDock") && bottom.isShown("animationDock"),
                  "the bottom area: Assets and the Timeline are both OPEN");
            CHECK(bottom.isTabbed("assetDock") && bottom.isTabbed("animationDock"),
                  "…as TABS of one group — the Timeline has its own tab");
            CHECK(bottom.isCurrent("assetDock") && !bottom.isCurrent("animationDock"),
                  "…and the editor opens on Assets, with the Timeline behind it");
            CHECK(!bottom.isShown(kConsoleDock),
                  "…and no Console tab until Ctrl+` asks for one");

            const QJsonObject tray = readObject(mcp, QStringLiteral("editor.trayState()"));
            CHECK(tray.value("tab").toString() == QLatin1String("assets"),
                  "editor.trayState() agrees: the tab in front is Assets");
            CHECK(tray.value("tabs").toArray().size() == 2,
                  "…and the bar has the two tabs the panels there are");

            // THE TIMELINE IS REACHABLE BY ITS TAB.
            const QJsonObject onTimeline = readObject(mcp,
                QStringLiteral("editor.tray({tab: 'timeline'})"));
            CHECK(onTimeline.value("tab").toString() == QLatin1String("timeline"),
                  "the Timeline tab comes to the front when it is asked for");
            // …and which tab is in front SURVIVES A SPACE ROUND TRIP: showing a
            // tabified dock raises it, so the visibility pass used to hand the
            // front tab to whichever dock it showed last.
            mcp.runScript(QStringLiteral("app.space('player')"));
            settle(mcp);
            mcp.runScript(QStringLiteral("app.space('editor')"));
            settle(mcp);
            CHECK(readDocks(mcp).isCurrent("animationDock"),
                  "…and it is still the front tab after a trip to the Player and back");

            // A REAL CLOSE STAYS CLOSED — the title-bar X's own gesture, which
            // is what editor.panel({open: false}) and the Toggle Widgets
            // dialog's buttons call (one function, lane SPACE-2).
            const QJsonObject closed = readObject(mcp,
                QStringLiteral("editor.panel({name: 'timeline', open: false})"));
            CHECK(!closed.value("open").toBool() && !closed.value("tabbed").toBool(),
                  "closing the Timeline takes its tab out of the bar");
            mcp.runScript(QStringLiteral("app.space('player')"));
            settle(mcp);
            mcp.runScript(QStringLiteral("app.space('editor')"));
            settle(mcp);
            const DockReading afterClose = readDocks(mcp);
            std::printf("info: Timeline closed, player -> editor -> %s\n",
                        qUtf8Printable(afterClose.detail));
            CHECK(!afterClose.isShown("animationDock"),
                  "…and it stays closed across a space round trip");
            CHECK(afterClose.isShown("assetDock") && afterClose.isCurrent("assetDock"),
                  "…while the asset browser beside it is untouched");

            // RESTORE ALL BRINGS IT BACK (the dialog's button, same function).
            const QJsonObject reopened = readObject(mcp,
                QStringLiteral("editor.panel({name: 'timeline', open: true})"));
            CHECK(reopened.value("open").toBool() && reopened.value("current").toBool(),
                  "asking for the Timeline again brings it back AND to the front");

            // THE CONSOLE IS THE THIRD TAB.
            const QJsonObject withConsole = readObject(mcp,
                QStringLiteral("editor.tray({console: true})"));
            CHECK(withConsole.value("tabs").toArray().size() == 3
                      && withConsole.value("tab").toString() == QLatin1String("console"),
                  "Ctrl+`'s verb adds the Console as a THIRD tab, in front");
            CHECK(readDocks(mcp).isShown(kConsoleDock),
                  "…and app.docks() reports the console dock as open");
            // Left OPEN, with Assets in front, for the restart in run 2.
            mcp.runScript(QStringLiteral("editor.tray({tab: 'assets'})"));
            settle(mcp);
        }

        // ---- immersive fullscreen keeps its chrome off ---------------------
        // F11 hides the docks INSIDE the editor space (EDITOR_SHORTCUTS_SPEC
        // §3), so a space round trip taken while it is on must not put them
        // back on top of it — and leaving fullscreen must still bring back
        // exactly what was there before.
        // A KNOWN front tab going in, so "it came back to it" means something.
        mcp.runScript(QStringLiteral("editor.tray({tab: 'timeline'})"));
        settle(mcp);
        mcp.runScript(QStringLiteral("editor.fullscreen(true)"));
        settle(mcp);
        const DockReading fullscreen = readDocks(mcp);
        std::printf("info: F11 on -> %s\n", qUtf8Printable(fullscreen.detail));
        CHECK(fullscreen.shown == 0, "immersive fullscreen hides the editor's panels");
        mcp.runScript(QStringLiteral("app.space('player')"));
        settle(mcp);
        mcp.runScript(QStringLiteral("app.space('editor')"));
        settle(mcp);
        const DockReading backInside = readDocks(mcp);
        std::printf("info: player -> editor inside F11 -> %s\n", qUtf8Printable(backInside.detail));
        CHECK(backInside.shown == 0,
              "a space round trip INSIDE fullscreen leaves the chrome off");
        mcp.runScript(QStringLiteral("editor.fullscreen(false)"));
        settle(mcp);
        expectEditorPanels(mcp, "leaving fullscreen");
        // …ON THE TAB F11 INTERRUPTED (round 2). Leaving fullscreen re-shows
        // the bottom docks, and showing a tabified dock raises it — so without
        // a front-tab restore the trip home lands on whichever dock the list
        // shows last (the Console when it is open, the Timeline otherwise),
        // which is the same defect the space switch had.
        const DockReading afterF11 = readDocks(mcp);
        std::printf("info: after F11 -> %s\n", qUtf8Printable(afterF11.detail));
        CHECK(afterF11.isCurrent("animationDock"),
              "leaving fullscreen comes back to the tab that was in front when F11 was pressed");
        mcp.runScript(QStringLiteral("editor.tray({tab: 'assets'})"));
        settle(mcp);

        // ---- quit FROM THE PLAYER: the exit that wrote "no panels" ---------
        mcp.runScript(QStringLiteral("app.space('player')"));
        settle(mcp);
        mcp.quit();
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
        mcp.clientName = QStringLiteral("space-docks-test");
        mcp.transferTimeoutMs = 240000;
        mcp.initialize();

        // THE BOOT STATE, BEFORE ANY SPACE SWITCH (round-2 review). The
        // scripted/MCP boot shows the editor page directly (beginEngineSelftest)
        // and never calls switchSpace, so this reading is the one a script or an
        // MCP client sees — and it is the one a visibility rule keyed on
        // `currentSpace` (still DESKTOP here) got wrong, hiding all five docks
        // one event-loop turn into every session that had a stored layout.
        settle(mcp);
        expectEditorPanels(mcp, "the scripted boot, before any project or space switch");

        mcp.runScript(QStringLiteral("project.create('SpaceDocks2')"));
        mcp.runScript(QStringLiteral("app.space('editor')"));
        settle(mcp);
        expectEditorPanels(mcp, "the next launch, after quitting from the player");

        // ---- and the bottom area comes back the way it was left (SPACE-2) ---
        // Run 1 quit with the console open and Assets in front. The Timeline is
        // the case that mattered: a tab that is not in front is not hidden by
        // Qt, so the seed that reads the restored layout must NOT record it as
        // a panel the user closed (it did not — this pins it end to end).
        {
            const DockReading bottom = readDocks(mcp);
            std::printf("info: the bottom area after a restart -> %s\n",
                        qUtf8Printable(bottom.detail));
            CHECK(bottom.isShown("animationDock") && !bottom.isCurrent("animationDock"),
                  "the restart: the Timeline is still a TAB — open, behind the front one");
            CHECK(bottom.isCurrent("assetDock"),
                  "the restart: …and the tab that was in front still is");
            CHECK(bottom.isShown(kConsoleDock),
                  "the restart: a console left open comes back as a tab");
            const QJsonObject tray = readObject(mcp, QStringLiteral("editor.trayState()"));
            CHECK(tray.value("tabs").toArray().size() == 3,
                  "the restart: all three tabs are in the bar");
        }

        mcp.quit();
        if (!jahshaka.waitForFinished(60000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    }

    std::printf(failures == 0 ? "ui.space_docks: ALL PASS\n" : "ui.space_docks: %d FAILURES\n",
                failures);
    return failures == 0 ? 0 : 1;
}
