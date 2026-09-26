/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// app.create_loop — FORTY CREATES BACK TO BACK, AND THE APP IS STILL UP (lane
// CREATE-CRASH-1).
//
// THE CRASH (spikes/d1-scale-fixtures/library-crash/): the D1 library generator
// drove `project.create` in batches of ten over a 10,000-asset library and the
// app died at the 33rd create — SIGSEGV in QWidgetPrivate::showChildren under
// MainWindow::applyDockVisibilityForSpace, the create's reveal showing the
// Properties dock again. The mechanism (spikes/create-crash-1/, and ui.show_walk,
// which reproduces the identical Qt frame stack deterministically): the
// column's showEvent paid its owed mount INSIDE Qt's walk of the scroll
// viewport's children, and a mount can free retired rows whose Qlementine focus
// frames are later siblings in that walk's snapshot.
//
// This suite is the product-level guard, driven the way the generator drove it
// — the REAL binary over MCP on the rig display, a library made through the
// import door (assets.importFile of distinct PNGs), `project.create` in batches
// of ten from one script each — and it asserts what a user needs: every create
// returns a project, the last one is the one open, the editor's Properties
// column settled with nothing owed, and the process never went away.
//
// NOT A CRASH REPRODUCER, and saying so is part of the suite: on the base
// build the fault needed an interleave the create hit by timing (retired rows
// still alive at the moment the dock was shown), and it did not recur in 930
// creates over a 10,000-asset library on this box (the lane's report). The
// deterministic red lives in ui.show_walk; this is the loop that must stay up.
#include "../support/mcpharness.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
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

// THE LIBRARY: big enough that the asset panel and the library listing are not
// trivially empty, small enough for the MERGE tier (the 10,000 of D1's fixture
// is ~14 minutes of imports; the mechanism does not scale with it).
static const int kAssets = 300;
static const int kCreates = 40;
static const int kBatch = 10;

int main(int argc, char **argv)
{
    QCoreApplication qapp(argc, argv);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    // THE HOME IS FRESH (app.create_loop.fresh_home wipes it): the cwd's parent.
    const QString home = QDir::cleanPath(QDir::currentPath() + "/..");
    const QString dataRoot = home + "/data";
    const QString src = home + "/library-src";
    QDir().mkpath(dataRoot);
    QDir().mkpath(src);

    // ---- the library's source files: distinct small PNGs --------------------
    for (int i = 0; i < kAssets; ++i) {
        QImage img(64, 64, QImage::Format_RGB888);
        const QColor a((i * 97) % 256, (i * 57) % 256, (i * 31) % 256);
        const QColor b((i * 13) % 256, 255 - (i * 7) % 256, (i * 3) % 256);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) img.setPixelColor(x, y, ((x / 8 + y / 8 + i) & 1) ? a : b);
        img.save(src + QStringLiteral("/loop-asset-%1.png").arg(i, 4, 10, QLatin1Char('0')));
    }

    // ---- the app, launched the way the generator launched it -----------------
    {
        QSettings s(QDir(dataRoot).filePath("jahsettings.ini"), QSettings::IniFormat);
        s.setValue(QStringLiteral("auto_save"), true);   // every create's close autosaves
        s.sync();
    }
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("JAHSHAKA_DATA_ROOT"), dataRoot);
    env.insert(QStringLiteral("HOME"), home);
    proc.setProcessEnvironment(env);
    QByteArray log;
    QString token;
    const quint16 port = freePort();
    if (!spawn(proc, port, &token, &log, QStringList(), 300000)) {
        std::printf("FAIL: the app did not boot\n");
        return 1;
    }
    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.clientName = QStringLiteral("create-loop");
    mcp.transferTimeoutMs = 300000;
    mcp.initialize();

    // ---- the library, through the import door -------------------------------
    QElapsedTimer t;
    t.start();
    int imported = 0;
    for (int a = 0; a < kAssets; a += 100) {
        const int b = std::min(kAssets, a + 100);
        imported += mcp.runScript(QStringLiteral(
            "var n=0;for(var i=%1;i<%2;i++){var s=''+i;while(s.length<4)s='0'+s;"
            "if(assets.importFile('%3/loop-asset-'+s+'.png'))n++;} n").arg(a).arg(b).arg(src))
                        .value("result").toInt();
    }
    const int listed = mcp.integer(QStringLiteral("assets.list().length"));
    CHECK(imported == kAssets && listed >= kAssets,
          "the library: %d of %d PNGs imported, assets.list() holds %d (%.1f s)",
          imported, kAssets, listed, t.elapsed() / 1000.0);

    // ---- THE LOOP: forty creates, ten per script, back to back ---------------
    t.restart();
    int created = 0;
    bool transportDied = false;
    for (int first = 0; first < kCreates; first += kBatch) {
        const QJsonObject r = mcp.runScript(QStringLiteral(
            "var n=0;for(var i=%1;i<%2;i++){var g=project.create('Loop project '+i);if(g)n++;} n")
                                                .arg(first).arg(first + kBatch));
        if (r.contains(QStringLiteral("transport"))) { transportDied = true; break; }
        created += r.value("result").toInt();
        if (proc.state() != QProcess::Running) break;
    }
    log += proc.readAll();
    const double loopS = t.elapsed() / 1000.0;
    CHECK(!transportDied && proc.state() == QProcess::Running,
          "the app is still up after the loop (the create-loop SIGSEGV)");
    CHECK(created == kCreates, "every create returned a project: %d of %d (%.1f s, %.0f ms each)",
          created, kCreates, loopS, created ? 1000.0 * loopS / created : 0.0);
    CHECK(!log.contains("*** CRASH"), "the app's own output reports no crash");

    if (proc.state() == QProcess::Running && !transportDied) {
        CHECK(mcp.integer(QStringLiteral("project.list().length")) == kCreates,
              "the library lists exactly the %d projects the loop made", kCreates);
        CHECK(mcp.string(QStringLiteral("(function(){var c=project.current();return c?c.name:''})()"))
                  == QStringLiteral("Loop project %1").arg(kCreates - 1),
              "the project open is the LAST one created");
        CHECK(mcp.string(QStringLiteral("project.openState()")) == QStringLiteral("idle"),
              "no install is still in flight");
        // THE COLUMN THE CRASH WAS IN: shown, settled, owing nothing.
        mcp.runScript(QStringLiteral("editor.frame(2)"));
        const QJsonObject stats = mcp.runScript(QStringLiteral("editor.propertiesStats()"))
                                      .value("result").toObject();
        std::printf("info: editor.propertiesStats() %s\n",
                    QJsonDocument(stats).toJson(QJsonDocument::Compact).constData());
        CHECK(stats.value("visible").toBool() && !stats.value("pending").toBool()
                  && !stats.value("deferredHidden").toBool() && stats.value("rows").toInt() > 0,
              "the Properties column is on screen, mounted, with nothing owed");
        mcp.quit();
    }
    if (!proc.waitForFinished(60000)) { proc.kill(); proc.waitForFinished(5000); }
    log += proc.readAll();
    CHECK(proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0,
          "the app quit cleanly (status %d, code %d)", int(proc.exitStatus()), proc.exitCode());

    if (failures) {
        const int at = log.lastIndexOf("*** CRASH");
        if (at >= 0) std::printf("info: %s\n", log.mid(at, 400).constData());
        std::printf("---- last 4 KB of the app's output ----\n%s\n", log.right(4096).constData());
    }
    std::printf(failures ? "app.create_loop: %d failure(s)\n" : "app.create_loop: ok\n", failures);
    return failures ? 1 : 0;
}
