// scale.library — W14: STUDIO'S O(N) LIBRARY (lane D1-SCALE-FIXTURES, brief §4.3/§4.4).
//
// Anchors (re-verified at d-build 129c9e82c): src/ui/pages/assetview.cpp:855-876 (the
// Library page builds a tile per asset and inflates every thumbnail, `image.loadFromData`,
// synchronously while the page is made); src/services/projectservice.cpp:176-186
// (resolveProjectGuid: `db->fetchProjects(0)` loads EVERY project row, its scene and its
// thumbnail BLOBs, to find one guid); src/data/database/database.cpp:876-880 (the whole
// database carries two CREATE INDEX statements).
//
// THE LIBRARY FIXTURE: a data root holding 10,000 library assets and 500 projects, made
// THROUGH THE PRODUCT'S DOORS — every asset an `assets.importFile` of a distinct small PNG
// (so each is a LIBRARY row with a real thumbnail, minted by the import pipeline — never a
// raw insert: since ASSETS-SCOPE-1 a project's own rows are a different shape), and every
// project through a project door (25 `project.create`, the rest `project.importArchive`
// of one of them — see kCreates). It is generated
// ONCE into the build tree (library-template/, marked complete by a file) and COPIED for
// each run, because making it is minutes of the app's own work; the generation time is
// printed when it happens.
//
// THE MEASUREMENTS (the numbers CREATE-GAP-1's suite takes, at this size), each against
// an EMPTY-library control in the same run: the boot to the MCP answering (the window is
// up and the Desktop built), the Desktop grid's build (desktop.gridStats), a project open
// and a project create (the UI-thread's worst gap from the heartbeat probe, and the
// verb's own ledger total), and a tray populate + a library list (the verbs' wall time,
// measured on the script thread around one hop). No bar: `target:` lines (label
// scale-target). Engine up on the rig display: the suite spawns the real binary.
#include "../support/mcpharness.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>

#include <cstdio>

using namespace mcpharness;

static int failures = 0;
#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
        std::fflush(stdout);                                                      \
    } while (0)

static const int kAssets = 10000;
static const int kProjects = 500;
static const QString kBase = QStringLiteral(SCALE_LIBRARY_DIR);

static void target(double v, const char *unit, const QString &what)
{
    std::printf("target: %.4f (bar none yet) W14 %s: %s\n", v, unit, qPrintable(what));
    std::fflush(stdout);
}

struct App {
    QProcess proc;
    McpClient mcp;
    QByteArray log;
    double bootMs = -1;
};

static bool launch(App &app, const QString &dataRoot)
{
    QDir().mkpath(dataRoot);
    {
        QSettings s(QDir(dataRoot).filePath("jahsettings.ini"), QSettings::IniFormat);
        s.setValue(QStringLiteral("auto_save"), true);
        s.sync();
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("JAHSHAKA_DATA_ROOT"), dataRoot);
    env.insert(QStringLiteral("HOME"), dataRoot + "/home");
    QDir().mkpath(dataRoot + "/home");
    app.proc.setProcessEnvironment(env);
    const quint16 port = freePort();
    QString token;
    QElapsedTimer t;
    t.start();
    if (!spawn(app.proc, port, &token, &app.log, QStringList(), 600000)) return false;
    app.bootMs = double(t.elapsed());
    app.mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    app.mcp.token = token;
    app.mcp.clientName = QStringLiteral("scale-library");
    app.mcp.transferTimeoutMs = 600000;
    app.mcp.initialize();
    return true;
}

static void quit(App &app)
{
    app.mcp.quit();
    if (!app.proc.waitForFinished(60000)) { app.proc.kill(); app.proc.waitForFinished(5000); }
}

static QJsonValue eval(App &app, const QString &script)
{
    return app.mcp.runScript(script).value("result");
}

/// THE FIXTURE, generated once — and RESUMABLE: a run that stopped part-way (a crash,
/// a timeout) continues from the counts the library already holds.
///
/// THE PROJECTS: the first kCreates through project.create (a world each: floor tile,
/// sun, sky, thumbnail), the rest through project.importArchive of one of them (the
/// archive door: a new guid, its own rows and thumbnail, ~10 ms each) and renamed. Not
/// 500 creates: a create loop over this library crashed the app at the 33rd create
/// (SIGSEGV in MainWindow::applyDockVisibilityForSpace under startCreateRun's reveal —
/// spikes/d1-scale-fixtures/library-crash/), and 500 creates are ~8 minutes of the app.
static const int kCreates = 25;

static bool ensureTemplate()
{
    const QString tmpl = kBase + "/library-template";
    if (QFileInfo::exists(tmpl + "/.complete")) return true;
    const QString src = kBase + "/library-src";
    QDir().mkpath(src);
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < kAssets; ++i) {
        const QString path = src + QStringLiteral("/scale-asset-%1.png").arg(i, 5, 10, QLatin1Char('0'));
        if (QFileInfo::exists(path)) continue;
        QImage img(64, 64, QImage::Format_RGB888);
        const QColor a((i * 97) % 256, (i * 57) % 256, (i * 31) % 256), b((i * 13) % 256, 255 - (i * 7) % 256, (i * 3) % 256);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) img.setPixelColor(x, y, ((x / 8 + y / 8 + i) & 1) ? a : b);
        img.save(path);
    }
    std::printf("library: %d source PNGs ready (%.1f s)\n", kAssets, t.elapsed() / 1000.0);
    std::fflush(stdout);
    App app;
    if (!launch(app, tmpl)) { std::printf("FAIL: the generator's app did not boot\n%s\n", app.log.constData()); return false; }
    // RESUME: the library's own counts (a row per imported file: the sources are imported
    // in index order, so the count IS the next index).
    int have = eval(app, QStringLiteral("assets.list().length")).toInt();
    t.restart();
    for (int a = have; a < kAssets; a += 250) {
        const int b = std::min(kAssets, a + 250);
        const QJsonObject r = app.mcp.runScript(QStringLiteral(
            "var n=0;for(var i=%1;i<%2;i++){var s=''+i;while(s.length<5)s='0'+s;"
            "if(assets.importFile('%3/scale-asset-'+s+'.png'))n++;} n").arg(a).arg(b).arg(src));
        if (r.value("result").toInt() != b - a) {
            std::printf("FAIL: an import batch at %d: %s\n", a, QJsonDocument(r).toJson(QJsonDocument::Compact).constData());
            quit(app);
            return false;
        }
    }
    const double importS = t.elapsed() / 1000.0;
    const int imported = kAssets - have;
    int projects = eval(app, QStringLiteral("project.list().length")).toInt();
    t.restart();
    int created = 0;
    for (; projects < kCreates; ++projects, ++created)
        app.mcp.runScript(QStringLiteral("project.create('Scale project %1'); 1").arg(projects));
    const double createS = t.elapsed() / 1000.0;
    t.restart();
    int archived = 0;
    if (projects < kProjects) {
        const QString seed = kBase + "/library-seed.jaf";
        QFile::remove(seed);
        const QJsonValue ex = eval(app, QStringLiteral(
            "(function(){var l=project.list();project.open(l[0].guid);return project.exportArchive('%1')})()").arg(seed));
        if (!QFileInfo::exists(seed)) {
            std::printf("FAIL: the seed archive: %s\n", QJsonDocument(ex.toObject()).toJson(QJsonDocument::Compact).constData());
            quit(app);
            return false;
        }
        for (; projects < kProjects; projects += 25) {
            const int b = std::min(kProjects, projects + 25);
            app.mcp.runScript(QStringLiteral(
                "for(var i=%1;i<%2;i++){var r=project.importArchive('%3');project.rename(r.guid,'Scale project '+i)} 1")
                                  .arg(projects).arg(b).arg(seed));
            archived += b - projects;
        }
    }
    const double archiveS = t.elapsed() / 1000.0;
    const int assets = eval(app, QStringLiteral("assets.list().length")).toInt();
    projects = eval(app, QStringLiteral("project.list().length")).toInt();
    quit(app);
    std::printf("library: GENERATED — %d assets imported now through assets.importFile in %.1f s (%.1f ms each); "
                "%d projects created in %.1f s, %d through project.importArchive in %.1f s; the library holds %d "
                "assets and %d projects\n",
                imported, importS, imported ? 1000.0 * importS / imported : 0.0, created, createS, archived, archiveS,
                assets, projects);
    std::fflush(stdout);
    if (assets < kAssets || projects < kProjects) {
        std::printf("FAIL: the template holds %d assets and %d projects\n", assets, projects);
        return false;
    }
    QFile f(tmpl + "/.complete");
    f.open(QIODevice::WriteOnly);
    f.write(QByteArray::number(QDateTime::currentSecsSinceEpoch()));
    return true;
}

struct Arm {
    double bootMs = -1, gridMs = -1, gridDecodes = -1, gridTiles = -1;
    double openGap = -1, openMs = -1, createGap = -1, createMs = -1, trayMs = -1, trayCount = -1, listMs = -1,
           listCount = -1;
};

static double gapAround(App &app, const QString &script, double *ledgerTotal)
{
    app.mcp.runScript(QStringLiteral("app.heartbeat(0)"));
    app.mcp.runScript(QStringLiteral("app.heartbeat(100)"));
    app.mcp.runScript(QStringLiteral("editor.frame(2)"));
    app.mcp.runScript(script);
    app.mcp.runScript(QStringLiteral("editor.frame(4)"));
    const double gap = app.mcp.runScript(QStringLiteral("app.heartbeatStats()")).value("result").toObject()
                           .value("maxGapMs").toDouble();
    if (ledgerTotal) {
        const QJsonArray t = eval(app, QStringLiteral("app.openTimings()")).toArray();
        *ledgerTotal = t.isEmpty() ? -1 : t.at(0).toObject().value("ms").toDouble();
    }
    app.mcp.runScript(QStringLiteral("app.heartbeat(0)"));
    return gap;
}

static bool runArm(const char *label, const QString &dataRoot, Arm &a)
{
    App app;
    if (!launch(app, dataRoot)) { std::printf("FAIL: [%s] boot\n", label); return false; }
    a.bootMs = app.bootMs;
    app.mcp.runScript(QStringLiteral("editor.frame(10)"));

    // THE OPEN: the first project the library lists that is not open.
    const QString guid = eval(app, QStringLiteral(
        "(function(){var l=project.list();var c=project.current();for(var i=0;i<l.length;i++)"
        "if(!c||l[i].guid!==c.guid)return l[i].guid;return ''})()")).toString();
    if (!guid.isEmpty())
        a.openGap = gapAround(app, QStringLiteral("project.open('%1')").arg(guid), &a.openMs);
    // THE TRAY: what the open project's tray shows, one hop, timed on the script thread.
    const QJsonArray tray = eval(app, QStringLiteral(
        "(function(){var t=Date.now();var n=editor.trayAssets().length;return [Date.now()-t,n]})()")).toArray();
    if (tray.size() == 2) { a.trayMs = tray.at(0).toDouble(); a.trayCount = tray.at(1).toDouble(); }
    const QJsonArray list = eval(app, QStringLiteral(
        "(function(){var t=Date.now();var n=assets.list().length;return [Date.now()-t,n]})()")).toArray();
    if (list.size() == 2) { a.listMs = list.at(0).toDouble(); a.listCount = list.at(1).toDouble(); }
    // THE CREATE (CREATE-GAP-1's measurement): over this library.
    a.createGap = gapAround(app, QStringLiteral("project.create('Scale create %1')")
                                     .arg(QDateTime::currentMSecsSinceEpoch()), &a.createMs);
    // THE DESKTOP GRID: a driven session boots with the Desktop never SHOWN, so its grid
    // (a tile per project, a thumbnail decode each) is first built when a create's close
    // passes through the Desktop — INSIDE the create above. Its own ledger, read after.
    const QJsonObject g = eval(app, QStringLiteral("desktop.gridStats()")).toObject();
    a.gridMs = g.value("lastBuildMs").toDouble();
    a.gridDecodes = g.value("lastBuildDecodes").toDouble();
    a.gridTiles = g.value("tiles").toDouble();
    std::printf("   [%s] desktop.gridStats() after the create %s\n", label,
                QJsonDocument(g).toJson(QJsonDocument::Compact).constData());
    quit(app);
    // THE APP'S OWN OUTPUT, kept beside the run (an Xid or a crash is triaged from it).
    QFile out(kBase + QStringLiteral("/library-%1-app.log").arg(QString::fromLatin1(label).section('+', 0, 0)));
    if (out.open(QIODevice::WriteOnly)) out.write(app.log + app.proc.readAll());
    std::printf("W14 [%-7s] boot->MCP %8.0f ms | grid build (inside the create) %7.1f ms, %5.0f decodes, %5.0f tiles | open: worst UI gap "
                "%7.1f ms, ledger %7.1f ms | tray %4.0f ms (%3.0f tiles) | assets.list %6.0f ms (%5.0f rows) | "
                "create: worst UI gap %7.1f ms, ledger %7.1f ms\n",
                label, a.bootMs, a.gridMs, a.gridDecodes, a.gridTiles, a.openGap, a.openMs, a.trayMs, a.trayCount,
                a.listMs, a.listCount, a.createGap, a.createMs);
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication qapp(argc, argv);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // a line at a time: the log is read while it runs
    if (!ensureTemplate()) return 1;
    // THE RUN'S COPY of the template, and an EMPTY control root — both fresh.
    const QString full = kBase + "/library-run", empty = kBase + "/library-empty";
    QDir(full).removeRecursively();
    QDir(empty).removeRecursively();
    QElapsedTimer t;
    t.start();
    const int cp = QProcess::execute(QStringLiteral("cp"), { QStringLiteral("-a"), kBase + "/library-template", full });
    CHECK(cp == 0, "the template copied (%.1f s)", t.elapsed() / 1000.0);
    // ONE CACHE STATE FOR BOTH ARMS: the template carries the generator run's shader
    // cache (and the driver's cache under its HOME), so the EMPTY control is given the
    // same copies — a bare control root pays the PSO compile storm the library arm does
    // not, and the difference would be the cache, not the library (DOCS/traps/
    // GATE_AND_RIG.md: a first-seconds measurement is the shader storm).
    QDir().mkpath(empty);
    const int cpCache = QProcess::execute(QStringLiteral("cp"),
        { QStringLiteral("-a"), kBase + "/library-template/shadercache", kBase + "/library-template/home", empty });
    CHECK(cpCache == 0, "the control root holds the template's shader and driver caches");
    Arm e, f;
    CHECK(runArm("empty", empty, e), "the EMPTY-library control ran");
    CHECK(runArm("10k+500", full, f), "the 10k-asset / 500-project library ran");
    target(f.bootMs, "ms", QStringLiteral("boot to the MCP answering with 10k assets + 500 projects (empty: %1 ms)").arg(e.bootMs));
    target(f.bootMs - e.bootMs, "ms", QStringLiteral("what the library adds to the boot"));
    target(f.gridMs, "ms", QStringLiteral("the Desktop grid's first build (inside a create's close) over %1 tiles, %2 decodes").arg(f.gridTiles).arg(f.gridDecodes));
    target(f.openGap, "ms", QStringLiteral("the worst UI gap of a project open over the library (empty: %1)").arg(e.openGap));
    target(f.createGap, "ms", QStringLiteral("the worst UI gap of a project create over the library (empty: %1)").arg(e.createGap));
    target(f.listMs, "ms", QStringLiteral("assets.list() over %1 rows (empty: %2 ms)").arg(f.listCount).arg(e.listMs));
    target(f.trayMs, "ms", QStringLiteral("a tray populate after the open (empty: %1 ms)").arg(e.trayMs));
    QDir(full).removeRecursively();
    QDir(empty).removeRecursively();
    return failures ? 1 : 0;
}
