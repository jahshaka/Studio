// theme.sheets — THE LIVE THEME WALK (theme sweep, lane 16; platform audit
// F-S2 and its missing guard, F-X3).
//
// Under the default Qlementine theme a widget stylesheet is a defect: ANY
// non-empty sheet interposes QStyleSheetStyle over the QStyle for that widget
// and its whole subtree (THEME_AUDIT.md §3) — the dark-on-dark text and
// platform-light panes the sweep removed, and (it turned out) a hiding place
// for real bugs: the sheets had masked a QProxyStyle that owned the app style,
// Qlementine's stale tab metrics and its icon-over-caption layout.
//
// So this suite boots the REAL app and walks every widget alive through
// app.styleSheets() at each stop of a tour — Desktop, a new project, the
// editor with the tray on Assets and on Console and a selection in the
// properties, Materials, Assets, Avatar, Player, Publish, and every dialog
// app.dialogs() can open — and asserts that every non-empty sheet under
// Qlementine is either the theme's own (ThemeManager hands it out and the walk
// classifies it 'theme') or on the ALLOWLIST below, each entry with its reason.
//
// Then it relaunches the app on the archived Classic theme and spot-checks
// that Classic still carries its archive BIT-FOR-BIT: the root sheets the
// sweep moved out of .ui files land on their widgets byte-identical to the
// StyleSheet:: getters (compiled into this suite).
//
// Why the real binary over MCP: the tour needs pages that only exist in a
// shown window with a turning event loop (the ui.column_law harness).
#include "../shutdown/mcpharness.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QThread>

#include "ui/style/stylesheet.h"

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", qUtf8Printable(QString(msg))); \
    else { std::printf("FAIL: %s\n", qUtf8Printable(QString(msg))); ++failures; } } while (0)

namespace {

// THE ALLOWLIST — raw (non-ThemeManager) sheets Qlementine may carry, and why.
// Matched on the TOP-LEVEL window's objectName ("window" in the walk).
struct Allowed { const char *window; const char *reason; };
const Allowed kAllowlist[] = {
    // The Claude chat popup is a self-contained designed surface — chat
    // bubbles, a banner, an input well — explicitly identical under both
    // themes and opted out of both cascades (ui/windows/claudechatwindow.h).
    // Its sheet is the content's look, not a stock-widget skin.
    { "claudeChatRoot", "Claude chat popup: self-contained bubble/banner design, same in both themes" },
};

const char *allowedReason(const QJsonObject &sheet)
{
    const QString window = sheet.value("window").toString();
    for (const Allowed &a : kAllowlist)
        if (window == QLatin1String(a.window)) return a.reason;
    return nullptr;
}

QJsonObject readObject(McpClient &mcp, const QString &expression)
{
    const QJsonObject reply = mcp.runScript(QStringLiteral("JSON.stringify(%1)").arg(expression));
    if (!reply.value("ok").toBool()) {
        std::printf("info: script failed: %s -> %s\n", qUtf8Printable(expression),
                    qUtf8Printable(reply.value("error").toString()));
        return {};
    }
    return QJsonDocument::fromJson(reply.value("result").toString().toUtf8()).object();
}

QJsonArray readArray(McpClient &mcp, const QString &expression)
{
    const QJsonObject reply = mcp.runScript(QStringLiteral("JSON.stringify(%1)").arg(expression));
    if (!reply.value("ok").toBool()) return {};
    return QJsonDocument::fromJson(reply.value("result").toString().toUtf8()).array();
}

void settle(McpClient &mcp, int ms = 800)
{
    QThread::msleep(ms);
    mcp.runScript(QStringLiteral("true"));
}

/// One stop of the tour: every widget alive, every raw sheet accounted for.
int walkQlementine(McpClient &mcp, const QString &stop, int *themeOwned)
{
    const QJsonObject walk = readObject(mcp, QStringLiteral("app.styleSheets()"));
    if (walk.isEmpty()) { CHECK(false, stop + ": the walk answered"); return -1; }
    int unexplained = 0;
    for (const QJsonValue &v : walk.value("sheets").toArray()) {
        const QJsonObject sheet = v.toObject();
        if (sheet.value("owner").toString() == QLatin1String("theme")) continue;
        if (allowedReason(sheet)) continue;
        ++unexplained;
        std::printf("  RAW %s [%s] window=%s: %s\n",
                    qUtf8Printable(sheet.value("path").toString()),
                    qUtf8Printable(sheet.value("class").toString()),
                    qUtf8Printable(sheet.value("window").toString()),
                    qUtf8Printable(sheet.value("sheet").toString()));
    }
    std::printf("info: %-22s widgets %4d, styled %3d (theme %3d, raw %3d)\n", qUtf8Printable(stop),
                walk.value("widgets").toInt(), walk.value("styled").toInt(),
                walk.value("themeOwned").toInt(), walk.value("raw").toInt());
    if (themeOwned) *themeOwned += walk.value("themeOwned").toInt();
    CHECK(unexplained == 0, stop + ": no raw stylesheet outside the allowlist (Qlementine)");
    return unexplained;
}

bool bootApp(QProcess &jahshaka, McpClient &mcp)
{
    QString token;
    QByteArray log;
    const quint16 port = freePort();
    if (!spawn(jahshaka, port, &token, &log) || token.isEmpty()) return false;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.initialize();
    return true;
}

void quitApp(QProcess &jahshaka, McpClient &mcp)
{
    mcp.runScript(QStringLiteral("app.quit()"));
    if (!jahshaka.waitForFinished(30000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
}

void setTheme(const QString &id)
{
    for (const QString &ini : testsupport::settingsFilesForSpawnedApp(QStringLiteral(JAHSHAKA_BINARY))) {
        QSettings s(ini, QSettings::IniFormat);
        if (id.isEmpty()) s.remove(QStringLiteral("appearance/theme"));
        else s.setValue(QStringLiteral("appearance/theme"), id);
        s.sync();
    }
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

    // ======================= 1. Qlementine: the tour ===========================
    setTheme(QString());   // no key: the default theme
    {
        QProcess jahshaka;
        McpClient mcp;
        CHECK(bootApp(jahshaka, mcp), "app booted on the default theme");
        if (jahshaka.state() != QProcess::Running) return 1;

        const QJsonObject theme = readObject(mcp, QStringLiteral("app.theme()"));
        CHECK(theme.value("id").toString() == QLatin1String("qlementine-dark")
                  && !theme.value("classic").toBool(),
              "the default theme is Qlementine Dark");

        int themeOwned = 0;
        settle(mcp, 1500);
        walkQlementine(mcp, QStringLiteral("desktop (boot)"), &themeOwned);

        CHECK(mcp.runScript(QStringLiteral("project.create('ThemeSheets')")).value("ok").toBool(),
              "a project is open (editor, player and materials need one)");
        settle(mcp, 1500);
        mcp.runScript(QStringLiteral("app.space('desktop')"));
        settle(mcp);
        walkQlementine(mcp, QStringLiteral("desktop (project)"), &themeOwned);

        mcp.runScript(QStringLiteral("app.space('editor')"));
        mcp.runScript(QStringLiteral("editor.tray({tab: 'assets'})"));
        settle(mcp, 1500);
        walkQlementine(mcp, QStringLiteral("editor / tray assets"), &themeOwned);
        mcp.runScript(QStringLiteral("editor.tray({tab: 'console'})"));
        settle(mcp);
        walkQlementine(mcp, QStringLiteral("editor / tray console"), &themeOwned);
        // a selection populates the property panels (blades, value rows)
        mcp.runScript(QStringLiteral(
            "var r = editor.outlinerRows(); if (r.length) editor.select(r[r.length - 1].id); true"));
        settle(mcp);
        walkQlementine(mcp, QStringLiteral("editor / selection"), &themeOwned);

        for (const char *space : { "materials", "assets", "avatar", "player", "publish" }) {
            const QJsonObject switched =
                mcp.runScript(QStringLiteral("app.space('%1')").arg(QLatin1String(space)));
            CHECK(switched.value("ok").toBool(), QStringLiteral("%1 page opens").arg(space));
            settle(mcp, 1200);
            walkQlementine(mcp, QString::fromLatin1(space), &themeOwned);
        }
        mcp.runScript(QStringLiteral("app.space('desktop')"));
        settle(mcp);

        const QJsonArray dialogs = readArray(mcp, QStringLiteral("app.dialogs()"));
        CHECK(dialogs.size() >= 10, QStringLiteral("app.dialogs() lists the app's dialogs (%1)")
                                        .arg(dialogs.size()));
        for (const QJsonValue &d : dialogs) {
            const QString name = d.toObject().value("name").toString();
            const QJsonObject opened =
                readObject(mcp, QStringLiteral("app.dialog('%1')").arg(name));
            CHECK(opened.value("open").toBool(), QStringLiteral("dialog %1 opens").arg(name));
            settle(mcp, 500);
            walkQlementine(mcp, QStringLiteral("dialog ") + name, &themeOwned);
            mcp.runScript(QStringLiteral("app.dialog('%1', false)").arg(name));
            settle(mcp, 300);
        }
        CHECK(themeOwned > 0, "the walk sees the theme's own chrome sheets (it is really walking)");
        const QJsonObject mainSheet = readObject(
            mcp, QStringLiteral("app.styleSheets({window: 'MainWindow'}).sheets"
                                ".filter(function(s){ return s.path === 'MainWindow'; })[0] || {}"));
        CHECK(mainSheet.isEmpty(),
              "the main window itself carries no sheet (the 5 KB mainwindow.ui root sheet is Classic-only)");
        quitApp(jahshaka, mcp);
    }

    // ======================= 2. Classic: still bit-for-bit =====================
    setTheme(QStringLiteral("classic"));
    {
        QProcess jahshaka;
        McpClient mcp;
        CHECK(bootApp(jahshaka, mcp), "app booted on the archived Classic theme");
        if (jahshaka.state() != QProcess::Running) { setTheme(QString()); return 1; }
        const QJsonObject theme = readObject(mcp, QStringLiteral("app.theme()"));
        CHECK(theme.value("classic").toBool(), "the persisted Classic choice took effect");
        mcp.runScript(QStringLiteral("project.create('ThemeSheetsClassic')"));
        settle(mcp, 1500);
        mcp.runScript(QStringLiteral("app.space('editor')"));
        settle(mcp, 1200);

        const QJsonObject walk = readObject(mcp, QStringLiteral("app.styleSheets({full: true})"));
        CHECK(walk.value("styled").toInt() > 100,
              QStringLiteral("Classic carries its archived sheets (%1 styled widgets)")
                  .arg(walk.value("styled").toInt()));

        // The sheets the sweep moved OUT of .ui files land on their widgets
        // byte-identical to the getters that now hold them.
        StyleSheet::setClassicThemeActive(true);
        struct Spot { const char *name; const char *cls; QString expected; };
        const Spot spots[] = {
            { "MainWindow", "MainWindow", StyleSheet::MainWindowRoot() },
            { "ProjectManager", "ProjectManager", StyleSheet::ProjectManagerRoot() },
            { "AnimationWidget", "AnimationWidget", StyleSheet::AnimationWidgetRoot() },
            { "SceneHierarchyWidget", "SceneHierarchyWidget", StyleSheet::SceneHierarchyRoot() },
            { "AccordianBladeWidget", "AccordianBladeWidget", StyleSheet::AccordionBladeRoot() },
        };
        for (const Spot &spot : spots) {
            bool found = false, identical = false;
            for (const QJsonValue &v : walk.value("sheets").toArray()) {
                const QJsonObject s = v.toObject();
                if (s.value("name").toString() != QLatin1String(spot.name)
                    && s.value("class").toString() != QLatin1String(spot.cls))
                    continue;
                if (s.value("sheet").toString() == spot.expected) { found = identical = true; break; }
                found = true;
            }
            CHECK(!spot.expected.isEmpty() && found && identical,
                  QStringLiteral("classic: %1 carries its archived sheet byte-for-byte").arg(spot.name));
        }
        quitApp(jahshaka, mcp);
    }
    setTheme(QString());

    std::printf(failures == 0 ? "theme.sheets: ALL PASS\n" : "theme.sheets: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
