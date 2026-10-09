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
// THE GENERATION IS ITS OWN ROW (GATE-SPEED-1 item 4, the gate-speed audit's T1):
// `test_scale_library --generate` is `scale.library.fixture`, a FIXTURES_SETUP row admitted
// like any app row (2 VRAM tokens) in the gate's parallel phase. The MEASUREMENT (this
// binary with no argument, `scale.library`) stays inside the whole-card lock and never
// generates: a missing template is a FAIL naming the fixture row. Generating inside the
// lock held every token on the box for 232-248 s of a fresh tree's serial phase against
// 48 s for the measurement itself.
//
// THE MEASUREMENTS (the numbers CREATE-GAP-1's suite takes, at this size), each against
// an EMPTY-library control in the same run: the boot to the MCP answering (the window is
// up and the Desktop built), the Desktop grid's build (desktop.gridStats), a project open
// and a project create (the UI-thread's worst gap from the heartbeat probe, and the
// verb's own ledger total), the Assets page's first build (assets.selected() builds it),
// and a tray populate + a library list (the verbs' wall time, measured on the script
// thread around one hop). Engine up on the rig display: the suite spawns the real binary.
//
// THE CONTROL IS THE SAME SESSION WITHOUT THE LIBRARY (D11-LIBRARY-SCALE). The first
// control was an EMPTY root: no project to open and none to close, so its create was
// 0.2 s where the library arm's create CLOSED an open project — a 1.7-1.9 s thumbnail
// render of a world compiling its shaders cold, which is not a library cost at all
// (measured: `closePrevious:save` = `saveOpen:thumbnail`, 1,750-1,900 ms, both before and
// after this lane). The control is now the template itself with the 10,000 library rows
// and all but two projects deleted: the same worlds, the same caches, the same open and
// the same create — the difference between the arms is the library and nothing else.
//
// THE BARS ARE COUNTED WORK since TESTING-CLEANUP-2 (bytes read and tiles built against the
// control; see "THE BARS ARE COUNTED WORK" in main); the millisecond readings below are printed.
// THE OLD BARS (D11-LIBRARY-SCALE §1, stated in milliseconds): at 10,000 assets + 500 projects
// the boot reaches the MCP in <= 20 s; the library
// adds <= 300 ms to the Desktop entry a create's close makes, the Desktop grid of 500
// projects never blocks the UI thread > 300 ms at a time, the library adds <= 200 ms to
// an open's worst UI gap (a create's worst gap is its close's thumbnail render in both
// arms — reported, see runArm; not a library cost); and NO
// LISTING SELECTS A THUMBNAIL over the whole driven session (app.queryLog: every
// statement that selects a thumbnail column is keyed by guid). The box's load average is
// printed beside every arm (a number read on a loaded box says so).
#include "../support/mcpharness.h"
#include "../src/data/database/casschema.h"

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
#include <QSqlDatabase>
#include <QSqlQuery>

#include <cmath>
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

static void report(double v, const char *unit, const QString &what)
{
    if (std::isnan(v)) {   // never measured (no sample, no project to open): never printed as a number
        std::printf("W14 unsampled %s: %s\n", unit, qPrintable(what));
        std::fflush(stdout);
        return;
    }
    std::printf("W14 %.1f %s: %s\n", v, unit, qPrintable(what));
    std::fflush(stdout);
}

static QString loadAverage()
{
    QFile f(QStringLiteral("/proc/loadavg"));
    if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("n/a");
    return QString::fromLatin1(f.readAll()).section(QLatin1Char(' '), 0, 2);
}

/// THE BYTES THE APP READ, so far (`rchar` of /proc/<pid>/io: every byte its read() calls
/// returned — files and sockets alike, page cache or not): library scale as COUNTED WORK
/// (TESTING-CLEANUP-2 item 6). -1 when unreadable.
static qint64 readChars(qint64 pid)
{
    QFile f(QStringLiteral("/proc/%1/io").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith("rchar:")) return line.mid(6).trimmed().toLongLong();
    return -1;
}

struct App {
    QProcess proc;
    McpClient mcp;
    QByteArray log;
    double bootMs = -1;
    qint64 bootRead = -1;   ///< bytes read by the time the MCP answered
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
    app.bootRead = readChars(app.proc.processId());
    app.mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    app.mcp.token = token;
    app.mcp.attach(app.proc, app.log);   // a transport failure prints the child's state + log tail
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

/// THE TEMPLATE IS A LIBRARY OF ONE GENERATION (FORWARD-ONLY-1,
/// services/librarygeneration.h): a template an older build generated is wiped by
/// the app at startup, so it is regenerated instead of copied.
static bool templateReady()
{
    const QString tmpl = kBase + "/library-template";
    return QFileInfo::exists(tmpl + "/.complete")
        && QFileInfo::exists(tmpl + QStringLiteral("/.generation-%1").arg(CasSchema::kUserVersion));
}

static bool ensureTemplate()
{
    const QString tmpl = kBase + "/library-template";
    const QString genMark =
        tmpl + QStringLiteral("/.generation-%1").arg(CasSchema::kUserVersion);
    if (templateReady()) {
        std::printf("library: the template is ready (%s) — nothing to generate\n", qPrintable(tmpl));
        return true;
    }
    if (QDir(tmpl).exists() && !QFileInfo::exists(genMark)) QDir(tmpl).removeRecursively();
    QDir().mkpath(tmpl);
    { QFile mark(genMark); mark.open(QIODevice::WriteOnly); }
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
    double gridMaxSliceMs = -1, desktopEntryMs = -1;
    double openGap = -1, openMs = -1, createGap = -1, createMs = -1, trayMs = -1, trayCount = -1, listMs = -1,
           listCount = -1, pageGap = -1, pageMs = -1;
    // COUNTED WORK (TESTING-CLEANUP-2 item 6): bytes read by the boot, the open and the create;
    // the grid's largest slice in tiles and the bound it promises.
    qint64 bootRead = -1, openRead = -1, createRead = -1;
    int gridMaxSliceTiles = -1, gridSliceBound = -1, gridSlices = -1;
    int listingThumbnailSelects = -1;   // statements that selected a thumbnail NOT keyed by guid
    int thumbnailSelects = -1;
    QString load;
};

static double gapAround(App &app, const QString &script, double *ledgerTotal, double *desktopEntryMs = nullptr)
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
        if (desktopEntryMs) {
            *desktopEntryMs = -1;
            for (const QJsonValue &v : t)
                if (v.toObject().value("stage").toString() == QStringLiteral("counter:closePrevious:switch"))
                    *desktopEntryMs = v.toObject().value("ms").toDouble();
        }
        // THE WHOLE LEDGER, one line: which stage a gap lives in is read from here.
        std::printf("   ledger %s\n", QJsonDocument(t).toJson(QJsonDocument::Compact).constData());
    }
    app.mcp.runScript(QStringLiteral("app.heartbeat(0)"));
    return gap;
}

static bool runArm(const char *label, const QString &dataRoot, Arm &a)
{
    a.load = loadAverage();
    App app;
    if (!launch(app, dataRoot)) { std::printf("FAIL: [%s] boot\n", label); return false; }
    a.bootMs = app.bootMs;
    a.bootRead = app.bootRead;
    const qint64 pid = app.proc.processId();
    // THE QUERY LOG over everything the session does from here (the boot built
    // nothing of the library since D11 — the Assets page waits for its first use).
    app.mcp.runScript(QStringLiteral("app.queryLog({on: true})"));
    app.mcp.runScript(QStringLiteral("editor.frame(10)"));

    // THE OPEN: the first project the library lists that is not open.
    const QString guid = eval(app, QStringLiteral(
        "(function(){var l=project.list();var c=project.current();for(var i=0;i<l.length;i++)"
        "if(!c||l[i].guid!==c.guid)return l[i].guid;return ''})()")).toString();
    if (!guid.isEmpty()) {
        const qint64 r0 = readChars(pid);
        a.openGap = gapAround(app, QStringLiteral("project.open('%1')").arg(guid), &a.openMs);
        const qint64 r1 = readChars(pid);
        a.openRead = (r0 >= 0 && r1 >= 0) ? r1 - r0 : -1;
    }
    // THE TRAY: what the open project's tray shows, one hop, timed on the script thread.
    const QJsonArray tray = eval(app, QStringLiteral(
        "(function(){var t=Date.now();var n=editor.trayAssets().length;return [Date.now()-t,n]})()")).toArray();
    if (tray.size() == 2) { a.trayMs = tray.at(0).toDouble(); a.trayCount = tray.at(1).toDouble(); }
    const QJsonArray list = eval(app, QStringLiteral(
        "(function(){var t=Date.now();var n=assets.list().length;return [Date.now()-t,n]})()")).toArray();
    if (list.size() == 2) { a.listMs = list.at(0).toDouble(); a.listCount = list.at(1).toDouble(); }
    // THE ASSETS PAGE'S FIRST BUILD (D11: on first use, not at boot) — a verb that
    // drives the page builds it; its worst UI gap and its wall time.
    {
        const QJsonArray page = eval(app, QStringLiteral(
            "(function(){var t=Date.now();assets.selected();return [Date.now()-t]})()")).toArray();
        if (!page.isEmpty()) a.pageMs = page.at(0).toDouble();
    }
    // THE CREATE (CREATE-GAP-1's measurement): over this library.
    // THE CREATE. Its worst UI gap is its CLOSE of the open project: that project's
    // thumbnail render (the ledger's saveOpen:thumbnail, 1.7-3.7 s measured in BOTH
    // arms, a fresh default scene's close included) — the same work with or without a
    // library, and the noisiest stage there is. What the library touches is the
    // Desktop the close enters (the ledger's closePrevious:switch: the grid's first
    // slice) and the grid's later slices (desktop.gridStats().lastBuildMaxSliceMs).
    {
        const qint64 r0 = readChars(pid);
        a.createGap = gapAround(app, QStringLiteral("project.create('Scale create %1')")
                                         .arg(QDateTime::currentMSecsSinceEpoch()), &a.createMs, &a.desktopEntryMs);
        const qint64 r1 = readChars(pid);
        a.createRead = (r0 >= 0 && r1 >= 0) ? r1 - r0 : -1;
    }
    // THE DESKTOP GRID: a driven session boots with the Desktop never SHOWN, so its grid
    // (a tile per project, a thumbnail decode each) is first built when a create's close
    // passes through the Desktop — INSIDE the create above. Its own ledger, read after.
    const QJsonObject g = eval(app, QStringLiteral("desktop.gridStats()")).toObject();
    a.gridMs = g.value("lastBuildMs").toDouble();
    a.gridDecodes = g.value("lastBuildDecodes").toDouble();
    a.gridTiles = g.value("tiles").toDouble();
    a.gridMaxSliceMs = g.value("lastBuildMaxSliceMs").toDouble(-1);
    a.gridMaxSliceTiles = g.value("lastBuildMaxSliceTiles").toInt(-1);
    a.gridSliceBound = g.value("sliceTiles").toInt(-1);
    a.gridSlices = g.value("lastBuildSlices").toInt(-1);
    std::printf("   [%s] desktop.gridStats() after the create %s\n", label,
                QJsonDocument(g).toJson(QJsonDocument::Compact).constData());
    // THE LOG: every thumbnail select of the session, and whether it was by guid.
    {
        const QJsonObject log = eval(app, QStringLiteral("app.queryLog({on: false})")).toObject();
        a.thumbnailSelects = 0;
        // A session whose log recorded NOTHING proves nothing (a binary without
        // the verb answers an empty object): that is a failure, not a pass.
        a.listingThumbnailSelects = log.value("statements").toInt() > 0 ? 0 : 1;
        for (const QJsonValue &v : log.value("thumbnailSelects").toArray()) {
            const QJsonObject e = v.toObject();
            a.thumbnailSelects += e.value("count").toInt();
            if (!e.value("byGuid").toBool()) {
                a.listingThumbnailSelects += e.value("count").toInt();
                std::printf("   [%s] A LISTING SELECTED A THUMBNAIL: %s x%d — %s\n", label,
                            qPrintable(e.value("name").toString()), e.value("count").toInt(),
                            qPrintable(e.value("sql").toString()));
            }
        }
        std::printf("   [%s] query log: %d statements, %d thumbnail selects (%d not keyed by guid)\n", label,
                    log.value("statements").toInt(), a.thumbnailSelects, a.listingThumbnailSelects);
    }
    quit(app);
    // THE APP'S OWN OUTPUT, kept beside the run (an Xid or a crash is triaged from it).
    QFile out(kBase + QStringLiteral("/library-%1-app.log").arg(QString::fromLatin1(label).section('+', 0, 0)));
    if (out.open(QIODevice::WriteOnly)) out.write(app.log + app.proc.readAll());
    std::printf("W14 [%-7s] load %s | boot->MCP %8.0f ms | grid build (inside the create) %7.1f ms, %5.0f decodes, %5.0f tiles | open: worst UI gap "
                "%7.1f ms, ledger %7.1f ms | tray %4.0f ms (%3.0f tiles) | assets.list %6.0f ms (%5.0f rows) | "
                "Assets page first build %6.0f ms | create: worst UI gap %7.1f ms, ledger %7.1f ms, Desktop entry %6.1f ms, "
                "grid's longest slice %5.1f ms\n",
                label, qPrintable(a.load), a.bootMs, a.gridMs, a.gridDecodes, a.gridTiles, a.openGap, a.openMs,
                a.trayMs, a.trayCount, a.listMs, a.listCount, a.pageMs, a.createGap, a.createMs, a.desktopEntryMs,
                a.gridMaxSliceMs);
    std::printf("W14 [%-7s] COUNTED: read %.1f MB by the boot, %.1f MB in the open, %.1f MB in the create; "
                "grid %d slice(s), the largest %d tile(s) (bound %d)\n",
                label, a.bootRead / 1e6, a.openRead / 1e6, a.createRead / 1e6, a.gridSlices,
                a.gridMaxSliceTiles, a.gridSliceBound);
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication qapp(argc, argv);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // a line at a time: the log is read while it runs
    // scale.library.fixture: generate (or resume) the template and stop — outside the lock.
    if (qapp.arguments().contains(QStringLiteral("--generate"))) return ensureTemplate() ? 0 : 1;
    // scale.library: measure only. The fixture row ran first (FIXTURES_REQUIRED); a missing
    // template here means it failed or was skipped, never a reason to generate in the lock.
    if (!templateReady()) {
        std::printf("FAIL: the library template is missing or of another generation (%s/library-template) — "
                    "scale.library.fixture (test_scale_library --generate) makes it\n", qPrintable(kBase));
        return 1;
    }
    // THE RUN'S COPY of the template, and the CONTROL — both fresh copies of it (one
    // cache state, the same worlds: a first-seconds measurement is the shader storm,
    // DOCS/traps/GATE_AND_RIG.md).
    const QString full = kBase + "/library-run", empty = kBase + "/library-empty";
    QDir(full).removeRecursively();
    QDir(empty).removeRecursively();
    QElapsedTimer t;
    t.start();
    const int cp = QProcess::execute(QStringLiteral("cp"), { QStringLiteral("-a"), kBase + "/library-template", full });
    CHECK(cp == 0, "the template copied (%.1f s)", t.elapsed() / 1000.0);
    const int cpControl = QProcess::execute(QStringLiteral("cp"), { QStringLiteral("-a"), kBase + "/library-template", empty });
    CHECK(cpControl == 0, "the control copied");
    // THE CONTROL WITHOUT THE LIBRARY: the imported rows and every project but the
    // first two CREATED ones go (their folders stay on disk, unlisted — nothing reads
    // them). What remains is the session shape the library arm has: a project to open
    // and one open to close.
    {
        QSqlDatabase conn = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("scale-control"));
        conn.setDatabaseName(empty + "/JahLibrary.db");
        bool ok = conn.open();
        QSqlQuery q(conn);
        ok = ok && q.exec(QStringLiteral("DELETE FROM assets WHERE view_filter = 2 AND (project_guid IS NULL OR project_guid = '')"));
        ok = ok && q.exec(QStringLiteral("DELETE FROM projects WHERE name NOT IN ('Scale project 0', 'Scale project 1')"));
        int assets = -1, projects = -1;
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM assets WHERE view_filter = 2")) && q.next()) assets = q.value(0).toInt();
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM projects")) && q.next()) projects = q.value(0).toInt();
        q.exec(QStringLiteral("VACUUM"));
        conn.close();
        conn = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("scale-control"));
        CHECK(ok && assets == 0 && projects == 2, "the control holds 0 library assets and 2 projects (%d / %d)", assets, projects);
    }
    Arm e, f;
    CHECK(runArm("control", empty, e), "the control (the same session, no library) ran");
    CHECK(runArm("10k+500", full, f), "the 10k-asset / 500-project library ran");
    report(f.bootMs, "ms", QStringLiteral("boot to the MCP answering with 10k assets + 500 projects (control: %1 ms; load %2)").arg(e.bootMs).arg(f.load));
    report(f.bootMs - e.bootMs, "ms", QStringLiteral("what the library adds to the boot"));
    report(f.gridMs, "ms", QStringLiteral("the Desktop grid's first build (inside a create's close) over %1 tiles, %2 decodes on the UI thread").arg(f.gridTiles).arg(f.gridDecodes));
    report(f.pageMs, "ms", QStringLiteral("the Assets page's first build over the library (control: %1 ms)").arg(e.pageMs));
    report(f.openGap, "ms", QStringLiteral("the worst UI gap of a project open over the library (control: %1)").arg(e.openGap));
    report(f.openGap - e.openGap, "ms", QStringLiteral("what the library adds to the open's worst UI gap"));
    report(f.createGap, "ms", QStringLiteral("the worst UI gap of a project create over the library (control: %1)").arg(e.createGap));
    report(f.createGap - e.createGap, "ms", QStringLiteral("what the library adds to the create's worst UI gap (its close's thumbnail render dominates both arms)"));
    report(f.desktopEntryMs, "ms", QStringLiteral("the create's Desktop entry, the grid's first slice included (control: %1 ms)").arg(e.desktopEntryMs));
    report(f.gridMaxSliceMs, "ms", QStringLiteral("the Desktop grid's longest slice (the build's worst UI-thread block)"));
    report(f.listMs, "ms", QStringLiteral("assets.list() over %1 rows (control: %2 ms)").arg(f.listCount).arg(e.listMs));
    report(f.trayMs, "ms", QStringLiteral("a tray populate after the open (control: %1 ms)").arg(e.trayMs));
    // THE BARS ARE COUNTED WORK (TESTING-CLEANUP-2 item 6, the lead's decision: "tests count frames,
    // never wall-clock"). The four millisecond bars D11-LIBRARY-SCALE stated (boot <= 20 s, a grid
    // slice <= 300 ms, the Desktop entry +300 ms, an open's worst gap +200 ms) read the BOX as much
    // as the code — 19 reds in 192 runs inside the timing phase, which holds the GPU, never the CPU
    // or the disk. What they guard is that the work does not grow with the library, and that is
    // COUNTED: bytes read (rchar) and tiles built, against the same session with no library. The
    // milliseconds above are still printed beside every arm.
    //   * PER LIBRARY ASSET, a session step may read what the library ADDS only up to
    //     kBytesPerAsset — 50 bytes, under ONE row's listing (a guid alone is 36): a step that lists
    //     the library reads >= ~100 bytes an asset, a step that does not reads a few pages.
    //   * THE GRID IS SLICED: no event-loop turn builds more than the slice's bound of tiles
    //     (the Desktop entry a create's close makes builds exactly one slice), every tile is built,
    //     and the UI thread decodes no thumbnail.
    // MEASURED (2026-10-09, :71, this tree): the open adds 0.1 MB (10 B/asset), the grid 16 slices of
    // <= 32 tiles over 501, 0 decodes — and the BOOT adds 45.9 MB (4.6 KB/asset: it reads 47.6 MB of
    // the 130 MB JahLibrary.db, strace -y) — a library-proportional boot the 20 s bar never saw.
    const double kBytesPerAsset = 50.0;
    const double bootAdds = double(f.bootRead - e.bootRead), openAdds = double(f.openRead - e.openRead);
    CHECK(f.bootRead >= 0 && e.bootRead >= 0 && bootAdds <= kBytesPerAsset * kAssets,
          "BAR: the boot reads what the library adds at <= %.0f bytes an asset (%.1f MB = %.0f bytes an asset over "
          "%d; boot %.0f ms, load %s)", kBytesPerAsset, bootAdds / 1e6, bootAdds / kAssets, kAssets, f.bootMs,
          qPrintable(f.load));
    CHECK(f.openRead >= 0 && e.openRead >= 0 && openAdds <= kBytesPerAsset * kAssets,
          "BAR: a project open reads what the library adds at <= %.0f bytes an asset (%.2f MB = %.1f bytes an asset)",
          kBytesPerAsset, openAdds / 1e6, openAdds / kAssets);
    const int wantSlices = f.gridSliceBound > 0 ? (int(f.gridTiles) + f.gridSliceBound - 1) / f.gridSliceBound : -1;
    CHECK(f.gridSliceBound > 0 && f.gridMaxSliceTiles >= 1 && f.gridMaxSliceTiles <= f.gridSliceBound &&
              f.gridSlices >= wantSlices && f.gridDecodes == 0,
          "BAR: the Desktop grid of %.0f tiles is built %d slice(s), none over %d tiles (the largest %d), and the "
          "UI thread decodes %.0f thumbnails (the Desktop entry builds one slice)",
          f.gridTiles, f.gridSlices, f.gridSliceBound, f.gridMaxSliceTiles, f.gridDecodes);
    CHECK(f.listingThumbnailSelects == 0 && e.listingThumbnailSelects == 0,
          "BAR: no listing selected a thumbnail in either session (%d / %d not keyed by guid)",
          f.listingThumbnailSelects, e.listingThumbnailSelects);
    QDir(full).removeRecursively();
    QDir(empty).removeRecursively();
    return failures ? 1 : 0;
}
