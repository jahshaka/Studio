// ui.mcp_prefs — THE CLAUDE/MCP PREFERENCES PAGE DOES NOT WRITE BACK STALE
// STATE (round-2 review of MCP-LOG-1, defect 1 + the pre-existing S-b).
//
// The Preferences dialog is built ONCE and shown many times, and OK saves
// EVERY page. So any control that still shows what it was built with writes
// that value back over whatever changed in between — and two things change
// behind this page's back:
//
//   * a SCRIPT: app.mcpLogging({session:true}) is the agent's own switch, and
//     the next OK on any page silently reverted it;
//   * the COMMAND LINE: --mcp-port starts a server without writing
//     mcp_enabled, so the enable switch showed "off" while the server was
//     serving — and an untouched OK STOPPED it.
//
// The page is compiled here as a slice with link stubs for the heavy
// dependencies (the server, the window, the theme), which is the tests/ui
// house pattern: no Sql, no engine, no MainWindow.

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>

#include "data/settingsmanager.h"
#include "scripting/mcp/mcplog.h"
#include "scripting/mcp/mcpserver.h"
#include "ui/dialogs/preferences/mcpsettingswidget.h"

#include <QAbstractButton>

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", name); }                       \
        else { std::printf("FAIL: %s\n", name); ++failures; }              \
    } while (0)

/// How many times the page stopped the stub server (test_mcp_stubs.cpp).
extern int gStubServerStops;

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir scratch;
    qputenv("JAHSHAKA_DATA_ROOT", scratch.path().toUtf8());
    QApplication app(argc, argv);

    SettingsManager *settings = SettingsManager::getDefaultManager();
    McpServer server(nullptr);

    // ---- a scripted opt-in survives an OK --------------------------------
    {
        McpSettingsWidget page(settings);
        page.wireMcp(&server, nullptr);
        CHECK(!McpLog::instance().sessionRecording(),
              "prefs: session recording starts off");

        // The page is built and left open, and THEN a script turns recording
        // on — exactly the order an agent works in.
        McpLog::instance().setSessionRecording(true);
        page.show();                    // the dialog is raised again
        qApp->processEvents();
        page.saveSettings();            // ...and OK'd on another page's business
        CHECK(McpLog::instance().sessionRecording(),
              "prefs: a scripted app.mcpLogging({session:true}) SURVIVES an OK");

        // And the reverse: the user turning it off in the page wins.
        auto *sessionSwitch = page.findChild<QAbstractButton *>("mcpLogSessions");
        CHECK(sessionSwitch && sessionSwitch->isChecked(),
              "prefs: ...and the switch shows it as on when the page is shown");
        if (sessionSwitch) sessionSwitch->setChecked(false);
        page.saveSettings();
        CHECK(!McpLog::instance().sessionRecording(),
              "prefs: unticking it in the page turns it off");
    }

    // ---- a --mcp-port server survives an untouched OK --------------------
    {
        settings->setValue("mcp_enabled", false);

        // THE REAL ORDER: the Preferences dialog is built at startup, and the
        // server starts afterwards (--mcp-port, or the chat window's Enable
        // button). The page must catch up when it is shown, not when it was
        // made.
        McpSettingsWidget page(settings);
        page.wireMcp(&server, nullptr);
        server.start(8639, nullptr);
        gStubServerStops = 0;
        page.show();
        qApp->processEvents();
        auto *enableSwitch = page.findChild<QAbstractButton *>("mcpEnabled");
        CHECK(enableSwitch && enableSwitch->isChecked(),
              "prefs: the enable switch shows the RUNNING server, not the setting");

        page.saveSettings();
        CHECK(gStubServerStops == 0,
              "prefs: an untouched OK does NOT stop a --mcp-port server");
        CHECK(settings->getValue("mcp_enabled", false).toBool() == false,
              "prefs: ...and does not persist mcp_enabled behind the user's back");

        // The user working the switch is a different thing entirely.
        if (enableSwitch) enableSwitch->setChecked(false);
        page.saveSettings();
        CHECK(gStubServerStops == 1, "prefs: unticking it DOES stop the server");
        CHECK(settings->getValue("mcp_enabled", true).toBool() == false,
              "prefs: ...and that choice IS persisted");
    }

    std::printf(failures ? "ui.mcp_prefs: %d FAILURES\n" : "ui.mcp_prefs: all checks passed\n",
                failures);
    return failures ? 1 : 0;
}
